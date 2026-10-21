#pragma once

#include <cstddef>
#include <cstdint>

class tN2kMsg;

/* One CAN frame as the controller delivers it: 29-bit identifier and up to
 * eight data bytes. The simulator and the TWAI receive task both produce
 * these, so everything after the frame queue is the same in both modes. */
struct N2kFrame {
    uint32_t id;
    uint8_t len;
    uint8_t data[8];
};

/* Takes one frame. Returns false when it could not keep it. */
using N2kFrameSink = bool (*)(const N2kFrame &frame, void *ctx);

/* 29-bit NMEA 2000 identifier. PDU1 PGNs carry the destination in the low
 * byte; PDU2 PGNs are broadcast and ignore it. */
uint32_t n2k_can_id(uint8_t priority, uint32_t pgn, uint8_t source, uint8_t destination);

/* Splits one message into frames: a single frame up to eight bytes, fast
 * packet above that (frame 0 carries the length, then seven bytes a frame).
 * sequence is the 3-bit fast-packet counter. Returns frames handed to sink. */
size_t n2k_frames_from_msg(const tN2kMsg &msg, uint8_t sequence, N2kFrameSink sink, void *ctx);

/* The bench simulator: a sailing boat working a box course in English Bay,
 * on a virtual bus with a GPS, compass, wind sensor, depth/speed transducer,
 * battery monitor and an AIS receiver with three targets. It builds real
 * PGNs and hands real CAN frames to the sink at realistic rates. */
void n2k_sim_reset(void);

/* False while the phone owns GNSS (LOCAL on the 7″): no position, COG/SOG,
 * GNSS or time PGNs. Everything else keeps coming. */
void n2k_sim_set_gnss(bool on);
bool n2k_sim_gnss(void);

/* Emits every frame due by now_ms. Call at 100 Hz or faster. The first call
 * starts the clock. Returns the number of frames handed to sink. */
size_t n2k_sim_step(uint32_t now_ms, N2kFrameSink sink, void *ctx);
