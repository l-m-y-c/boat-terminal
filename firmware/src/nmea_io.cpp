#include "nmea_io.h"

#include "n2k.h"
#include "nmea.h"

#include <Arduino.h>

static uint32_t g_last_rx_ms = 0;
/* A full 223-byte PGN as contiguous hex, plus "NMEA PGN <n> src=N ". */
static char g_cmd[512];
static size_t g_cmd_len = 0;
static bool g_nmea_line = false;
static bool g_cmd_overflow = false;

static void command_log(const char *msg)
{
    Serial0.println(msg);
    if (!n2k_using_uart()) {
        Serial.println(msg);
    }
}

/* Commands share the console with pasted sentences.
 * The NMEA assembler ignores anything that does not start with '$' or '!'.
 * A line that does not fit is dropped whole, not run as a clipped command. */
/* Pasted sentences are a bench tool. On a live bus they would sit beside
 * real instrument data, so they are dropped there. */
static void feed_byte(uint8_t byte)
{
    if (n2k_source() == DataSource::Simulator) {
        nmea_feed_bytes(&byte, 1);
    }
}

static void take_byte(int b)
{
    if (b < 0) {
        return;
    }
    const char c = static_cast<char>(b);
    if (c == '\r' || c == '\n') {
        if (!g_nmea_line && g_cmd_overflow) {
            command_log("NMEA command too long");
        } else if (!g_nmea_line && g_cmd_len > 0) {
            while (g_cmd_len > 0 &&
                   (g_cmd[g_cmd_len - 1] == ' ' || g_cmd[g_cmd_len - 1] == '\t')) {
                --g_cmd_len;
            }
            if (g_cmd_len > 0) {
                g_cmd[g_cmd_len] = '\0';
                n2k_handle_command(g_cmd);
            }
        }
        const bool was_nmea = g_nmea_line;
        g_cmd_len = 0;
        g_nmea_line = false;
        g_cmd_overflow = false;
        if (was_nmea) {
            feed_byte(static_cast<uint8_t>(b));
        }
        return;
    }
    if (g_cmd_len == 0 && !g_nmea_line && !g_cmd_overflow && (c == '$' || c == '!')) {
        g_nmea_line = true;
    }
    if (g_nmea_line) {
        feed_byte(static_cast<uint8_t>(b));
        g_last_rx_ms = millis();
        return;
    }
    if (g_cmd_overflow) {
        return;
    }
    if (g_cmd_len + 1 < sizeof(g_cmd)) {
        g_cmd[g_cmd_len++] = c;
    } else {
        g_cmd_overflow = true;
        g_cmd_len = 0;
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
    /* UART Type-C is the CH343 on UART0: TX GPIO43, RX GPIO44.
     * begin(baud, config, rxPin, txPin). This socket does not reset the chip.
     * The socket marked USB is native CDC; opening it from the Mac resets
     * the chip into the ROM download bootloader. */
    Serial0.begin(115200, SERIAL_8N1, 44, 43);
    command_log("UART console: NMEA SIM | NMEA N2K | NMEA STATS | NMEA DEMO | NMEA PGN <num> [src=N] <hex>");
    /*
     * TODO(slice-d): Wire Waveshare RS485 UART + DE/RE once pins are confirmed
     * against the wiki / esp_panel_board_custom_conf.h. Do not drive a TX pin
     * until inject policy (Slice C) and listen-only Slice D are gated.
     * Until then the only boat-facing source is the NMEA 2000 header.
     */
}

void nmea_io_poll(void)
{
    /* Both consoles. UART0 stays up when CAN later takes GPIO19/20.
     * Pasted sentences still feed the assembler. */
    poll_stream(Serial0);
    poll_stream(Serial);
}

bool nmea_io_quiet(void)
{
    if (g_last_rx_ms == 0) {
        return true;
    }
    return (millis() - g_last_rx_ms) > 3000;
}
