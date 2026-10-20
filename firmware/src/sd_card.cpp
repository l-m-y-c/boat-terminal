#include "sd_card.h"

#include "maint_store.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_io_expander.hpp>

static constexpr int kSdMosi = 11;
static constexpr int kSdSck = 12;
static constexpr int kSdMiso = 13;
static constexpr int kSdCsExpander = 4; /* CH422G EXIO4 */

static char g_summary[40] = "Not checked";
static bool g_ready = false;

bool sd_card_ready(void) { return g_ready; }
const char *sd_card_summary(void) { return g_summary; }

bool sd_card_init(esp_expander::Base *expander)
{
    g_ready = false;
    if (expander == nullptr) {
        snprintf(g_summary, sizeof(g_summary), "No IO expander");
        Serial.println(g_summary);
        return false;
    }

    expander->pinMode(kSdCsExpander, OUTPUT);
    expander->digitalWrite(kSdCsExpander, LOW);
    delay(20);

    SPI.begin(kSdSck, kSdMiso, kSdMosi, -1);
    /* 4 MHz made a photo write or read feel stuck. 16 MHz is inside the SD SPI limit. */
    if (!SD.begin(-1, SPI, 16000000)) {
        snprintf(g_summary, sizeof(g_summary), "Need FAT32 (not exFAT)");
        Serial.println("SD: mount failed (FAT32 card seated?)");
        expander->digitalWrite(kSdCsExpander, HIGH);
        return false;
    }

    const uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        snprintf(g_summary, sizeof(g_summary), "Slot empty");
        Serial.println("SD: CARD_NONE");
        return false;
    }

    const char *type_name = "Unknown";
    if (type == CARD_MMC) {
        type_name = "MMC";
    } else if (type == CARD_SD) {
        type_name = "SD";
    } else if (type == CARD_SDHC) {
        type_name = "SDHC";
    }

    const uint64_t bytes = SD.cardSize();
    const unsigned long mb = static_cast<unsigned long>(bytes / (1024ULL * 1024ULL));
    snprintf(g_summary, sizeof(g_summary), "%s  %lu MB", type_name, mb);
    g_ready = true;
    Serial.printf("SD: %s\n", g_summary);
    maint_store_begin();
    return true;
}
