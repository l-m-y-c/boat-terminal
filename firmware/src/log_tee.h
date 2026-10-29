#pragma once

/* The app log, on both sockets. USB Serial (native CDC) as before, plus
 * Serial0, the UART Type-C socket, so that socket shows the full log in
 * Simulator mode too (on NMEA 2000 USB is CAN and only the UART is left).
 *
 * The UART copy never blocks: Serial0 has a 2 KB transmit buffer (set in
 * log_tee_uart_begin) and a line that does not fit in the free space is
 * dropped whole, so a slow or unplugged UART cannot stall the CAN, node,
 * UI or network tasks. Until Serial0 is started the copy is skipped. */

#include <Arduino.h>

#include "diag_log.h"

inline constexpr size_t kLogTeeUartTx = 2048;
inline volatile bool g_log_tee_uart = false;

/* Instead of Serial0.begin(115200, SERIAL_8N1, 44, 43). */
inline void log_tee_uart_begin(void)
{
    if (!g_log_tee_uart) {
        Serial0.setTxBufferSize(kLogTeeUartTx); /* only before the first begin() */
    }
    Serial0.begin(115200, SERIAL_8N1, 44, 43);
    g_log_tee_uart = true;
}

class LogTee : public Print {
public:
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t *buf, size_t n) override
    {
        Serial.write(buf, n);
        if (g_log_tee_uart && Serial0.availableForWrite() >= static_cast<int>(n)) {
            Serial0.write(buf, n);
        }
        /* Each finished line also goes to the diagnostics ring as an event,
         * so the bench pull sees Wi-Fi clients, gateway and mode changes.
         * Lines from two tasks at once may mix; the ring is best effort. */
        for (size_t i = 0; i < n; ++i) {
            const char ch = static_cast<char>(buf[i]);
            if (ch == '\n' || line_n_ + 1 >= sizeof(line_)) {
                line_[line_n_] = '\0';
                if (line_n_ > 0) {
                    diag_log_add('L', line_);
                }
                line_n_ = 0;
            } else if (ch != '\r') {
                line_[line_n_++] = ch;
            }
        }
        return n;
    }

private:
    char line_[96] = {};
    size_t line_n_ = 0;
};

inline LogTee g_log;
