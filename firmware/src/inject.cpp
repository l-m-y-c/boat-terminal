#include "inject.h"

#include "nmea.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cstdio>
#include <cstring>

static constexpr size_t kInjectDepth = 8;

struct InjectLine {
    char text[kNmeaMaxSentence + 4];
};

static bool g_local = false;
static QueueHandle_t g_queue = nullptr;

void inject_init(void)
{
    g_local = false;
    if (g_queue == nullptr) {
        g_queue = xQueueCreate(kInjectDepth, sizeof(InjectLine));
    }
}

bool inject_local_armed(void) { return g_local; }
void inject_set_local(bool armed) { g_local = armed; }

bool inject_is_gnss_sentence(const char *sentence)
{
    if (sentence == nullptr || strlen(sentence) < 6) {
        return false;
    }
    if (sentence[0] != '$') {
        return false;
    }
    const char fmt[4] = {sentence[3], sentence[4], sentence[5], '\0'};
    return strcmp(fmt, "RMC") == 0 || strcmp(fmt, "GGA") == 0 ||
           strcmp(fmt, "VTG") == 0 || strcmp(fmt, "GLL") == 0;
}

bool inject_submit(const char *sentence)
{
    if (g_queue == nullptr || sentence == nullptr) {
        return false;
    }
    InjectLine line;
    snprintf(line.text, sizeof(line.text), "%s", sentence);
    return xQueueSend(g_queue, &line, 0) == pdTRUE;
}

void inject_poll(void)
{
    if (g_queue == nullptr) {
        return;
    }
    InjectLine line;
    while (xQueueReceive(g_queue, &line, 0) == pdTRUE) {
        /* LOCAL may have been disarmed after the phone posted. */
        if (g_local) {
            nmea_feed_line(line.text);
        }
    }
}
