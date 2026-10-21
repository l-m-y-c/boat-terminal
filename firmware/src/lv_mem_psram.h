#pragma once

#include <stdlib.h>
#include <esp_heap_caps.h>

/* LVGL widgets are small allocations, which the IDF malloc keeps in internal
 * RAM. Internal RAM is what BLE, Wi-Fi and the CAN driver need, so the UI
 * lives in PSRAM and falls back to internal only if PSRAM is exhausted. */
static inline void *lv_psram_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : malloc(size);
}

static inline void *lv_psram_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p != NULL ? p : realloc(ptr, size);
}
