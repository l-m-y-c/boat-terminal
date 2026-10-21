#include "n2k.h"

#include "n2k_sim.h"
#include "n2k_to_0183.h"
#include "nmea_io.h"

#include <NMEA2000.h>
#include <N2kMessages.h>

#include <Arduino.h>
#include <Preferences.h>
#include <cstring>
#include <strings.h>

#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "driver/gpio.h"
#include "driver/twai.h"

#include "boat_config.h"

#ifndef NMEA_SOURCE
#define NMEA_SOURCE NMEA_SOURCE_SIM
#endif

/* Waveshare ESP32-S3-Touch-LCD-7 CAN header. TJA1051, not isolated.
 * GPIO19/20 are also USB D-/D+. CH422G EXIO5 selects which one is wired:
 * high = CAN, low = USB. The 120 Ω jumper on the board must be open. */
static constexpr gpio_num_t kCanTx = GPIO_NUM_20;
static constexpr gpio_num_t kCanRx = GPIO_NUM_19;

/* Local source address only. Listen-only mode never transmits ISO Address
 * Claim (PGN 60928). 22 is just the address the library keeps internally. */
static constexpr uint8_t kLocalAddress = 22;

static constexpr uint32_t kQuietMs = 3000;
static constexpr uint32_t kRetryMs = 5000;

/* Receive path, sized so nothing is lost while loop() is busy drawing or
 * decoding a photo:
 *  - the TWAI ISR fills the driver queue in internal RAM;
 *  - the receive task, above loop() on the same core, moves each frame into
 *    the frame queue as it arrives;
 *  - loop() drains the frame queue completely on every pass.
 * 2048 frames is over a second of a fully loaded 250 kbit/s bus. */
static constexpr uint32_t kDriverQueueFrames = 256;
static constexpr uint32_t kFrameQueueFrames = 2048;
/* ParseMessages() takes at most 20 frames a call. */
static constexpr int kParseRounds = 128;
/* Fast-packet messages being reassembled at once, across all sources. */
static constexpr uint16_t kReassemblySlots = 32;

static constexpr UBaseType_t kRxTaskPriority = 10;
static constexpr UBaseType_t kSimTaskPriority = 4;
static constexpr BaseType_t kTaskCore = 1;

static constexpr const char *kPrefsSpace = "lmyc";
static constexpr const char *kPrefsSource = "nmea_src";

/* The library's frame source is the frame queue. The controller belongs
 * to the receive task, and nothing is ever transmitted. */
class N2kStack : public tNMEA2000 {
protected:
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf,
                      bool wait_sent = true) override;
    bool CANOpen(void) override { return true; }
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override;
};

static N2kStack g_stack;
static N2kCanSelectFn g_select = nullptr;
static DataSource g_source = DataSource::Simulator;
static bool g_uart = false;
static bool g_tx_attempted = false;
static uint32_t g_restart_at = 0;

static QueueHandle_t g_queue = nullptr;
static TaskHandle_t g_rx_task = nullptr;
static TaskHandle_t g_sim_task = nullptr;
static SemaphoreHandle_t g_released = nullptr;

/* Written by the producer task (receive or simulator, never both), read by
 * loop(). Aligned 32-bit loads and stores are atomic on the ESP32-S3. */
static volatile uint32_t g_frames = 0;
static volatile uint32_t g_last_frame_ms = 0;
static volatile uint32_t g_queue_peak = 0;
static volatile uint32_t g_lost_queue = 0;
static volatile uint32_t g_ignored = 0;
static volatile uint32_t g_lost_driver = 0;
static volatile uint32_t g_lost_controller = 0;
static volatile uint32_t g_bus_errors = 0;
static volatile uint32_t g_rec = 0;
static volatile bool g_driver_on = false;
static volatile bool g_bus_off = false;
static volatile bool g_failed = false;
static volatile bool g_release_req = false;
static volatile bool g_sim_paused = false;

/* loop() only. */
static uint32_t g_messages = 0;
static uint32_t g_rate_ms = 0;
static uint32_t g_rate_frames = 0;
static uint32_t g_rate_messages = 0;
static uint32_t g_fps = 0;
static uint32_t g_fps_peak = 0;
static uint32_t g_mps = 0;
static uint32_t g_parse_peak_us = 0;
static constexpr size_t kPgnSetMax = 96;
static uint32_t g_pgns[kPgnSetMax];
static size_t g_pgn_n = 0;
static uint32_t g_sources[256 / 32];
static uint32_t g_source_n = 0;

