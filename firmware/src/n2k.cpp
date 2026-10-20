#include "n2k.h"

#include "n2k_to_0183.h"
#include "nmea_sim.h"

#include <NMEA2000.h>
#include <N2kMessages.h>

#include <Arduino.h>
#include <cstring>
#include <strings.h>

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
 * Claim (PGN 60928). Claiming would be a transmission, which this slice
 * does not do. 22 is just the address the library keeps internally. */
static constexpr uint8_t kLocalAddress = 22;

static constexpr uint32_t kQuietMs = 3000;
static constexpr uint32_t kRetryMs = 5000;

class N2kTwai : public tNMEA2000 {
public:
    bool hardware_up(void) const { return driver_on_; }
    void release(bool restore_usb);
    /* After a bus-off the stack stays open; only the controller is restarted. */
    bool reopen(void) { return CANOpen(); }

protected:
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf,
                      bool wait_sent = true) override;
    bool CANOpen(void) override;
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override;

private:
    bool driver_on_ = false;
    bool install_listen(void);
};

static N2kTwai g_n2k;
static N2kCanSelectFn g_select = nullptr;
static int g_source = NMEA_SOURCE_SIM;
static bool g_stack_open = false;
static bool g_uart = false;
static bool g_play = false;
static bool g_fault = false;
static uint32_t g_frames = 0;
static uint32_t g_last_frame_ms = 0;
static uint32_t g_retry_ms = 0;
static bool g_tx_attempted = false;

static void n2k_log(const char *msg)
{
    /* UART0 (CH343) is up in every mode, so a Mac on the UART socket
     * sees the ack. USB Serial is only alive while the mux is not on CAN. */
    Serial0.println(msg);
    if (!g_uart) {
        Serial.println(msg);
    }
}

bool N2kTwai::CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf,
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

bool N2kTwai::install_listen(void)
{
    twai_general_config_t general =
        TWAI_GENERAL_CONFIG_DEFAULT(kCanTx, kCanRx, TWAI_MODE_LISTEN_ONLY);
    general.tx_queue_len = 1;
    general.rx_queue_len = 64;
    general.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_RX_QUEUE_FULL;
    const twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&general, &timing, &filter) != ESP_OK) {
        return false;
    }
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }
    driver_on_ = true;
    return true;
}

bool N2kTwai::CANOpen(void)
{
    if (driver_on_) {
        return true;
    }

    /* USB Serial/JTAG owns GPIO19/20 until it is stopped. */
    Serial.flush();
    Serial.end();
    g_uart = true;
    if (g_select != nullptr) {
        g_select(true);
    }
    delay(20);
    gpio_reset_pin(kCanTx);
    gpio_reset_pin(kCanRx);

    if (!install_listen()) {
        n2k_log("N2K: TWAI listen-only failed");
        release(true);
        return false;
    }

    Serial0.begin(115200);
    n2k_log("N2K: listen-only 250 kbit, GPIO20 TX / GPIO19 RX, no address claim");
    return true;
}

bool N2kTwai::CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf)
{
    if (!driver_on_ || buf == nullptr) {
        return false;
    }
    twai_message_t message = {};
    if (twai_receive(&message, 0) != ESP_OK) {
        return false;
    }
    if (!message.extd || message.rtr) {
        return false;
    }
    id = message.identifier & 0x1fffffffu;
    len = message.data_length_code;
    if (len > 8) {
        len = 8;
    }
    memcpy(buf, message.data, len);
    ++g_frames;
    g_last_frame_ms = millis();
    g_fault = false;
    return true;
}

void N2kTwai::release(bool restore_usb)
{
    if (driver_on_) {
        twai_stop();
        twai_driver_uninstall();
        driver_on_ = false;
    }
    /* TXD high (recessive). A floating TXD on the TJA1051 can chatter. */
    gpio_reset_pin(kCanTx);
    gpio_set_direction(kCanTx, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kCanTx, GPIO_PULLUP_ONLY);
    gpio_reset_pin(kCanRx);
    gpio_set_direction(kCanRx, GPIO_MODE_INPUT);

    if (restore_usb) {
        gpio_reset_pin(kCanTx);
        gpio_reset_pin(kCanRx);
        if (g_select != nullptr) {
            g_select(false);
        }
        g_uart = false;
        Serial.begin(115200);
        Serial.println("N2K: CAN released, USB serial restored");
    }
}

static void on_n2k_msg(const tN2kMsg &msg)
{
    n2k_to_0183_handle(msg);
}

static bool open_stack(void)
{
    if (g_stack_open && g_n2k.hardware_up()) {
        return true;
    }
    if (!g_stack_open) {
        g_n2k.SetMode(tNMEA2000::N2km_ListenOnly, kLocalAddress);
        g_n2k.EnableForward(false);
        g_n2k.SetN2kCANMsgBufSize(24);
        g_n2k.SetN2kCANReceiveFrameBufSize(64);
        g_n2k.SetMsgHandler(on_n2k_msg);
        g_stack_open = g_n2k.Open();
        if (!g_stack_open) {
            g_fault = true;
            n2k_log("N2K: open failed");
        }
        return g_stack_open;
    }
    /* Stack is up but the controller was stopped after a bus-off. */
    return g_n2k.reopen();
}

static void note_play(void)
{
    g_play = true;
    /* The 1 Hz simulator would overwrite played values after the quiet window. */
    if (g_source == NMEA_SOURCE_SIM) {
        nmea_sim_set_enabled(false);
    }
}

