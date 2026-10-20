#pragma once

/* Per-boat build settings. secrets.h (gitignored, copied from
 * secrets.h.example) overrides any of these.
 *
 * Include this header, never secrets.h directly. A file that read a macro
 * without secrets.h got the default instead of the boat's value: /status.json
 * reported sailboat_id 0 while the card stamped the real one. */

#if __has_include("secrets.h")
#include "secrets.h"
#define LMYC_HAVE_SECRETS 1
#else
#define LMYC_HAVE_SECRETS 0
#endif

/* Bench identity. A production unit gets a provisioned boat id + device key. */
#ifndef LMYC_BOAT_ID
#define LMYC_BOAT_ID "BENCH-01"
#endif

#ifndef LMYC_TERMINAL_ID
#define LMYC_TERMINAL_ID "WS7-001"
#endif

/* WordPress sailboat post id for this boat's maintenance book. 0 = unassigned. */
#ifndef LMYC_SAILBOAT_ID
#define LMYC_SAILBOAT_ID 0
#endif

/* SoftAP passphrase. WPA2 needs at least 8 characters. */
#ifndef AP_PASSWORD
#define AP_PASSWORD "lmycboat"
#endif

/* NMEA_SOURCE defaults to the simulator. See n2k.h for the values. */