static void n2k_log(const char *msg)
{
    /* UART0 (CH343) is up in every mode. USB Serial only while the mux is on USB. */
    Serial0.println(msg);
    if (!g_uart) {
        Serial.println(msg);
    }
}

static DataSource default_source(void)
{
    return NMEA_SOURCE == NMEA_SOURCE_SIM ? DataSource::Simulator : DataSource::Nmea2000;
}

static DataSource load_source(void)
{
    Preferences prefs;
    if (!prefs.begin(kPrefsSpace, true)) {
        return default_source();
    }
    const uint8_t saved = prefs.getUChar(kPrefsSource, 0xff);
    prefs.end();
    if (saved == static_cast<uint8_t>(DataSource::Simulator)) {
        return DataSource::Simulator;
    }
    if (saved == static_cast<uint8_t>(DataSource::Nmea2000)) {
        return DataSource::Nmea2000;
    }
    return default_source();
}

static bool save_source(DataSource source)
{
    Preferences prefs;
    if (!prefs.begin(kPrefsSpace, false)) {
        return false;
    }
    const size_t n = prefs.putUChar(kPrefsSource, static_cast<uint8_t>(source));
    prefs.end();
    return n == 1;
}

/* ---- frame queue ------------------------------------------------------ */

static bool push_frame(const N2kFrame &frame, void * /*ctx*/)
{
    g_frames = g_frames + 1;
    g_last_frame_ms = millis();
    if (xQueueSend(g_queue, &frame, 0) != pdTRUE) {
        g_lost_queue = g_lost_queue + 1;
        return false;
    }
    const uint32_t depth = static_cast<uint32_t>(uxQueueMessagesWaiting(g_queue));
    if (depth > g_queue_peak) {
        g_queue_peak = depth;
    }
    return true;
}

static QueueHandle_t make_queue(void)
{
    /* Keep internal RAM for Wi-Fi, BLE and the TWAI driver. */
    const size_t bytes = kFrameQueueFrames * sizeof(N2kFrame);
    auto *storage = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (storage == nullptr) {
        storage = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    auto *control = static_cast<StaticQueue_t *>(heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_INTERNAL));
    if (storage == nullptr || control == nullptr) {
        return nullptr;
    }
    return xQueueCreateStatic(kFrameQueueFrames, sizeof(N2kFrame), storage, control);
}

bool N2kStack::CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf,
                            bool wait_sent)
{
    (void)id;
    (void)len;
    (void)buf;
    (void)wait_sent;
    /* Listen-only. Returning true keeps the stack from retrying a send
     * that must never reach the wire. TWAI is also in listen-only mode. */
    if (!g_tx_attempted) {
        g_tx_attempted = true;
        n2k_log("N2K: blocked a transmit (listen-only)");
    }
    return true;
}

bool N2kStack::CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf)
{
    if (g_queue == nullptr || buf == nullptr) {
        return false;
    }
    N2kFrame frame;
    if (xQueueReceive(g_queue, &frame, 0) != pdTRUE) {
        return false;
    }
    id = frame.id;
    len = frame.len;
    memcpy(buf, frame.data, frame.len);
    return true;
}

/* ---- CAN controller (receive task only) -------------------------------- */

static void hold_tx_recessive(void)
{
    /* TXD high is recessive. A floating TXD on the TJA1051 can chatter. */
    gpio_reset_pin(kCanTx);
    gpio_set_direction(kCanTx, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kCanTx, GPIO_PULLUP_ONLY);
    gpio_reset_pin(kCanRx);
    gpio_set_direction(kCanRx, GPIO_MODE_INPUT);
}

/* Driver counters restart at zero on every install. */
static uint32_t g_base_missed = 0;
static uint32_t g_base_overrun = 0;
static uint32_t g_base_bus_errors = 0;

