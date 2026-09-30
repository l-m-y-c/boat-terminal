#include "nmea_io.h"

#include "nmea.h"

#include <Arduino.h>

static uint32_t g_last_rx_ms = 0;

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
    /* Optional: USB-CDC typed lines for bench inject without RS485.
     * Does not replace the simulator; just lets a human paste a sentence. */
    while (Serial.available() > 0) {
        const int b = Serial.read();
        if (b < 0) {
            break;
        }
        const uint8_t byte = static_cast<uint8_t>(b);
        nmea_feed_bytes(&byte, 1);
        g_last_rx_ms = millis();
    }
}

bool nmea_io_quiet(void)
{
    if (g_last_rx_ms == 0) {
        return true;
    }
    return (millis() - g_last_rx_ms) > 3000;
}
