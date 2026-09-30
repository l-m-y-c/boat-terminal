#pragma once

/* Physical NMEA byte source. Slice A is sim-only until RS485 pins are confirmed. */

void nmea_io_init(void);
void nmea_io_poll(void);

/* True when no real talker bytes have been seen recently (sim may fill in). */
bool nmea_io_quiet(void);