static void read_driver_counters(bool fold)
{
    twai_status_info_t st = {};
    if (twai_get_status_info(&st) != ESP_OK) {
        return;
    }
    g_lost_driver = g_base_missed + st.rx_missed_count;
    g_lost_controller = g_base_overrun + st.rx_overrun_count;
    g_bus_errors = g_base_bus_errors + st.bus_error_count;
    g_rec = st.rx_error_counter;
    if (fold) {
        g_base_missed = g_lost_driver;
        g_base_overrun = g_lost_controller;
        g_base_bus_errors = g_bus_errors;
    }
}

static bool install_listen(void)
{
    twai_general_config_t general =
        TWAI_GENERAL_CONFIG_DEFAULT(kCanTx, kCanRx, TWAI_MODE_LISTEN_ONLY);
    general.tx_queue_len = 0;
    general.rx_queue_len = kDriverQueueFrames;
    general.alerts_enabled = TWAI_ALERT_BUS_OFF;
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
        return false;
    }
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }
    g_driver_on = true;
    return true;
}

static void uninstall_driver(void)
{
    if (!g_driver_on) {
        return;
    }
    /* Stop first so nothing arrives between the count and the uninstall.
     * Frames still in the driver queue are about to be freed: count them. */
    twai_stop();
    twai_status_info_t st = {};
    const uint32_t stranded = twai_get_status_info(&st) == ESP_OK ? st.msgs_to_rx : 0;
    read_driver_counters(true);
    g_base_missed += stranded;
    g_lost_driver = g_base_missed;
    twai_driver_uninstall();
    g_driver_on = false;
    hold_tx_recessive();
}

static void rx_task(void * /*arg*/)
{
    uint32_t retry_at = millis();
    uint32_t status_at = 0;
    bool announced = false;
    for (;;) {
        if (g_release_req) {
            uninstall_driver();
            xSemaphoreGive(g_released);
            vTaskSuspend(nullptr);
        }

        if (!g_driver_on) {
            if (static_cast<int32_t>(millis() - retry_at) >= 0) {
                if (install_listen()) {
                    g_bus_off = false;
                    g_failed = false;
                    if (!announced) {
                        announced = true;
                        n2k_log("N2K: listen-only 250 kbit, GPIO20 TX / GPIO19 RX, no address claim");
                    } else {
                        n2k_log("N2K: listening again");
                    }
                } else {
                    g_failed = !g_bus_off;
                    retry_at = millis() + kRetryMs;
                    n2k_log("N2K: TWAI listen-only would not start, retrying");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        twai_message_t message = {};
        if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
            if (message.extd && !message.rtr) {
                N2kFrame frame;
                frame.id = message.identifier & 0x1fffffffu;
                frame.len = message.data_length_code > 8 ? 8 : message.data_length_code;
                memcpy(frame.data, message.data, frame.len);
                push_frame(frame, nullptr);
            } else {
                /* 11-bit and remote frames are not NMEA 2000. Counted, not decoded. */
                g_ignored = g_ignored + 1;
            }
        }

        const uint32_t now = millis();
        if (now - status_at < 100) {
            continue;
        }
        status_at = now;
        read_driver_counters(false);
        uint32_t alerts = 0;
        if (twai_read_alerts(&alerts, 0) == ESP_OK && (alerts & TWAI_ALERT_BUS_OFF)) {
            /* Stop the controller and hold TXD recessive. Do not run bus-off
             * recovery, which drives the line. Retry later, still listen-only. */
            uninstall_driver();
            g_bus_off = true;
            retry_at = millis() + kRetryMs;
            n2k_log("N2K: bus-off, dropped off (TXD recessive)");
        }
    }
}

static bool open_can(void)
{
    /* USB Serial/JTAG owns GPIO19/20 until it is stopped. */
    Serial.flush();
    Serial.end();
    g_uart = true;
    /* TXD is a recessive input before the transceiver is wired to it. */
    hold_tx_recessive();
    if (g_select != nullptr) {
        g_select(true);
    }
    delay(20);
    Serial0.begin(115200, SERIAL_8N1, 44, 43);

    g_released = xSemaphoreCreateBinary();
    return xTaskCreatePinnedToCore(rx_task, "n2k_rx", 4096, nullptr, kRxTaskPriority, &g_rx_task,
                                   kTaskCore) == pdPASS;
}

/* ---- simulator --------------------------------------------------------- */

static void sim_task(void * /*arg*/)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
        /* Paused for canned PGNs, or a person is pasting sentences on the console. */
        if (g_sim_paused || !nmea_io_quiet()) {
            continue;
        }
        n2k_sim_step(millis(), push_frame, nullptr);
    }
}

