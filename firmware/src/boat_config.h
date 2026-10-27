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

/* The boat's name as crew say it ("Baca Lele"), shown as the title on the
 * terminal and in the app. LMYC_BOAT_ID stays the machine name in the SSID,
 * the BLE name and the pairing link. Plain text: no quotes or backslashes. */
#ifndef LMYC_BOAT_NAME
#define LMYC_BOAT_NAME LMYC_BOAT_ID
#endif

#ifndef LMYC_TERMINAL_ID
#define LMYC_TERMINAL_ID "WS7-001"
#endif

/* 1 builds the maintenance book (Reports), the chart picture (Charts), the
 * SD card store and their HTTP routes. 0 is the instruments-and-gateway
 * build: less to go wrong, and more internal RAM for BLE and Wi-Fi. */
#ifndef LMYC_WITH_REPORTS_CHARTS
#define LMYC_WITH_REPORTS_CHARTS 0
#endif

/* WordPress sailboat post id for this boat's maintenance book. 0 = unassigned. */
#ifndef LMYC_SAILBOAT_ID
#define LMYC_SAILBOAT_ID 0
#endif

/* SoftAP passphrase. WPA2 needs at least 8 characters. */
#ifndef AP_PASSWORD
#define AP_PASSWORD "lmycboat"
#endif

/* NMEA_SOURCE is the first-boot data source; the saved choice wins after
 * that. See n2k.h for the values. */

/* NMEA 2000 identity. Both codes are issued by NMEA to members; a unit
 * with NMEA-issued codes sets them in secrets.h. Until then 2046 is the
 * open-source manufacturer code the NMEA2000 library uses and 666 is its
 * placeholder product code. Neither is an NMEA registration, and a device
 * using them must not be described as NMEA certified. */
#ifndef LMYC_N2K_MANUFACTURER_CODE
#define LMYC_N2K_MANUFACTURER_CODE 2046
#endif
#ifndef LMYC_N2K_PRODUCT_CODE
#define LMYC_N2K_PRODUCT_CODE 666
#endif

/* Software version in Product Information (126996), at most 32 characters. */
#ifndef LMYC_FW_VERSION
#define LMYC_FW_VERSION "0.9 " __DATE__
#endif
