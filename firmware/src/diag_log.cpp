#include "diag_log.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

#include <cstdio>
#include <cstring>

/* 8192 x 96 bytes = 768 KB of PSRAM: about 25 min of a GPS-only bus
 * (RMC + GGA + GLL + VTG at 1 Hz), more with fewer sentences. */
static constexpr uint32_t kEntries = 8192;
static constexpr size_t kText = 86;

struct Entry {
    uint32_t seq;
    uint32_t ms;
    char kind;
    char text[kText + 1];
};

static Entry *g_ring = nullptr;
static uint32_t g_seq = 0;
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

void diag_log_init(void)
{
    if (g_ring == nullptr) {
        g_ring = static_cast<Entry *>(heap_caps_calloc(kEntries, sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    }
}

void diag_log_add(char kind, const char *text)
{
    if (g_ring == nullptr || text == nullptr) {
        return;
    }
    const uint32_t ms = millis();
    portENTER_CRITICAL(&g_lock);
    const uint32_t seq = g_seq + 1;
    Entry &e = g_ring[seq % kEntries];
    e.seq = seq;
    e.ms = ms;
    e.kind = kind;
    size_t i = 0;
    for (; i < kText && text[i] != '\0' && text[i] != '\r' && text[i] != '\n'; ++i) {
        e.text[i] = text[i];
    }
    e.text[i] = '\0';
    g_seq = seq;
    portEXIT_CRITICAL(&g_lock);
}

uint32_t diag_log_count(void) { return g_seq; }

size_t diag_log_read(uint32_t after, char *out, size_t cap, uint32_t *next, uint32_t *lost)
{
    if (next != nullptr) {
        *next = after;
    }
    if (lost != nullptr) {
        *lost = 0;
    }
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    if (g_ring == nullptr) {
        return 0;
    }
    portENTER_CRITICAL(&g_lock);
    const uint32_t head = g_seq;
    portEXIT_CRITICAL(&g_lock);
    const uint32_t oldest = head >= kEntries ? head - kEntries + 1 : 1;
    uint32_t seq = after + 1;
    if (seq < oldest) {
        if (lost != nullptr && after != 0) {
            *lost = oldest - seq;
        }
        seq = oldest;
    }
    constexpr size_t kLineMax = kText + 24;
    size_t used = 0;
    uint32_t last = seq - 1;
    while (seq <= head && used + kLineMax < cap) {
        Entry e;
        portENTER_CRITICAL(&g_lock);
        e = g_ring[seq % kEntries];
        portEXIT_CRITICAL(&g_lock);
        if (e.seq == seq) {
            const int w = snprintf(out + used, cap - used, "%lu.%03lu %c %s\n", static_cast<unsigned long>(e.ms / 1000),
                                   static_cast<unsigned long>(e.ms % 1000), e.kind, e.text);
            if (w > 0) {
                used += static_cast<size_t>(w);
            }
        } else if (lost != nullptr) {
            ++*lost;
        }
        last = seq;
        ++seq;
    }
    if (next != nullptr) {
        *next = last;
    }
    return used;
}
