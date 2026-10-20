#pragma once

namespace esp_expander {
class Base;
}

/* TF card on the shared SPI pins. Chip select is CH422G EXIO4, so the
 * board's IO expander is passed in. Mounts FAT32, then opens the
 * maintenance book. */
bool sd_card_init(esp_expander::Base *expander);

bool sd_card_ready(void);
/* "SDHC  29818 MB", "Slot empty", "Need FAT32 (not exFAT)", … */
const char *sd_card_summary(void);
