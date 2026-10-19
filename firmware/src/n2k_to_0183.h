#pragma once

#include <N2kMessages.h>

#include <cstddef>

/* Decode one reassembled NMEA 2000 message into NMEA 0183 and push it
 * through nmea_feed_line(). GNSS / depth / wind / heading are cached and
 * emitted at 1 Hz from n2k_to_0183_poll(). AIS and DSC go out immediately. */
void n2k_to_0183_handle(const tN2kMsg &msg);
void n2k_to_0183_poll(void);

/* Cabin debug. Every reassembled message is kept, including ones this
 * file does not convert. Eight newest (PGN, source address) pairs. */
void n2k_pgn_reset(void);

/* "129026 COG/SOG  src 35  1s  410" per line, newest first.
 * Empty when nothing has arrived. */
void n2k_pgn_lines(char *out, size_t cap);

/* "COG/SOG, Depth" or "none". An unnamed PGN is its number. */
void n2k_pgn_names(char *out, size_t cap);

/* Inject one reassembled PGN. Does not open CAN. */
bool n2k_play_pgn(unsigned long pgn, uint8_t src, const uint8_t *data, size_t len);

/* "PGN <number> [src=N] <hex>". Hex may contain spaces. */
bool n2k_play_line(const char *args);

/* Canned instrument PGNs with values unlike the 1 Hz simulator:
 * depth 42.0 m, COG 90°, SOG 7.5 kn, wind 18 kn at 40° apparent,
 * heading 45° true, and PGN 130816 so an unknown type shows up. */
void n2k_play_demo(void);
