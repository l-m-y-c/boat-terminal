#pragma once

#include <cstddef>
#include <cstdint>

/* ITU-R M.1371 bit packer and !AIVDM / !AIVDO formatter.
 * Sentences are checksummed and capped at the 82-character NMEA 0183 limit. */

struct AisBits {
    uint8_t data[64];
    int nbits;

    void clear(void);
    void put(uint32_t value, int width);
    void put_signed(int32_t value, int width);
    void put_text(const char *text, int nchars);
};

/* channel is 'A' or 'B'. own selects !AIVDO (this station) vs !AIVDM (others).
 * Writes up to max_out sentences into out[][84]. Returns how many. */
int ais_finish(const AisBits &bits, char channel, bool own, char out[][84], int max_out);
