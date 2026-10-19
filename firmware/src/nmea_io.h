#pragma once

/* Console bytes on USB and on UART0 (CH343, GPIO43/44), and later RS485.
 * "NMEA SIM|N2K|AUTO|DEMO" and "NMEA PGN ..." are taken here.
 * Sentences starting with $ or ! go to the parser. */

void nmea_io_init(void);
void nmea_io_poll(void);

/* True when no real talker bytes have been seen recently (sim may fill in). */
bool nmea_io_quiet(void);
