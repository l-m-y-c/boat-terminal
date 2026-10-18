#include "nmea_io.h"

#include "n2k.h"
#include "nmea.h"

#include <Arduino.h>

static uint32_t g_last_rx_ms = 0;
static char g_cmd[40];
static size_t g_cmd_len = 0;
static bool g_nmea_line = false;

/* Commands ("NMEA SIM|N2K|AUTO") share the console with pasted sentences.
 * The NMEA assembler ignores anything that does not start with '$' or '!'. */
static void take_byte(int b)
{
    if (b < 0) {
        return;
    }
    const char c = static_cast<char>(b);
    if (c == '\r' || c == '\n') {
        if (!g_nmea_line && g_cmd_len > 0) {
            g_cmd[g_cmd_len] = '\0';
            n2k_handle_command(g_cmd);
        }
        const bool was_nmea = g_nmea_line;
        g_cmd_len = 0;
        g_nmea_line = false;
        if (was_nmea) {
            const uint8_t byte = static_cast<uint8_t>(b);
            nmea_feed_bytes(&byte, 1);
        }
        return;
    }
    if (g_cmd_len == 0 && !g_nmea_line && (c == '$' || c == '!')) {
        g_nmea_line = true;
    }
    if (g_nmea_line) {
        const uint8_t byte = static_cast<uint8_t>(b);
        nmea_feed_bytes(&byte, 1);
        g_last_rx_ms = millis();
        return;
    }
    if (g_cmd_len + 1 < sizeof(g_cmd)) {
        g_cmd[g_cmd_len++] = c;
    }
}

static void poll_stream(Stream &in)
{
    while (in.available() > 0) {
        take_byte(in.read());
    }
}

void nmea_io_init(void)
{
    g_last_rx_ms = 0;
    /*
     * TODO(slice-d): Wire Waveshare RS485 UART + DE/RE once pins are confirmed
     * against the wiki / esp_panel_board_custom_conf.h. Do not drive a TX pin
     * until inject policy (Slice C) and listen-only Slice D are gated.
     * Until then, Slice A uses nmea_sim only — no boat-facing talker.
     */
}

void nmea_io_poll(void)
{
    /* USB-CDC while the CAN mux is on USB. UART0 (CH343) once N2K owns
     * GPIO19/20. Pasted sentences still feed the assembler. */
    if (n2k_using_uart()) {
        poll_stream(Serial0);
    }
    poll_stream(Serial);
}

bool nmea_io_quiet(void)
{
    if (g_last_rx_ms == 0) {
        return true;
    }
    return (millis() - g_last_rx_ms) > 3000;
}
