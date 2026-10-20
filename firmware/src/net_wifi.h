#pragma once

#include <cstddef>

/* SoftAP (always, for phones and chart apps) plus STA to the marina Wi-Fi
 * from secrets.h. The NMEA gateway and HTTP hub start from net_wifi_poll()
 * once either side is up, so they never start on the Wi-Fi event task. */

/* Call after pairing_init(): it hands the SoftAP details to BLE. */
void net_wifi_start(void);

/* loop(): STA reconnect backoff and the one-time service start. */
void net_wifi_poll(void);

bool net_wifi_sta_up(void);
/* "-" until DHCP. */
const char *net_wifi_sta_ip(void);
/* "Up  -62 dBm", "Retry 4s", "AP LMYC-…" and so on. */
void net_wifi_status(char *out, size_t cap);

const char *net_wifi_ap_ssid(void);
const char *net_wifi_ap_ip(void);
/* True after WIFI_EVENT_AP_START. */
bool net_wifi_ap_up(void);
/* Phones associated to the hotspot. Not the upstream STA. 0 until the AP is up. */
int net_wifi_ap_stations(void);
