#pragma once
/* Host stand-in: a fixed MAC so the NAME's unique number is known. */
#include <cstdint>
#include <cstring>
static inline int esp_efuse_mac_get_default(uint8_t *mac)
{
    static const uint8_t kMac[6] = {0x58, 0xe6, 0xc5, 0x72, 0xd6, 0x48};
    memcpy(mac, kMac, 6);
    return 0;
}
