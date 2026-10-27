#pragma once

#include "n2k.h"

#include <cstddef>
#include <cstdint>

class tN2kMsg;

/* Bus diagnostics for the first runs on a real backbone: who is talking,
 * what each device sends, whether fast packets come out whole, and what
 * the converter did with them. loop() only, so no locking; n2k.cpp keeps
 * the JSON copy the web server reads.
 *
 * Frame-level counts come from every CAN frame before reassembly, message
 * counts from every message the library hands over. For a fast-packet PGN
 * "starts" is first frames seen and "msgs" is messages completed, so
 * starts - msgs is how many never came out whole. */

void n2k_diag_reset(void);

/* Every CAN frame. fast: the library reassembles this PGN as fast packet. */
void n2k_diag_frame(uint32_t can_id, uint8_t len, const uint8_t *data, bool fast, uint32_t now_ms);

/* Every reassembled message. Also reads 60928 / 126996 for device names. */
void n2k_diag_message(const tN2kMsg &msg, uint32_t now_ms);

/* Once a second: message rates. */
void n2k_diag_tick(uint32_t now_ms);

/* Fast-packet messages started but never completed, all PGNs. */
uint32_t n2k_diag_fast_incomplete(void);

/* PGN of the frame. PDU1 PGNs drop the destination byte. */
uint32_t n2k_diag_pgn(uint32_t can_id);

/* The one-second picture the hint is chosen from. */
struct N2kHintInput {
    DataSource source;
    BusState state;
    NodeState node;
    uint32_t frames_per_s;
    uint32_t repeats_per_s;
    uint32_t bus_errors_per_s;
    uint32_t lost_per_s;
    uint32_t fast_incomplete_per_s;
    uint32_t fast_starts_per_s;
    uint32_t bad_per_s;
    bool two_sources;
};

N2kHint n2k_hint_pick(const N2kHintInput &in);
/* "repeats", "errors_no_frames", ... "ok". */
const char *n2k_hint_code(N2kHint hint);
/* One or two sentences for the screen and the phone. Empty for None. */
const char *n2k_hint_text(N2kHint hint);

/* Manufacturer name for an NMEA 2000 manufacturer code, or nullptr. */
const char *n2k_maker_name(uint16_t code);

/* Whole picture as JSON: bus (from stats), devices, pgns, conv, sources.
 * Returns the length, 0 if it did not fit. */
size_t n2k_diag_json(char *out, size_t cap, const N2kStats &stats, uint32_t now_ms);

/* Fast-packet first frames seen, all PGNs. */
uint32_t n2k_diag_fast_starts(void);

/* "Display", "Navigation", ... for an NMEA 2000 device class, or "". */
const char *n2k_class_name(uint8_t cls);

/* One device's traffic for the node detail page, as JSON members (no
 * braces): "pgns":[...] with each PGN's rate, counts, the newest payload in
 * hex and decoded, and "recent":[...], its newest messages first. */
size_t n2k_diag_device_json(char *out, size_t cap, uint8_t src, uint32_t now_ms);

/* One device's PGNs and recent messages, decoded, for the node detail page. */
struct N2kPgnLine {
    uint32_t pgn;
    char name[20];
    uint16_t rate_x10;
    uint32_t msgs;
    uint32_t age_ms;
    char text[72]; /* decoded, or the payload in hex */
};
size_t n2k_diag_device_pgns(uint8_t src, N2kPgnLine *out, size_t max, uint32_t now_ms);
size_t n2k_diag_device_recent(uint8_t src, N2kPgnLine *out, size_t max, uint32_t now_ms);