/* ---- decoding (loop) --------------------------------------------------- */

static void note_message(const tN2kMsg &msg)
{
    ++g_messages;
    const uint32_t pgn = static_cast<uint32_t>(msg.PGN);
    bool known = false;
    for (size_t i = 0; i < g_pgn_n; ++i) {
        if (g_pgns[i] == pgn) {
            known = true;
            break;
        }
    }
    if (!known && g_pgn_n < kPgnSetMax) {
        g_pgns[g_pgn_n++] = pgn;
    }
    const uint8_t src = msg.Source;
    const uint32_t bit = 1u << (src & 31u);
    if ((g_sources[src >> 5] & bit) == 0) {
        g_sources[src >> 5] |= bit;
        ++g_source_n;
    }
}

static void on_n2k_msg(const tN2kMsg &msg)
{
    note_message(msg);
    n2k_to_0183_handle(msg);
}

static void roll_rates(void)
{
    const uint32_t now = millis();
    if (g_rate_ms == 0) {
        g_rate_ms = now;
        g_rate_frames = g_frames;
        g_rate_messages = g_messages;
        return;
    }
    const uint32_t span = now - g_rate_ms;
    if (span < 1000) {
        return;
    }
    const uint32_t frames = g_frames;
    g_fps = static_cast<uint32_t>((static_cast<uint64_t>(frames - g_rate_frames) * 1000u) / span);
    g_mps = static_cast<uint32_t>((static_cast<uint64_t>(g_messages - g_rate_messages) * 1000u) / span);
    if (g_fps > g_fps_peak) {
        g_fps_peak = g_fps;
    }
    g_rate_ms = now;
    g_rate_frames = frames;
    g_rate_messages = g_messages;
}

static void restart_now(void)
{
    g_restart_at = 0;
    if (g_rx_task != nullptr && g_released != nullptr) {
        g_release_req = true;
        xSemaphoreTake(g_released, pdMS_TO_TICKS(300));
    }
    if (g_sim_task != nullptr) {
        vTaskSuspend(g_sim_task);
    }
    /* Hand GPIO19/20 back to USB for the reset. setup() puts the mux on USB
     * again right after the board init, which drives every expander pin high. */
    if (g_uart && g_select != nullptr) {
        g_select(false);
    }
    Serial0.flush();
    if (!g_uart) {
        Serial.flush();
    }
    delay(50);
    esp_restart();
}

void n2k_init(N2kCanSelectFn select_can)
{
    g_select = select_can;
    g_source = load_source();
    g_queue = make_queue();
    if (g_queue == nullptr) {
        n2k_log("N2K: no memory for the frame queue");
        return;
    }

    g_stack.SetMode(tNMEA2000::N2km_ListenOnly, kLocalAddress);
    g_stack.EnableForward(false);
    g_stack.SetN2kCANMsgBufSize(kReassemblySlots);
    g_stack.SetMsgHandler(on_n2k_msg);
    /* The library holds back for 200 ms after opening and throws away any
     * frame it reads meanwhile. Finish that before a producer starts, so
     * no frame is ever discarded uncounted. */
    g_stack.Open();
    for (int i = 0; i < 40 && !g_stack.IsOpen(); ++i) {
        delay(10);
        g_stack.Open();
    }
    if (!g_stack.IsOpen()) {
        n2k_log("N2K: stack open failed");
    }

    if (g_source == DataSource::Simulator) {
        /* A restart from NMEA 2000 can leave the mux on CAN. */
        if (g_select != nullptr) {
            g_select(false);
        }
        n2k_sim_reset();
        xTaskCreatePinnedToCore(sim_task, "n2k_sim", 4096, nullptr, kSimTaskPriority, &g_sim_task,
                                kTaskCore);
        n2k_log("NMEA source: simulator (virtual NMEA 2000 bus, CAN closed)");
        return;
    }

    if (!open_can()) {
        g_failed = true;
        n2k_log("N2K: could not start the receive task");
        return;
    }
    n2k_log("NMEA source: NMEA 2000 listen-only. Serial log is on the UART Type-C socket.");
}

