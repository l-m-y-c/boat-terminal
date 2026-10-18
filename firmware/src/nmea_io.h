#pragma once

/* Console bytes (USB, or UART0 once N2K owns the CAN mux) and, later, RS485.
 * "NMEA SIM|N2K|AUTO" is taken here. Sentences starting with $ or ! go to the parser. */

void nmea_io_init(void);
void nmea_io_poll(void);

/* True when no real talker bytes have been seen recently (sim may fill in). */
bool nmea_io_quiet(void);
