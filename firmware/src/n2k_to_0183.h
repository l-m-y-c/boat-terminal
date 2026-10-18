#pragma once

#include <N2kMessages.h>

/* Decode one reassembled NMEA 2000 message into NMEA 0183 and push it
 * through nmea_feed_line(). GNSS / depth / wind / heading are cached and
 * emitted at 1 Hz from n2k_to_0183_poll(). AIS and DSC go out immediately. */
void n2k_to_0183_handle(const tN2kMsg &msg);
void n2k_to_0183_poll(void);