void n2k_poll(void)
{
    if (g_restart_at != 0 && static_cast<int32_t>(millis() - g_restart_at) >= 0) {
        restart_now();
    }
    if (g_queue == nullptr) {
        return;
    }
    const int64_t start = esp_timer_get_time();
    for (int i = 0; i < kParseRounds && uxQueueMessagesWaiting(g_queue) > 0; ++i) {
        g_stack.ParseMessages();
    }
    const uint32_t spent = static_cast<uint32_t>(esp_timer_get_time() - start);
    if (spent > g_parse_peak_us) {
        g_parse_peak_us = spent;
    }
    n2k_to_0183_poll();
    roll_rates();
}

DataSource n2k_source(void) { return g_source; }

const char *n2k_source_name(DataSource source)
{
    return source == DataSource::Simulator ? "Simulator" : "NMEA 2000";
}

bool n2k_switch_source(DataSource next)
{
    if (next == g_source) {
        return true;
    }
    if (!save_source(next)) {
        n2k_log("NMEA source: could not save the choice");
        return false;
    }
    char line[80];
    snprintf(line, sizeof(line), "NMEA source: %s saved, restarting", n2k_source_name(next));
    n2k_log(line);
    g_restart_at = millis() + 600;
    return true;
}

bool n2k_restart_pending(void) { return g_restart_at != 0; }

bool n2k_quiet(void)
{
    const uint32_t last = g_last_frame_ms;
    return last == 0 || (millis() - last) > kQuietMs;
}

static BusState bus_state(void)
{
    if (g_source == DataSource::Simulator) {
        return g_sim_paused ? BusState::Paused : BusState::Simulated;
    }
    if (g_driver_on) {
        if (g_frames == 0) {
            return BusState::Waiting;
        }
        return n2k_quiet() ? BusState::Quiet : BusState::Live;
    }
    if (g_bus_off) {
        return BusState::BusOff;
    }
    return g_failed ? BusState::Failed : BusState::Opening;
}

const char *n2k_state_name(BusState state)
{
    switch (state) {
    case BusState::Simulated:
        return "Simulated";
    case BusState::Paused:
        return "Paused";
    case BusState::Opening:
        return "Opening";
    case BusState::Waiting:
        return "No frames yet";
    case BusState::Live:
        return "Live";
    case BusState::Quiet:
        return "Quiet";
    case BusState::BusOff:
        return "Bus-off";
    case BusState::Failed:
        return "CAN failed";
    }
    return "?";
}

void n2k_stats(N2kStats *out)
{
    if (out == nullptr) {
        return;
    }
    *out = {};
    out->source = g_source;
    out->state = bus_state();
    out->frames = g_frames;
    out->frames_per_s = g_fps;
    out->frames_per_s_peak = g_fps_peak;
    out->messages = g_messages;
    out->messages_per_s = g_mps;
    out->queue_capacity = kFrameQueueFrames;
    out->queue_depth = g_queue != nullptr ? static_cast<uint32_t>(uxQueueMessagesWaiting(g_queue)) : 0;
    out->queue_peak = g_queue_peak;
    out->lost_queue = g_lost_queue;
    out->lost_driver = g_lost_driver;
    out->lost_controller = g_lost_controller;
    out->ignored = g_ignored;
    out->bus_errors = g_bus_errors;
    out->rx_error_counter = g_rec;
    out->pgn_types = static_cast<uint32_t>(g_pgn_n);
    out->devices = g_source_n;
    const uint32_t last = g_last_frame_ms;
    out->last_frame_ms = last == 0 ? UINT32_MAX : millis() - last;
    out->parse_peak_us = g_parse_peak_us;
}

void n2k_set_sim_gnss(bool on) { n2k_sim_set_gnss(on); }

bool n2k_using_uart(void) { return g_uart; }
uint32_t n2k_frame_count(void) { return g_frames; }

const char *n2k_fix_tag(void)
{
    switch (bus_state()) {
    case BusState::Simulated:
        return "SIM";
    case BusState::Paused:
        return "PLAY";
    case BusState::Live:
        return "BOAT";
    default:
        return "N2K";
    }
}

