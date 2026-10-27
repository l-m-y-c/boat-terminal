#include "n2k.h"

#include "n2k_diag.h"
#include "n2k_node.h"
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

/* Waveshare ESP32-S3-Touch-LCD-7 CAN header. TJA1051, not isolated (see
 * docs/05 on what that means for compliance). GPIO19/20 are also USB
 * D-/D+. CH422G EXIO5 selects which one is wired: high = CAN, low = USB.
 * The 120 Ω jumper on the board must be open. */
static constexpr gpio_num_t kCanTx = GPIO_NUM_20;
static constexpr gpio_num_t kCanRx = GPIO_NUM_19;

static constexpr uint32_t kQuietMs = 3000;
static constexpr uint32_t kRetryMs = 5000;

/* 250 kbit/s with the J1939 / ISO 11783 bit timing NMEA 2000 uses: 16 time
 * quanta, sample point 87.5 % (1 + 13 before it, 2 after), SJW 1. The
 * IDF default for 250 kbit/s samples at 75 %. */
static const twai_timing_config_t kTiming = {
    .clk_src = TWAI_CLK_SRC_DEFAULT,
    .quanta_resolution_hz = 4000000,
    .brp = 0,
    .tseg_1 = 13,
    .tseg_2 = 2,
    .sjw = 1,
    .triple_sampling = false,
};

/* Receive and node path, sized so nothing is lost while loop() is busy
 * drawing or decoding a photo:
 *  - the TWAI ISR fills the driver queue in internal RAM;
 *  - the receive task moves each frame into the frame queue as it arrives;
 *  - the node task runs the NMEA2000 library: reassembly and the network
 *    management the standard requires (address claim, requests, heartbeat)
 *    on time whatever the screen is doing;
 *  - each reassembled message goes to loop() through the message queue,
 *    and loop() converts it to NMEA 0183.
 * 2048 frames is over a second of a fully loaded 250 kbit/s bus. */
static constexpr uint32_t kDriverQueueFrames = 256;
static constexpr uint32_t kDriverTxFrames = 32;
static constexpr uint32_t kFrameQueueFrames = 2048;
static constexpr uint32_t kMessageQueueMsgs = 128;
/* Fast-packet messages being reassembled at once, across all sources. */
static constexpr uint16_t kReassemblySlots = 32;
/* Frames the library holds when the driver's transmit queue is full. A
 * 126996 product information reply is 20 frames. */
static constexpr uint16_t kSendBufferFrames = 64;
/* The standard's address-claim wait. */
static constexpr uint32_t kClaimMs = 250;

static constexpr UBaseType_t kRxTaskPriority = 10;
static constexpr UBaseType_t kNodeTaskPriority = 9;
static constexpr UBaseType_t kSimTaskPriority = 4;
static constexpr BaseType_t kTaskCore = 1;

static constexpr const char *kPrefsSpace = "lmyc";
static constexpr const char *kPrefsSource = "nmea_src";

/* The library's frames come from the frame queue and go to the TWAI
 * driver. Everything here runs in the node task. */