static void apply_source(int source)
{
    if (source != NMEA_SOURCE_N2K && source != NMEA_SOURCE_AUTO) {
        source = NMEA_SOURCE_SIM;
    }
    g_source = source;
    g_play = false;
    g_retry_ms = 0;

    if (source == NMEA_SOURCE_SIM) {
        nmea_sim_set_enabled(true);
        if (g_n2k.hardware_up() || g_uart) {
            g_n2k.release(true);
        }
        n2k_log("NMEA source SIM");
        return;
    }

    nmea_sim_set_enabled(source == NMEA_SOURCE_AUTO);
    if (!open_stack()) {
        if (source == NMEA_SOURCE_AUTO) {
            nmea_sim_set_enabled(true);
        }
        g_retry_ms = millis() + kRetryMs;
    }
}

void n2k_init(N2kCanSelectFn select_can)
{
    g_select = select_can;
    g_source = NMEA_SOURCE;
    g_stack_open = false;
    g_uart = false;
    g_play = false;
    g_fault = false;
    g_frames = 0;
    g_last_frame_ms = 0;
    g_retry_ms = 0;
    g_tx_attempted = false;
    apply_source(NMEA_SOURCE);
}

void n2k_set_source(int source)
{
    apply_source(source);
}

int n2k_source(void) { return g_source; }
bool n2k_using_uart(void) { return g_uart; }
uint32_t n2k_frame_count(void) { return g_frames; }

bool n2k_quiet(void)
{
    if (g_last_frame_ms == 0) {
        return true;
    }
    return (millis() - g_last_frame_ms) > kQuietMs;
}

const char *n2k_fix_tag(void)
{
    if (g_play && g_source == NMEA_SOURCE_SIM) {
        return "PLAY";
    }
    if (g_source != NMEA_SOURCE_SIM && g_n2k.hardware_up() && !n2k_quiet()) {
        return "BOAT";
    }
    if (g_source == NMEA_SOURCE_N2K) {
        return "N2K";
    }
    if (nmea_sim_enabled()) {
        return "SIM";
    }
    return "N2K";
}

void n2k_status_line(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    if (g_play && g_source == NMEA_SOURCE_SIM) {
        snprintf(out, cap, "PLAY - CAN off");
        return;
    }
    const char *mode = "SIM";
    if (g_source == NMEA_SOURCE_N2K) {
        mode = "N2K";
    } else if (g_source == NMEA_SOURCE_AUTO) {
        mode = "AUTO";
    }

    const char *bus = "CAN off";
    if (g_n2k.hardware_up()) {
        if (g_frames == 0) {
            bus = "listening, no frames";
        } else if (n2k_quiet()) {
            bus = "quiet";
        } else {
            bus = "listening";
        }
    } else if (g_fault) {
        bus = "bus-off";
    } else if (g_source != NMEA_SOURCE_SIM) {
        bus = "CAN not open";
    }

    if (g_frames == 0) {
        snprintf(out, cap, "%s - %s", mode, bus);
    } else {
        snprintf(out, cap, "%s - %s - %lu frames", mode, bus,
                 static_cast<unsigned long>(g_frames));
    }
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
        n2k_set_source(NMEA_SOURCE_SIM);
    } else if (strcasecmp(arg, "N2K") == 0) {
        n2k_set_source(NMEA_SOURCE_N2K);
    } else if (strcasecmp(arg, "AUTO") == 0) {
        n2k_set_source(NMEA_SOURCE_AUTO);
    } else if (strcasecmp(arg, "DEMO") == 0) {
        n2k_play_demo();
        note_play();
        n2k_log("NMEA play: depth 42.0 m, COG 90, SOG 7.5 kn, wind 18 kn. "
                "NMEA SIM restores the simulator.");
    } else if (strncasecmp(arg, "PGN", 3) == 0) {
        if (n2k_play_line(arg)) {
            note_play();
            n2k_log("NMEA play: PGN accepted");
        } else {
            n2k_log("NMEA PGN <number> [src=N] <hex>");
        }
    } else {
        n2k_log("NMEA command: SIM, N2K, AUTO, DEMO, or PGN <number> [src=N] <hex>");
    }
    return true;
}

static void note_bus_fault(void)
{
    if (!g_n2k.hardware_up()) {
        return;
    }
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) != ESP_OK || alerts == 0) {
        return;
    }
    if (alerts & TWAI_ALERT_RX_QUEUE_FULL) {
        n2k_log("N2K: RX queue full");
    }
    if (alerts & TWAI_ALERT_BUS_OFF) {
        /* Stop the controller and hold TXD recessive. Do not run bus-off
         * recovery that drives the line. Retry later, still listen-only. */
        g_n2k.release(false);
        g_fault = true;
        g_retry_ms = millis() + kRetryMs;
        n2k_log("N2K: bus-off, dropped off (TXD recessive)");
    }
}

void n2k_poll(void)
{
    /* SIM does not open CAN. Played PGNs still need the 1 Hz VTG refresh. */
    if (g_source == NMEA_SOURCE_SIM) {
        if (g_play) {
            n2k_to_0183_poll();
        }
        return;
    }
    if (!g_n2k.hardware_up()) {
        if (g_retry_ms != 0 && millis() >= g_retry_ms) {
            g_retry_ms = 0;
            if (!open_stack() && g_source == NMEA_SOURCE_AUTO) {
                nmea_sim_set_enabled(true);
                g_retry_ms = millis() + kRetryMs;
            }
        }
        return;
    }

    note_bus_fault();
    if (g_n2k.hardware_up()) {
        g_n2k.ParseMessages();
        n2k_to_0183_poll();
    }
    if (g_source == NMEA_SOURCE_AUTO) {
        nmea_sim_set_enabled(n2k_quiet());
    }
}
