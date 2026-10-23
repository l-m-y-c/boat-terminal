#include "nmea_state.h"

#include <Arduino.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr size_t kMaxTalkers = 8;

static NmeaInstruments g_inst = {};
static NmeaTalkerInfo g_talkers[kMaxTalkers];
static size_t g_talker_n = 0;

void nmea_state_init(void)
{
    memset(&g_inst, 0, sizeof(g_inst));
    memset(g_talkers, 0, sizeof(g_talkers));
    g_talker_n = 0;
}

static bool parse_float(const char *s, float *out)
{
    if (s == nullptr || s[0] == '\0') {
        return false;
    }
    char *end = nullptr;
    const float v = strtof(s, &end);
    if (end == s) {
        return false;
    }
    *out = v;
    return true;
}

static void note_talker(const char talker[3], const char *formatter)
{
    NmeaTalkerInfo *slot = nullptr;
    for (size_t i = 0; i < g_talker_n; ++i) {
        if (strcmp(g_talkers[i].id, talker) == 0) {
            slot = &g_talkers[i];
            break;
        }
    }
    if (slot == nullptr) {
        if (g_talker_n >= kMaxTalkers) {
            return;
        }
        slot = &g_talkers[g_talker_n++];
        memset(slot, 0, sizeof(*slot));
        memcpy(slot->id, talker, 3);
    }
    slot->sentences++;
    slot->last_ms = millis();
    if (formatter != nullptr && formatter[0] != '\0' &&
        strstr(slot->formatters, formatter) == nullptr) {
        const size_t used = strlen(slot->formatters);
        if (used + 4 < sizeof(slot->formatters)) {
            if (used > 0) {
                slot->formatters[used] = ' ';
                slot->formatters[used + 1] = '\0';
            }
            strncat(slot->formatters, formatter,
                    sizeof(slot->formatters) - strlen(slot->formatters) - 1);
        }
    }
}

void nmea_state_note(const char *sentence)
{
    if (sentence == nullptr || (sentence[0] != '$' && sentence[0] != '!')) {
        return;
    }
    if (strlen(sentence) < 6) {
        return;
    }

    char talker[3] = {sentence[1], sentence[2], '\0'};
    char formatter[4] = {sentence[3], sentence[4], sentence[5], '\0'};
    note_talker(talker, formatter);

    /* Copy and split fields 0..n on commas; stop at '*'. */
    char buf[96];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *star = strchr(buf, '*');
    if (star != nullptr) {
        *star = '\0';
    }

    const char *fields[16] = {};
    size_t nfields = 0;
    char *p = buf;
    while (p != nullptr && nfields < 16) {
        fields[nfields++] = p;
        char *comma = strchr(p, ',');
        if (comma == nullptr) {
            break;
        }
        *comma = '\0';
        p = comma + 1;
    }

    const uint32_t now = millis();

    if (strcmp(formatter, "RMC") == 0 && nfields > 8) {
        float sog = 0, cog = 0;
        if (parse_float(fields[7], &sog)) {
            g_inst.sog_kn = sog;
            g_inst.have_sog = true;
            g_inst.sog_ms = now;
            g_inst.updated_ms = now;
        }
        if (parse_float(fields[8], &cog)) {
            g_inst.cog_deg = cog;
            g_inst.have_cog = true;
            g_inst.cog_ms = now;
            g_inst.updated_ms = now;
        }
    } else if ((strcmp(formatter, "DPT") == 0 || strcmp(formatter, "DBT") == 0) &&
               nfields > 1) {
        float d = 0;
        if (strcmp(formatter, "DBT") == 0 && nfields > 3) {
            /* DBT: feet, metres, fathoms — field 3 is metres. */
            if (parse_float(fields[3], &d)) {
                g_inst.depth_m = d;
                g_inst.have_depth = true;
                g_inst.depth_ms = now;
                g_inst.updated_ms = now;
            }
        } else if (parse_float(fields[1], &d)) {
            g_inst.depth_m = d;
            g_inst.have_depth = true;
            g_inst.depth_ms = now;
            g_inst.updated_ms = now;
        }
    } else if (strcmp(formatter, "VTG") == 0 && nfields > 5) {
        /* $GPVTG,cog,T,,M,sog,N,kmh,K,mode. 129026 with no position emits
         * VTG and not RMC, so course and speed have to come from here. */
        float sog = 0, cog = 0;
        if (parse_float(fields[5], &sog)) {
            g_inst.sog_kn = sog;
            g_inst.have_sog = true;
            g_inst.sog_ms = now;
            g_inst.updated_ms = now;
        }
        if (parse_float(fields[1], &cog)) {
            g_inst.cog_deg = cog;
            g_inst.have_cog = true;
            g_inst.cog_ms = now;
            g_inst.updated_ms = now;
        }
    } else if (strcmp(formatter, "MWV") == 0 && nfields > 4) {
        /* The tile is apparent wind (AWS / AWA). True wind (T) would put a
         * true speed beside an apparent angle, and a boat sending both
         * would flicker between them, so only R updates it. */
        const char ref = fields[2][0];
        float ang = 0, spd = 0;
        if ((ref == 'R' || ref == 'r') && parse_float(fields[1], &ang) &&
            parse_float(fields[3], &spd)) {
            const char unit = fields[4][0];
            if (unit == 'M' || unit == 'm') {
                spd *= 1.943844f; /* m/s -> kn */
            } else if (unit == 'K' || unit == 'k') {
                spd /= 1.852f; /* km/h -> kn */
            }
            /* Fold 0..360 into -180..180 for display. */
            if (ang > 180.0f) {
                ang -= 360.0f;
            }
            g_inst.awa_deg = ang;
            g_inst.aws_kn = spd;
            g_inst.have_wind = true;
            g_inst.wind_ms = now;
            g_inst.updated_ms = now;
        }
    }
}

const NmeaInstruments *nmea_state_instruments(void) { return &g_inst; }

bool nmea_state_fresh(uint32_t stamp_ms)
{
    return stamp_ms != 0 && (millis() - stamp_ms) <= kNmeaInstrumentStaleMs;
}
size_t nmea_state_talker_count(void) { return g_talker_n; }
const NmeaTalkerInfo *nmea_state_talker(size_t index)
{
    return index < g_talker_n ? &g_talkers[index] : nullptr;
}

void nmea_state_talkers_summary(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    for (size_t i = 0; i < g_talker_n; ++i) {
        const size_t used = strlen(out);
        snprintf(out + used, cap - used, "%s%s", i == 0 ? "" : "  ", g_talkers[i].id);
    }
    if (g_talker_n == 0) {
        strncpy(out, "—", cap);
        out[cap - 1] = '\0';
    }
}
