#pragma once

#include <cstddef>
#include <cstdint>

/* NMEA 0183 line assembler + XOR checksum. Pass-through of unknown-but-valid
 * sentences is intentional (AIS !AIVDM etc.). */

static constexpr size_t kNmeaMaxSentence = 82; /* body excl. CR/LF; incl. $…*hh */

using NmeaSinkFn = void (*)(const char *sentence, size_t len, void *ctx);

void nmea_init(void);
void nmea_set_sink(NmeaSinkFn fn, void *ctx);

/* Feed raw UART/USB bytes; completes lines on CR or LF. */
void nmea_feed_bytes(const uint8_t *data, size_t n);

/* Feed one complete sentence (with or without CR/LF). Returns true if checksum OK. */
bool nmea_feed_line(const char *line);

uint32_t nmea_good_count(void);
uint32_t nmea_bad_count(void);

/* Compute XOR checksum for chars between '$'/'!' and '*'. */
uint8_t nmea_checksum(const char *sentence);

/* Write "*HH" (null-terminated) for an open sentence that already has '$'… fields. */
void nmea_append_checksum(char *sentence, size_t cap);
