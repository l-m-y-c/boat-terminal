#pragma once

#include <cstddef>
#include <cstdint>

/* The last few thousand 0183 sentences the terminal produced and the
 * events around them (mode, Wi-Fi clients, bus state), in PSRAM, for the
 * bench: GET /diag/nmea.txt over Wi-Fi, NMEA OUT [n] on the UART.
 * Lost at power-off; tools/boat_diag.py pulls it before that. */

void diag_log_init(void);
/* kind: 'S' a sentence the converter or a source produced, 'E' an NMEA 2000 event, 'L' an app log line. */
void diag_log_add(char kind, const char *text);
/* Entries after sequence `after` (0 = the oldest kept) as lines
 * "<seconds since boot> <kind> <text>\n". *next is the last one written. */
size_t diag_log_read(uint32_t after, char *out, size_t cap, uint32_t *next, uint32_t *lost);
uint32_t diag_log_count(void);
