#pragma once

#include <cstddef>

class tN2kMsg;

/* One message in words, for the node detail page: "Depth 12.3 m, offset
 * 0.4 m", "COG 090.0° T, SOG 5.8 kn", "Address claim: maker 229 class 120".
 * Decoded with the NMEA2000 library's own parsers; a PGN it does not know
 * gets "". Values the device reports as not available say "n/a". */
void n2k_describe(const tN2kMsg &msg, char *out, size_t cap);