class N2kStack : public tNMEA2000 {
public:
    bool fast_pgn(unsigned long pgn) { return IsFastPacketPGN(pgn); }
    bool ready_to_send(void) const { return IsReadyToSend(); }

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
static uint32_t g_restart_at = 0;

static QueueHandle_t g_queue = nullptr;
static QueueHandle_t g_msgs = nullptr;
static TaskHandle_t g_rx_task = nullptr;
static TaskHandle_t g_node_task = nullptr;
static TaskHandle_t g_sim_task = nullptr;
static SemaphoreHandle_t g_released = nullptr;
/* n2k_diag tables: written by the node task, read by loop(). */
static SemaphoreHandle_t g_diag_tables = nullptr;

/* Written by one task, read by others. Aligned 32-bit loads and stores
 * are atomic on the ESP32-S3. */
static volatile uint32_t g_frames = 0;
static volatile uint32_t g_last_frame_ms = 0;
static volatile uint32_t g_queue_peak = 0;
static volatile uint32_t g_lost_queue = 0;
static volatile uint32_t g_ignored = 0;
static volatile uint32_t g_lost_driver = 0;
static volatile uint32_t g_lost_controller = 0;
static volatile uint32_t g_bus_errors = 0;
static volatile uint32_t g_rec = 0;
static volatile uint32_t g_tx_frames = 0;
static volatile uint32_t g_tx_failed = 0;
static volatile uint32_t g_bus_off_count = 0;
static volatile uint32_t g_lost_messages = 0;
static volatile uint32_t g_parse_peak_us = 0;
static volatile bool g_driver_on = false;
static volatile bool g_bus_off = false;
static volatile bool g_failed = false;
static volatile bool g_release_req = false;
static volatile bool g_sim_paused = false;
/* Set by the receive task after bus-off recovery: the node task then
 * announces its address again. */
static volatile bool g_reclaim = false;
/* The library reads and drops every frame during its 200 ms start-up wait,
 * so the producers start only once it is open. */
static volatile bool g_stack_open = false;
static volatile NodeState g_node = NodeState::Off;
static volatile uint8_t g_address = N2kNullCanBusAddress;
static volatile uint32_t g_repeats = 0;

/* loop() only. */
static uint32_t g_messages = 0;
static uint32_t g_rate_ms = 0;
static uint32_t g_rate_frames = 0;
static uint32_t g_rate_messages = 0;
static uint32_t g_fps = 0;
static uint32_t g_fps_peak = 0;
static uint32_t g_mps = 0;
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

/* Producer task only. A CAN transmitter that gets no ACK sends the same
 * frame again straight away, ~0.6 ms later at 250 kbit/s. No healthy
 * device repeats a frame that fast: fast-packet frames differ in their
 * counter, and repeated values come 100 ms or more apart. With this node
 * acknowledging, repeats mean a wiring or termination fault. */
static constexpr int64_t kRepeatUs = 2000;
static N2kFrame g_prev = {};

static void note_repeat(const N2kFrame &f)
{
    if (f.id == g_prev.id && f.len == g_prev.len && memcmp(f.data, g_prev.data, f.len) == 0 &&
        f.us - g_prev.us < kRepeatUs) {
        g_repeats = g_repeats + 1;
    }
    g_prev = f;
}

static bool push_frame(const N2kFrame &in, void * /*ctx*/)
{
    N2kFrame frame = in;
    frame.us = esp_timer_get_time();
    note_repeat(frame);
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
    if (g_node_task != nullptr) {
        xTaskNotifyGive(g_node_task);
    }
    return true;
}

static QueueHandle_t make_queue(uint32_t items, size_t item_size)
{
    /* Keep internal RAM for Wi-Fi, BLE and the TWAI driver. */
    const size_t bytes = items * item_size;
    auto *storage = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (storage == nullptr) {
        storage = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    auto *control = static_cast<StaticQueue_t *>(heap_caps_malloc(sizeof(StaticQueue_t), MALLOC_CAP_INTERNAL));
    if (storage == nullptr || control == nullptr) {
        return nullptr;
    }
    return xQueueCreateStatic(items, item_size, storage, control);
}

/* ---- raw capture ------------------------------------------------------ */

/* Every frame loop() decodes, kept for the phone's "Record bus". At 500
 * frames/s 4096 frames is 8 s, so a phone polling twice a second never
 * falls behind. Written by loop(), read by the web server task. */
static constexpr uint32_t kCaptureFrames = 4096;

struct CaptureFrame {
    uint32_t seq;
    uint32_t id;
    int64_t us;
    uint8_t len;
    uint8_t data[8];
};

static CaptureFrame *g_capture = nullptr;
static uint32_t g_capture_seq = 0; /* last written, 0 = none */
static portMUX_TYPE g_capture_lock = portMUX_INITIALIZER_UNLOCKED;

static void capture_init(void)
{
    const size_t bytes = kCaptureFrames * sizeof(CaptureFrame);
    g_capture = static_cast<CaptureFrame *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void capture_frame(const N2kFrame &f)
{
    if (g_capture == nullptr) {
        return;
    }
    portENTER_CRITICAL(&g_capture_lock);
    const uint32_t seq = g_capture_seq + 1;
    CaptureFrame &c = g_capture[seq % kCaptureFrames];
    c.seq = seq;
    c.id = f.id;
    c.us = f.us;
    c.len = f.len;
    memcpy(c.data, f.data, sizeof(c.data));
    g_capture_seq = seq;
    portEXIT_CRITICAL(&g_capture_lock);
}

size_t n2k_capture_read(uint32_t after, char *out, size_t cap, uint32_t *next, uint32_t *lost)
{
    if (next != nullptr) {
        *next = after;
    }
    if (lost != nullptr) {
        *lost = 0;
    }
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (g_capture == nullptr) {
        return 0;
    }
    portENTER_CRITICAL(&g_capture_lock);
    const uint32_t head = g_capture_seq;
    portEXIT_CRITICAL(&g_capture_lock);
    if (after == 0 || after > head) {
        if (next != nullptr) {
            *next = head;
        }
        return 0;
    }
    const uint32_t oldest = head >= kCaptureFrames ? head - kCaptureFrames + 1 : 1;
    uint32_t seq = after + 1;
    if (seq < oldest) {
        if (lost != nullptr) {
            *lost = oldest - seq;
        }
        seq = oldest;
    }
    /* Longest line: "(4294967.295000) can0 1FFFFFFF#0011223344556677\n". */
    constexpr size_t kLineMax = 52;
    constexpr uint32_t kChunk = 32;
    size_t used = 0;
    uint32_t last = seq - 1;
    while (seq <= head && used + kLineMax * kChunk < cap) {
        CaptureFrame chunk[kChunk];
        uint32_t n = 0;
        portENTER_CRITICAL(&g_capture_lock);
        for (; n < kChunk && seq + n <= head; ++n) {
            chunk[n] = g_capture[(seq + n) % kCaptureFrames];
        }
        portEXIT_CRITICAL(&g_capture_lock);
        for (uint32_t i = 0; i < n; ++i) {
            const CaptureFrame &c = chunk[i];
            if (c.seq != seq + i) {
                /* Overwritten between the head read and this copy. */
                if (lost != nullptr) {
                    ++*lost;
                }
                continue;
            }
            char hex[17];
            for (uint8_t b = 0; b < c.len && b < 8; ++b) {
                snprintf(hex + b * 2, 3, "%02X", c.data[b]);
            }
            hex[c.len * 2] = '\0';
            const int w = snprintf(out + used, cap - used, "(%lu.%06lu) can0 %08lX#%s\n",
                                   static_cast<unsigned long>(c.us / 1000000),
                                   static_cast<unsigned long>(c.us % 1000000), static_cast<unsigned long>(c.id),
                                   hex);
            if (w > 0) {
                used += static_cast<size_t>(w);
            }
        }
        seq += n;
        last = seq - 1;
    }
    if (next != nullptr) {
        *next = last;
    }
    return used;
}

/* ---- diagnostics JSON -------------------------------------------------- */

static constexpr size_t kDiagJsonMax = 16384;
static char *g_diag_json = nullptr;
static size_t g_diag_len = 0;
static SemaphoreHandle_t g_diag_lock = nullptr;

size_t n2k_diag_copy(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (g_diag_json == nullptr || g_diag_lock == nullptr) {
        return 0;
    }
    xSemaphoreTake(g_diag_lock, portMAX_DELAY);
    const size_t n = g_diag_len < cap ? g_diag_len : cap - 1;
    memcpy(out, g_diag_json, n);
    xSemaphoreGive(g_diag_lock);
    out[n] = '\0';
    return n;
}


/* ---- library frame I/O (node task) ------------------------------------- */

bool N2kStack::CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf,
                            bool /*wait_sent*/)
{
    if (!g_driver_on || g_bus_off || buf == nullptr) {
        return false;
    }
    twai_message_t m = {};
    m.extd = 1;
    m.identifier = id & 0x1fffffffu;
    m.data_length_code = len > 8 ? 8 : len;
    memcpy(m.data, buf, m.data_length_code);
    /* Never block the node task. The driver queue is FIFO, so frames keep
     * their order; when it is full the library keeps the frame and resends. */
    if (twai_transmit(&m, 0) != ESP_OK) {
        g_tx_failed = g_tx_failed + 1;
        return false;
    }
    g_tx_frames = g_tx_frames + 1;
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
    capture_frame(frame);
    const bool fast = fast_pgn(n2k_diag_pgn(frame.id));
    xSemaphoreTake(g_diag_tables, portMAX_DELAY);
    n2k_diag_frame(frame.id, frame.len, frame.data, fast, millis());
    xSemaphoreGive(g_diag_tables);
    return true;
}

/* Library message handler, node task: diagnostics here, conversion in loop(). */
static void on_n2k_msg(const tN2kMsg &msg)
{
    xSemaphoreTake(g_diag_tables, portMAX_DELAY);
    n2k_diag_message(msg, millis());
    xSemaphoreGive(g_diag_tables);
    if (g_msgs == nullptr || xQueueSend(g_msgs, &msg, 0) != pdTRUE) {
        g_lost_messages = g_lost_messages + 1;
    }
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

/* Normal CAN mode: acknowledges frames, signals errors and keeps the
 * standard fault confinement, like every device on the backbone. */
static bool install_driver(void)
{
    twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(kCanTx, kCanRx, TWAI_MODE_NORMAL);
    general.tx_queue_len = kDriverTxFrames;
    general.rx_queue_len = kDriverQueueFrames;
    general.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED;
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&general, &kTiming, &filter) != ESP_OK) {
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
    g_driver_on = false;
    twai_stop();
    twai_status_info_t st = {};
    const uint32_t stranded = twai_get_status_info(&st) == ESP_OK ? st.msgs_to_rx : 0;
    read_driver_counters(true);
    g_base_missed += stranded;
    g_lost_driver = g_base_missed;
    twai_driver_uninstall();
    hold_tx_recessive();
}

static void rx_task(void * /*arg*/)
{
    while (!g_stack_open) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint32_t retry_at = millis();
    uint32_t status_at = 0;
    for (;;) {
        if (g_release_req) {
            uninstall_driver();
            xSemaphoreGive(g_released);
            vTaskSuspend(nullptr);
        }

        if (!g_driver_on) {
            if (static_cast<int32_t>(millis() - retry_at) >= 0) {
                if (install_driver()) {
                    g_failed = false;
                    n2k_log("N2K: NMEA 2000 node, 250 kbit/s (sample point 87.5 %), GPIO20 TX / GPIO19 RX");
                } else {
                    g_failed = true;
                    retry_at = millis() + kRetryMs;
                    n2k_log("N2K: TWAI would not start, retrying");
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        twai_message_t message = {};
        if (twai_receive(&message, pdMS_TO_TICKS(20)) == ESP_OK) {
            if (message.extd && !message.rtr) {
                N2kFrame frame = {};
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
        if (twai_read_alerts(&alerts, 0) != ESP_OK) {
            continue;
        }
        if (alerts & TWAI_ALERT_BUS_OFF) {
            /* Standard CAN recovery: wait out 128 x 11 recessive bits, then
             * rejoin. Our own transmit errors got us here, so nothing more
             * is sent until recovery completes. */
            g_bus_off = true;
            g_bus_off_count = g_bus_off_count + 1;
            n2k_log("N2K: bus-off, standard recovery");
            twai_initiate_recovery();
        }
        if (alerts & TWAI_ALERT_BUS_RECOVERED) {
            if (twai_start() == ESP_OK) {
                g_bus_off = false;
                g_reclaim = true;
                n2k_log("N2K: recovered from bus-off, announcing the address again");
            }
        }
    }
}

/* ---- node task ----------------------------------------------------------- */

static void update_node_state(void)
{
    if (g_source != DataSource::Nmea2000) {
        g_node = NodeState::Off;
        g_address = N2kNullCanBusAddress;
        return;
    }
    const uint8_t a = g_stack.GetN2kSource();
    g_address = a;
    if (a >= N2kNullCanBusAddress) {
        g_node = NodeState::NoAddress;
    } else {
        g_node = g_stack.ready_to_send() ? NodeState::Claimed : NodeState::Claiming;
    }
}

static void node_task(void * /*arg*/)
{
    /* The library waits 200 ms after Open() before it claims an address. */
    g_stack.Open();
    while (!g_stack.IsOpen()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        g_stack.Open();
    }
    g_stack_open = true;
    uint8_t logged = 0xff;
    for (;;) {
        /* Woken by every frame, and at least every 10 ms for the library's
         * timers: claim wait, resends, heartbeat. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
        const int64_t start = esp_timer_get_time();
        do {
            g_stack.ParseMessages();
        } while (uxQueueMessagesWaiting(g_queue) > 0);
        const uint32_t spent = static_cast<uint32_t>(esp_timer_get_time() - start);
        if (spent > g_parse_peak_us) {
            g_parse_peak_us = spent;
        }
        if (g_reclaim) {
            g_reclaim = false;
            g_stack.SendIsoAddressClaim();
        }
        n2k_node_save_changes(g_stack);
        update_node_state();
        if (g_source == DataSource::Nmea2000 && g_node == NodeState::Claimed && g_address != logged) {
            logged = g_address;
            char line[64];
            snprintf(line, sizeof(line), "N2K: claimed address %u", static_cast<unsigned>(g_address));
            n2k_log(line);
        } else if (g_node == NodeState::NoAddress && logged != N2kNullCanBusAddress) {
            logged = N2kNullCanBusAddress;
            n2k_log("N2K: no free address on the bus (every address is held by a higher-priority NAME)");
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
    while (!g_stack_open) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
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

static BusState bus_state(void);

/* Per-second deltas the hint is chosen from. loop() only. */
static uint32_t g_rate_repeats = 0;
static uint32_t g_rate_errors = 0;
static uint32_t g_rate_lost = 0;
static uint32_t g_rate_fast_starts = 0;
static uint32_t g_rate_fast_incomplete = 0;
static uint32_t g_rate_bad = 0;
static uint32_t g_repeats_ps = 0;
static uint32_t g_errors_ps = 0;
static N2kHint g_hint = N2kHint::None;
static uint32_t g_hint_ms = 0;
/* A hint stays up this long after its cause last showed, so a problem
 * that comes and goes is still readable. */
static constexpr uint32_t kHintHoldMs = 10000;

static bool two_sources(void)
{
    N2kSourceLock locks[16];
    const size_t n = n2k_conv_sources(locks, 16);
    for (size_t i = 0; i < n; ++i) {
        if (locks[i].other >= 0 && locks[i].other_age_ms <= kHintHoldMs) {
            return true;
        }
    }
    return false;
}

static void pick_hint(uint32_t now, uint32_t span)
{
    auto per_s = [span](uint32_t d) { return static_cast<uint32_t>((static_cast<uint64_t>(d) * 1000u) / span); };
    const uint32_t repeats = g_repeats;
    const uint32_t errors = g_bus_errors;
    const uint32_t lost = g_lost_queue + g_lost_driver + g_lost_controller + g_lost_messages;
    xSemaphoreTake(g_diag_tables, portMAX_DELAY);
    const uint32_t starts = n2k_diag_fast_starts();
    const uint32_t incomplete = n2k_diag_fast_incomplete();
    xSemaphoreGive(g_diag_tables);
    const uint32_t bad = n2k_conv_bad_total();

    N2kHintInput in = {};
    in.source = g_source;
    in.state = bus_state();
    in.node = g_node;
    in.frames_per_s = g_fps;
    in.repeats_per_s = per_s(repeats - g_rate_repeats);
    in.bus_errors_per_s = per_s(errors - g_rate_errors);
    in.lost_per_s = per_s(lost - g_rate_lost);
    in.fast_starts_per_s = per_s(starts - g_rate_fast_starts);
    in.fast_incomplete_per_s = incomplete > g_rate_fast_incomplete ? per_s(incomplete - g_rate_fast_incomplete) : 0;
    in.bad_per_s = per_s(bad - g_rate_bad);
    in.two_sources = two_sources();
    g_repeats_ps = in.repeats_per_s;
    g_errors_ps = in.bus_errors_per_s;

    g_rate_repeats = repeats;
    g_rate_errors = errors;
    g_rate_lost = lost;
    g_rate_fast_starts = starts;
    g_rate_fast_incomplete = incomplete;
    g_rate_bad = bad;

    const N2kHint now_hint = n2k_hint_pick(in);
    if (now_hint != N2kHint::None &&
        (g_hint == N2kHint::None || static_cast<uint8_t>(now_hint) <= static_cast<uint8_t>(g_hint))) {
        if (now_hint != g_hint) {
            char line[200];
            snprintf(line, sizeof(line), "N2K hint: %s. %s", n2k_hint_code(now_hint),
                     n2k_hint_text(now_hint));
            n2k_log(line);
        }
        g_hint = now_hint;
        g_hint_ms = now;
    } else if (now_hint == g_hint && now_hint != N2kHint::None) {
        g_hint_ms = now;
    } else if (g_hint != N2kHint::None && now - g_hint_ms > kHintHoldMs) {
        g_hint = now_hint;
        g_hint_ms = now;
    }
}

static void rebuild_diag(uint32_t now)
{
    if (g_diag_json == nullptr || g_diag_lock == nullptr) {
        return;
    }
    N2kStats s;
    n2k_stats(&s);
    /* Build outside the lock into the back half, then swap in under it. */
    char *scratch = g_diag_json + kDiagJsonMax;
    xSemaphoreTake(g_diag_tables, portMAX_DELAY);
    const size_t n = n2k_diag_json(scratch, kDiagJsonMax, s, now);
    xSemaphoreGive(g_diag_tables);
    if (n == 0) {
        return;
    }
    xSemaphoreTake(g_diag_lock, portMAX_DELAY);
    memcpy(g_diag_json, scratch, n + 1);
    g_diag_len = n;
    xSemaphoreGive(g_diag_lock);
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
    pick_hint(now, span);
    xSemaphoreTake(g_diag_tables, portMAX_DELAY);
    n2k_diag_tick(now);
    xSemaphoreGive(g_diag_tables);
    rebuild_diag(now);
}

static void restart_now(void)
{
    g_restart_at = 0;
    if (g_node_task != nullptr) {
        vTaskSuspend(g_node_task);
    }
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
    g_queue = make_queue(kFrameQueueFrames, sizeof(N2kFrame));
    g_msgs = make_queue(kMessageQueueMsgs, sizeof(tN2kMsg));
    g_diag_tables = xSemaphoreCreateMutex();
    if (g_queue == nullptr || g_msgs == nullptr || g_diag_tables == nullptr) {
        n2k_log("N2K: no memory for the frame and message queues");
        g_queue = nullptr;
        return;
    }
    capture_init();
    n2k_diag_reset();
    g_diag_lock = xSemaphoreCreateMutex();
    g_diag_json = static_cast<char *>(heap_caps_malloc(kDiagJsonMax * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (g_capture == nullptr || g_diag_json == nullptr) {
        n2k_log("N2K: no PSRAM for capture or diagnostics; both are off");
    }

    const bool bus = g_source == DataSource::Nmea2000;
    /* A full node on the backbone. On the simulator the library listens to
     * a virtual bus and sends nothing anywhere. */
    n2k_node_configure(g_stack, bus);
    g_stack.EnableForward(false);
    g_stack.SetN2kCANMsgBufSize(kReassemblySlots);
    g_stack.SetN2kCANSendFrameBufSize(kSendBufferFrames);
    g_stack.SetMsgHandler(on_n2k_msg);

    if (!bus) {
        /* A restart from NMEA 2000 can leave the mux on CAN. */
        if (g_select != nullptr) {
            g_select(false);
        }
        n2k_sim_reset();
        xTaskCreatePinnedToCore(sim_task, "n2k_sim", 4096, nullptr, kSimTaskPriority, &g_sim_task,
                                kTaskCore);
    } else if (!open_can()) {
        g_failed = true;
        n2k_log("N2K: could not start the receive task");
        return;
    }
    if (xTaskCreatePinnedToCore(node_task, "n2k_node", 6144, nullptr, kNodeTaskPriority, &g_node_task,
                                kTaskCore) != pdPASS) {
        g_failed = true;
        n2k_log("N2K: could not start the node task");
        return;
    }
    n2k_log(bus ? "NMEA source: NMEA 2000 node. Serial log is on the UART Type-C socket."
                : "NMEA source: simulator (virtual NMEA 2000 bus, CAN closed)");
}

void n2k_poll(void)
{
    if (g_restart_at != 0 && static_cast<int32_t>(millis() - g_restart_at) >= 0) {
        restart_now();
    }
    if (g_queue == nullptr) {
        return;
    }
    /* Reassembled messages from the node task, converted here because the
     * sentence sink, gateway and BLE belong to loop(). */
    tN2kMsg msg;
    while (xQueueReceive(g_msgs, &msg, 0) == pdTRUE) {
        note_message(msg);
        n2k_to_0183_handle(msg);
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
    if (g_driver_on && g_bus_off) {
        return BusState::BusOff;
    }
    if (g_driver_on) {
        if (g_frames == 0) {
            return BusState::Waiting;
        }
        return n2k_quiet() ? BusState::Quiet : BusState::Live;
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
    out->node = g_node;
    out->address = g_address;
    out->tx_frames = g_tx_frames;
    out->tx_failed = g_tx_failed;
    out->bus_off_count = g_bus_off_count;
    out->lost_messages = g_lost_messages;
    out->unique_number = n2k_node_unique_number();
    out->manufacturer_code = n2k_node_manufacturer_code();
    out->repeats = g_repeats;
    out->repeats_per_s = g_repeats_ps;
    out->bus_errors_per_s = g_errors_ps;
    if (g_diag_tables != nullptr) {
        xSemaphoreTake(g_diag_tables, portMAX_DELAY);
        out->fast_incomplete = n2k_diag_fast_incomplete();
        xSemaphoreGive(g_diag_tables);
    }
    out->conv_bad = n2k_conv_bad_total();
    out->hint = g_hint;
}

void n2k_set_sim_gnss(bool on) { n2k_sim_set_gnss(on); }

const char *n2k_node_state_name(NodeState state)
{
    switch (state) {
    case NodeState::Off:
        return "Not on a bus";
    case NodeState::Claiming:
        return "Claiming an address";
    case NodeState::Claimed:
        return "NMEA 2000 node";
    case NodeState::NoAddress:
        return "No free address";
    }
    return "?";
}

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
    const uint32_t lost = g_lost_queue + g_lost_driver + g_lost_controller + g_lost_messages;
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

static void log_diag(void)
{
    N2kStats s;
    n2k_stats(&s);
    char line[320];
    snprintf(line, sizeof(line),
             "NMEA %s, node: %s, address %u, sent %lu frames (%lu refused), bus-off %lu, repeats %lu (%lu/s), "
             "fast-packet incomplete %lu, bad values %lu. %s",
             n2k_hint_code(s.hint), n2k_node_state_name(s.node), static_cast<unsigned>(s.address),
             static_cast<unsigned long>(s.tx_frames), static_cast<unsigned long>(s.tx_failed),
             static_cast<unsigned long>(s.bus_off_count), static_cast<unsigned long>(s.repeats),
             static_cast<unsigned long>(s.repeats_per_s), static_cast<unsigned long>(s.fast_incomplete),
             static_cast<unsigned long>(s.conv_bad), n2k_hint_text(s.hint));
    n2k_log(line);
    char *json = static_cast<char *>(heap_caps_malloc(kDiagJsonMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (json != nullptr && n2k_diag_copy(json, kDiagJsonMax) > 0) {
        n2k_log(json);
    }
    heap_caps_free(json);
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
    if (g_msgs != nullptr) {
        xQueueReset(g_msgs);
    }
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
    } else if (strcasecmp(arg, "DIAG") == 0) {
        log_diag();
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
        n2k_log("NMEA command: SIM, N2K, STATS, DIAG, DEMO, or PGN <number> [src=N] <hex>");
    }
    return true;
}
