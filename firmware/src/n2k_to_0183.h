#pragma once

#include <N2kMessages.h>

#include <cstddef>
#include <cstdint>

/* Decode one reassembled NMEA 2000 message into NMEA 0183 and push it
 * through nmea_feed_line(). GNSS / depth / wind / heading are cached and
 * emitted at 1 Hz from n2k_to_0183_poll(). AIS and DSC go out immediately. */
void n2k_to_0183_handle(const tN2kMsg &msg);
void n2k_to_0183_poll(void);

/* What the converter did with each PGN it handles, counted from boot.
 * rx = used + na + bad + throttled + other_src. */
struct N2kConvStats {
    unsigned long pgn;
    uint32_t rx;        /* reassembled messages of this PGN */
    uint32_t used;      /* went into a sentence or the position cache */
    uint32_t na;        /* the value was "not available" */
    uint32_t bad;       /* N2K error code, short message, or not physically possible */
    uint32_t throttled; /* valid, but inside the 1 Hz output window */
    uint32_t other_src; /* valid, but another device owns this value */
};

/* Fills up to max entries, one per handled PGN that has arrived at least once. */
size_t n2k_conv_stats(N2kConvStats *out, size_t max);
uint32_t n2k_conv_bad_total(void);

/* Each instrument value is taken from one device at a time. Another device
 * takes over only after the owner has sent nothing valid for 3 s. */
struct N2kSourceLock {
    const char *what;      /* "position", "depth", ... */
    int src;               /* owner, -1 when nothing has arrived */
    uint32_t age_ms;       /* since the owner last sent a valid value */
    int other;             /* newest other device that sent it, -1 for none */
    uint32_t other_age_ms;
};
size_t n2k_conv_sources(N2kSourceLock *out, size_t max);

/* Forget every owner. Canned PGNs (NMEA DEMO / PGN) come from made-up
 * addresses and must not wait out the simulator's devices. */
void n2k_conv_reset_sources(void);

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
