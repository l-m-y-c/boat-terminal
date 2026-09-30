#include "nmea_sim.h"

#include "nmea.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>

/* Vancouver harbour-ish: 49°16.45'N 123°06.78'W — clearly not a keel transducer. */
static bool g_enabled = true;
static bool g_emit_gnss = true;
static uint32_t g_last_ms = 0;
static uint32_t g_tick = 0;

void nmea_sim_init(void)
{
    g_enabled = true;
    g_emit_gnss = true;
    g_last_ms = 0;
    g_tick = 0;
}

void nmea_sim_set_enabled(bool enabled) { g_enabled = enabled; }
bool nmea_sim_enabled(void) { return g_enabled; }
void nmea_sim_set_emit_gnss(bool emit) { g_emit_gnss = emit; }
bool nmea_sim_emit_gnss(void) { return g_emit_gnss; }

static void emit(char *buf, size_t cap)
{
    nmea_append_checksum(buf, cap);
    nmea_feed_line(buf);
}

void nmea_sim_poll(void)
{
    if (!g_enabled) {
        return;
    }
    const uint32_t now = millis();
    if (g_last_ms != 0 && (now - g_last_ms) < 1000) {
        return;
    }
    g_last_ms = now;
    ++g_tick;

    /* Fake UTC from uptime so the stream is obviously synthetic. */
    const uint32_t sec = now / 1000UL;
    const unsigned hh = (sec / 3600UL) % 24UL;
    const unsigned mm = (sec / 60UL) % 60UL;
    const unsigned ss = sec % 60UL;

    const float sog = 0.4f + 0.05f * static_cast<float>(g_tick % 7);
    const float cog = 214.0f + static_cast<float>(g_tick % 5);
    const float depth = 12.0f + 0.1f * static_cast<float>(g_tick % 11);
    const float aws = 10.0f + 0.2f * static_cast<float>(g_tick % 9);
    const float awa = -22.0f + static_cast<float>(g_tick % 6);

    char line[kNmeaMaxSentence + 4];

    if (g_emit_gnss) {
        snprintf(line, sizeof(line),
                 "$GPRMC,%02u%02u%02u.00,A,4916.4500,N,12306.7800,W,%.1f,%.1f,020926,,,A",
                 hh, mm, ss, static_cast<double>(sog), static_cast<double>(cog));
        emit(line, sizeof(line));

        snprintf(line, sizeof(line),
                 "$GPGGA,%02u%02u%02u.00,4916.4500,N,12306.7800,W,1,08,1.0,12.0,M,,,",
                 hh, mm, ss);
        emit(line, sizeof(line));
    }

    /* Depth — SIM label lives on the UI; talker stays standard so apps parse it. */
    snprintf(line, sizeof(line), "$GPDPT,%.1f,0.0,", static_cast<double>(depth));
    emit(line, sizeof(line));

    snprintf(line, sizeof(line),
             "$WIMWV,%.1f,R,%.1f,N,A",
             static_cast<double>(awa < 0 ? awa + 360.0f : awa),
             static_cast<double>(aws));
    emit(line, sizeof(line));
}
