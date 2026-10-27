#include "n2k_node.h"

#include "boat_config.h"

#include <NMEA2000.h>

#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include <cstring>

#include <esp_mac.h>

/* NAME. Class 25 (inter/intranetwork device), function 130 (PC gateway):
 * the terminal is a gateway from NMEA 2000 to Wi-Fi and Bluetooth. */
static constexpr unsigned char kDeviceClass = 25;
static constexpr unsigned char kDeviceFunction = 130;
static constexpr unsigned char kIndustryMarine = 4;

/* Product information. Load equivalency is in 50 mA units: the board, its
 * 5 V converter and the drop draw at most 0.5 A from the bus (doc 19). */
static constexpr unsigned char kLoadEquivalency = 10;
static constexpr unsigned short kN2kVersion = 2101;
static constexpr const char *kModelId = "LMYC Boat Terminal";
static constexpr const char *kModelVersion = "Waveshare ESP32-S3-Touch-LCD-7";
static constexpr const char *kManufacturerInfo = "LMYC boat-terminal";

/* A first boot has no saved address. 22 is only where the search starts:
 * the library moves to a free address if another device holds it. */
static constexpr uint8_t kFirstAddress = 22;

static constexpr const char *kPrefsSpace = "n2k";
static constexpr const char *kKeyAddress = "addr";
static constexpr const char *kKeyInstLower = "inst_lo";
static constexpr const char *kKeyInstUpper = "inst_hi";
static constexpr const char *kKeySystem = "sys";
static constexpr const char *kKeyDesc1 = "desc1";
static constexpr const char *kKeyDesc2 = "desc2";

/* Every PGN the converter reads. Transmit lists need nothing beyond the
 * network-management PGNs the library adds itself: the terminal sends no
 * data on NMEA 2000. */
static const unsigned long kReceivePgns[] PROGMEM = {
    126992UL, 127250UL, 127258UL, 128259UL, 128267UL, 129025UL, 129026UL, 129029UL,
    129038UL, 129039UL, 129040UL, 129794UL, 129808UL, 129809UL, 129810UL, 130306UL,
    130310UL, 130312UL, 130316UL, 0,
};

static uint32_t g_unique = 0;
static char g_serial[Max_N2kModelSerialCode_len + 1] = "";

static uint32_t unique_from_mac(void)
{
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    /* 21 bits, unique among units from the same manufacturer code. */
    return ((static_cast<uint32_t>(mac[3]) << 16) | (static_cast<uint32_t>(mac[4]) << 8) | mac[5]) & 0x1fffffu;
}

uint32_t n2k_node_unique_number(void) { return g_unique; }
uint16_t n2k_node_manufacturer_code(void) { return LMYC_N2K_MANUFACTURER_CODE; }
const char *n2k_node_serial(void) { return g_serial; }

void n2k_node_configure(tNMEA2000 &stack, bool node)
{
    g_unique = unique_from_mac();
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    char *serial = g_serial;
    snprintf(serial, sizeof(g_serial), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    stack.SetProductInformation(serial, LMYC_N2K_PRODUCT_CODE, kModelId, LMYC_FW_VERSION, kModelVersion,
                                kLoadEquivalency, kN2kVersion);
    stack.SetDeviceInformation(g_unique, kDeviceFunction, kDeviceClass, LMYC_N2K_MANUFACTURER_CODE,
                               kIndustryMarine);

    Preferences prefs;
    const bool open = prefs.begin(kPrefsSpace, true);
    const uint8_t address = open ? prefs.getUChar(kKeyAddress, kFirstAddress) : kFirstAddress;
    const uint8_t lower = open ? prefs.getUChar(kKeyInstLower, 0) : 0;
    const uint8_t upper = open ? prefs.getUChar(kKeyInstUpper, 0) : 0;
    const uint8_t system = open ? prefs.getUChar(kKeySystem, 0) : 0;
    char desc1[Max_N2kConfigurationInfoField_len] = LMYC_BOAT_NAME;
    char desc2[Max_N2kConfigurationInfoField_len] = "";
    if (open && prefs.isKey(kKeyDesc1)) {
        prefs.getString(kKeyDesc1, desc1, sizeof(desc1));
    }
    if (open && prefs.isKey(kKeyDesc2)) {
        prefs.getString(kKeyDesc2, desc2, sizeof(desc2));
    }
    if (open) {
        prefs.end();
    }

    stack.SetDeviceInformationInstances(lower, upper, system);
    stack.SetConfigurationInformation(kManufacturerInfo, desc1, desc2);
    stack.ExtendReceiveMessages(kReceivePgns);
    /* Heartbeat (126993): the library sets the standard 60 s itself at Open(). */
    stack.SetMode(node ? tNMEA2000::N2km_ListenAndNode : tNMEA2000::N2km_ListenOnly,
                  address < N2kNullCanBusAddress ? address : kFirstAddress);
}

bool n2k_node_save_changes(tNMEA2000 &stack)
{
    const bool address = stack.ReadResetAddressChanged();
    const bool device = stack.ReadResetDeviceInformationChanged();
    const bool desc = stack.ReadResetInstallationDescriptionChanged();
    if (!address && !device && !desc) {
        return false;
    }
    Preferences prefs;
    if (!prefs.begin(kPrefsSpace, false)) {
        return false;
    }
    if (address) {
        const uint8_t a = stack.GetN2kSource();
        /* 254 is "cannot claim": keep the last good address to try first. */
        if (a < N2kNullCanBusAddress) {
            prefs.putUChar(kKeyAddress, a);
        }
    }
    if (device) {
        const tNMEA2000::tDeviceInformation info = stack.GetDeviceInformation();
        prefs.putUChar(kKeyInstLower, info.GetDeviceInstanceLower());
        prefs.putUChar(kKeyInstUpper, info.GetDeviceInstanceUpper());
        prefs.putUChar(kKeySystem, info.GetSystemInstance());
    }
    if (desc) {
        char buf[Max_N2kConfigurationInfoField_len];
        stack.GetInstallationDescription1(buf, sizeof(buf));
        prefs.putString(kKeyDesc1, buf);
        stack.GetInstallationDescription2(buf, sizeof(buf));
        prefs.putString(kKeyDesc2, buf);
    }
    prefs.end();
    return true;
}
