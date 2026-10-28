#pragma once

#include <cstdint>

class tNMEA2000;

/* Who the terminal is on the bus: one definition, used for the NAME and
 * Product Information here and read back by the network screen, the
 * phone and the JSON. Class 25 (inter/intranetwork device), function 130
 * (PC gateway): a gateway from NMEA 2000 to Wi-Fi and Bluetooth. Load
 * equivalency is in 50 mA units: at most 0.5 A from the bus (doc 19). */
inline constexpr uint8_t kN2kNodeIndustryGroup = 4;
inline constexpr uint8_t kN2kNodeDeviceClass = 25;
inline constexpr uint8_t kN2kNodeDeviceFunction = 130;
inline constexpr uint8_t kN2kNodeLoadEquivalency = 6;  // measured 260 mA on NET-S with Wi-Fi, 2026-10-28
inline constexpr uint16_t kN2kNodeN2kVersion = 2101;
inline constexpr const char *kN2kNodeModelId = "LMYC Boat Terminal";
inline constexpr const char *kN2kNodeModelVersion = "Waveshare ESP32-S3-Touch-LCD-7";
inline constexpr const char *kN2kNodeManufacturerInfo = "LMYC boat-terminal";

/* The terminal's identity on NMEA 2000, and what the standard says it must
 * remember across power cycles.
 *
 * The NMEA2000 library does the network management itself: address claim
 * and defence (60928), ISO request and acknowledgement (59904 / 59392),
 * group function (126208), commanded address (65240), PGN list (126464),
 * product (126996) and configuration information (126998), heartbeat
 * (126993). This file gives it the NAME and product data, and keeps the
 * values other devices may change: the claimed source address, device and
 * system instances, and both installation descriptions. */

/* Before Open(): NAME, product and configuration information, PGN lists,
 * heartbeat, and the saved address and instances. node: true on the real
 * backbone (ListenAndNode), false on the simulator's virtual bus. */
void n2k_node_configure(tNMEA2000 &stack, bool node);

/* Node task, after each ParseMessages(): saves anything the bus changed.
 * Returns true if it wrote flash. */
bool n2k_node_save_changes(tNMEA2000 &stack);

/* The NAME fields, for the screen, the phone and /n2k.json. */
uint32_t n2k_node_unique_number(void);
/* The serial code in Product Information: the MAC, as 12 hex digits. */
const char *n2k_node_serial(void);
uint16_t n2k_node_manufacturer_code(void);
