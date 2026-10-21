#pragma once

#include <cstddef>
#include <cstdint>

/* Decoded instrument snapshot + talker discovery for the helm UI. */

struct NmeaTalkerInfo {
    char id[3];          /* "GP", "WI", … */
    char formatters[48]; /* "RMC GGA DPT" */
    uint32_t sentences;
    uint32_t last_ms;
};

struct NmeaInstruments {
    bool have_depth;
    bool have_sog;
    bool have_cog;
    bool have_wind;
    float depth_m;
    float sog_kn;
    float cog_deg;
    float aws_kn;
    float awa_deg; /* signed, -180..180 preferred */
    uint32_t updated_ms;
    /* When each value last arrived. A value older than
     * kNmeaInstrumentStaleMs is shown as missing, never as current. */
    uint32_t depth_ms;
    uint32_t sog_ms;
    uint32_t cog_ms;
    uint32_t wind_ms;
};

/* The converter sends each instrument once a second. Five missed seconds
 * means the sensor or the bus has gone. */
static constexpr uint32_t kNmeaInstrumentStaleMs = 5000;

void nmea_state_init(void);

/* Call for every checksum-valid sentence (sim or live). */
void nmea_state_note(const char *sentence);

const NmeaInstruments *nmea_state_instruments(void);

/* True when stamp_ms (one of the *_ms fields) is recent enough to show. */
bool nmea_state_fresh(uint32_t stamp_ms);

size_t nmea_state_talker_count(void);
const NmeaTalkerInfo *nmea_state_talker(size_t index);

/* Human-readable formatter list for UI, e.g. "GP WI". */
void nmea_state_talkers_summary(char *out, size_t cap);
