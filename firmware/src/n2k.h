#pragma once

#include <cstdint>

/* NMEA input. 0 keeps the bench simulator and the USB socket.
 * 1 and 2 listen on the CAN header. That muxes GPIO19/20 away from USB,
 * so the serial log moves to the UART Type-C socket. See docs/05. */
#define NMEA_SOURCE_SIM 0
#define NMEA_SOURCE_N2K 1
#define NMEA_SOURCE_AUTO 2

/* can_mode true connects the TJA1051. false connects the USB socket. */
using N2kCanSelectFn = void (*)(bool can_mode);

void n2k_init(N2kCanSelectFn select_can);
void n2k_poll(void);

int n2k_source(void);
void n2k_set_source(int source);

/* "NMEA SIM|N2K|AUTO". Returns true if the line was a command. */
bool n2k_handle_command(const char *line);

/* True once the USB/CAN mux is on the transceiver and logs go to UART0. */
bool n2k_using_uart(void);

/* True when no CAN frame has arrived for 3 s (simulator may fill in). */
bool n2k_quiet(void);

uint32_t n2k_frame_count(void);

/* "SIM", "N2K" (selected, no frames yet), or "BOAT". */
const char *n2k_fix_tag(void);