void n2k_status_line(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    const uint32_t lost = g_lost_queue + g_lost_driver + g_lost_controller;
    snprintf(out, cap, "%s - %s - %lu frames/s - %lu lost", n2k_source_name(g_source),
             n2k_state_name(bus_state()), static_cast<unsigned long>(g_fps),
             static_cast<unsigned long>(lost));
}

static void log_stats(void)
{
    N2kStats s;
    n2k_stats(&s);
    char line[200];
    snprintf(line, sizeof(line),
             "NMEA %s %s  frames %lu (%lu/s, peak %lu)  msgs %lu  queue peak %lu/%lu  "
             "lost %lu/%lu/%lu  bus errors %lu  pgns %lu  devices %lu",
             n2k_source_name(s.source), n2k_state_name(s.state),
             static_cast<unsigned long>(s.frames), static_cast<unsigned long>(s.frames_per_s),
             static_cast<unsigned long>(s.frames_per_s_peak), static_cast<unsigned long>(s.messages),
             static_cast<unsigned long>(s.queue_peak), static_cast<unsigned long>(s.queue_capacity),
             static_cast<unsigned long>(s.lost_queue), static_cast<unsigned long>(s.lost_driver),
             static_cast<unsigned long>(s.lost_controller), static_cast<unsigned long>(s.bus_errors),
             static_cast<unsigned long>(s.pgn_types), static_cast<unsigned long>(s.devices));
    n2k_log(line);
}

static void switch_from_console(DataSource next)
{
    if (next == g_source) {
        if (g_sim_paused) {
            g_sim_paused = false;
            n2k_log("NMEA: simulator resumed");
            return;
        }
        n2k_log(next == DataSource::Simulator ? "NMEA: already on the simulator"
                                              : "NMEA: already on NMEA 2000");
        return;
    }
    n2k_switch_source(next);
}

/* Pausing for canned PGNs: frames the simulator already queued would
 * overwrite the demo values in the same pass, so they are dropped and
 * counted as ignored. */
static void pause_simulator(void)
{
    if (g_sim_paused) {
        return;
    }
    g_sim_paused = true;
    delay(20); /* the simulator task steps every 10 ms */
    const uint32_t queued = static_cast<uint32_t>(uxQueueMessagesWaiting(g_queue));
    xQueueReset(g_queue);
    g_ignored = g_ignored + queued;
}

static bool require_simulator(const char *what)
{
    if (g_source == DataSource::Simulator) {
        return true;
    }
    char line[96];
    snprintf(line, sizeof(line), "NMEA %s runs only on the simulator. This terminal is on NMEA 2000.",
             what);
    n2k_log(line);
    return false;
}

bool n2k_handle_command(const char *line)
{
    if (line == nullptr) {
        return false;
    }
    if (strncmp(line, "NMEA ", 5) != 0 && strncmp(line, "nmea ", 5) != 0) {
        return false;
    }
    const char *arg = line + 5;
    while (*arg == ' ') {
        ++arg;
    }
    if (strcasecmp(arg, "SIM") == 0) {
        switch_from_console(DataSource::Simulator);
    } else if (strcasecmp(arg, "N2K") == 0) {
        switch_from_console(DataSource::Nmea2000);
    } else if (strcasecmp(arg, "AUTO") == 0) {
        n2k_log("NMEA AUTO was removed: a live bus never falls back to simulated data. "
                "Use NMEA N2K.");
    } else if (strcasecmp(arg, "STATS") == 0) {
        log_stats();
    } else if (strcasecmp(arg, "DEMO") == 0) {
        if (require_simulator("DEMO")) {
            pause_simulator();
            n2k_play_demo();
            n2k_log("NMEA play: depth 42.0 m, COG 90, SOG 7.5 kn, wind 18 kn. "
                    "NMEA SIM resumes the simulator.");
        }
    } else if (strncasecmp(arg, "PGN", 3) == 0) {
        if (require_simulator("PGN")) {
            pause_simulator();
            if (n2k_play_line(arg)) {
                n2k_log("NMEA play: PGN accepted");
            } else {
                n2k_log("NMEA PGN <number> [src=N] <hex>");
            }
        }
    } else {
        n2k_log("NMEA command: SIM, N2K, STATS, DEMO, or PGN <number> [src=N] <hex>");
    }
    return true;
}
