#include "n2k_describe.h"

#include <NMEA2000.h>
#include <N2kMessages.h>

#include <cmath>
#include <cstdio>
#include <cstring>

static constexpr double kRadToDeg = 57.29577951308232;
static constexpr double kMsToKn = 1.9438444924406046;

static bool na(double v) { return N2kIsNA(v) || !std::isfinite(v); }

/* "12.3" or "n/a". */
static const char *num(char *buf, size_t cap, double v, const char *fmt)
{
    if (na(v)) {
        snprintf(buf, cap, "n/a");
    } else {
        snprintf(buf, cap, fmt, v);
    }
    return buf;
}

static void clock_text(double seconds, char *buf, size_t cap)
{
    if (na(seconds) || seconds < 0 || seconds >= 86400) {
        snprintf(buf, cap, "n/a");
        return;
    }
    const long s = static_cast<long>(seconds);
    snprintf(buf, cap, "%02ld:%02ld:%02ld UTC", s / 3600, (s / 60) % 60, s % 60);
}

void n2k_describe(const tN2kMsg &msg, char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    char a[24], b[24], c[24];
    unsigned char sid = 0;
    switch (msg.PGN) {
    case 59904L: {
        unsigned long pgn = 0;
        if (ParseN2kPGN59904(msg, pgn)) {
            snprintf(out, cap, "Request for PGN %lu, to %u", pgn, static_cast<unsigned>(msg.Destination));
        }
        break;
    }
    case 59392L:
        if (msg.DataLen >= 8) {
            const unsigned long pgn = msg.Data[5] | (msg.Data[6] << 8) | (static_cast<unsigned long>(msg.Data[7]) << 16);
            snprintf(out, cap, "%s for PGN %lu, to %u", msg.Data[0] == 0 ? "ACK" : "NAK", pgn,
                     static_cast<unsigned>(msg.Destination));
        }
        break;
    case 60928L:
        if (msg.DataLen >= 8) {
            uint64_t n = 0;
            for (int i = 7; i >= 0; --i) {
                n = (n << 8) | msg.Data[i];
            }
            snprintf(out, cap, "Address claim: maker %u, class %u, function %u, instance %u, unique %lu",
                     static_cast<unsigned>((n >> 21) & 0x7ff), static_cast<unsigned>((n >> 49) & 0x7f),
                     static_cast<unsigned>((n >> 40) & 0xff), static_cast<unsigned>((n >> 32) & 0xff),
                     static_cast<unsigned long>(n & 0x1fffff));
        }
        break;
    case 126993L:
        if (msg.DataLen >= 3) {
            snprintf(out, cap, "Heartbeat, every %u s, #%u", static_cast<unsigned>((msg.Data[0] | (msg.Data[1] << 8)) / 1000),
                     static_cast<unsigned>(msg.Data[2]));
        }
        break;
    case 126996L: {
        unsigned short version = 0, code = 0;
        char model[33] = {}, sw[33] = {}, mv[33] = {}, serial[33] = {};
        unsigned char cert = 0, len = 0;
        if (ParseN2kPGN126996(msg, version, code, sizeof(model), model, sizeof(sw), sw, sizeof(mv), mv,
                              sizeof(serial), serial, cert, len)) {
            snprintf(out, cap, "Product: %s, software %s, %u LEN", model, sw, static_cast<unsigned>(len));
        }
        break;
    }
    case 126998L: {
        char maker[71] = {}, d1[71] = {}, d2[71] = {};
        size_t lm = sizeof(maker), l1 = sizeof(d1), l2 = sizeof(d2);
        if (ParseN2kPGN126998(msg, lm, maker, l1, d1, l2, d2)) {
            snprintf(out, cap, "Configuration: installed \"%s\"%s%s", d1, d2[0] ? ", " : "", d2);
        }
        break;
    }
    case 126464L:
        if (msg.DataLen >= 1) {
            snprintf(out, cap, "%s PGN list, %d PGNs", msg.Data[0] == 0 ? "Transmit" : "Receive", (msg.DataLen - 1) / 3);
        }
        break;
    case 126992L: {
        uint16_t days = 0;
        double secs = 0;
        tN2kTimeSource src = N2ktimes_GPS;
        if (ParseN2kPGN126992(msg, sid, days, secs, src)) {
            clock_text(secs, a, sizeof(a));
            snprintf(out, cap, "System time %s, day %u", a, static_cast<unsigned>(days));
        }
        break;
    }
    case 127250L: {
        double h = 0, dev = 0, var = 0;
        tN2kHeadingReference ref = N2khr_true;
        if (ParseN2kPGN127250(msg, sid, h, dev, var, ref)) {
            snprintf(out, cap, "Heading %s° %s, variation %s°", num(a, sizeof(a), na(h) ? h : h * kRadToDeg, "%.1f"),
                     ref == N2khr_magnetic ? "M" : "T", num(b, sizeof(b), na(var) ? var : var * kRadToDeg, "%.1f"));
        }
        break;
    }
    case 127258L: {
        tN2kMagneticVariation src = N2kmagvar_Manual;
        uint16_t days = 0;
        double var = 0;
        if (ParseN2kPGN127258(msg, sid, src, days, var)) {
            snprintf(out, cap, "Variation %s°", num(a, sizeof(a), na(var) ? var : var * kRadToDeg, "%.1f"));
        }
        break;
    }
    case 127508L: {
        unsigned char inst = 0;
        double v = 0, i = 0, t = 0;
        if (ParseN2kPGN127508(msg, inst, v, i, t, sid)) {
            snprintf(out, cap, "Battery %u: %s V, %s A", static_cast<unsigned>(inst), num(a, sizeof(a), v, "%.2f"),
                     num(b, sizeof(b), i, "%.1f"));
        }
        break;
    }
    case 128259L: {
        double w = 0, g = 0;
        tN2kSpeedWaterReferenceType t = N2kSWRT_Paddle_wheel;
        if (ParseN2kPGN128259(msg, sid, w, g, t)) {
            snprintf(out, cap, "Speed through water %s kn", num(a, sizeof(a), na(w) ? w : w * kMsToKn, "%.1f"));
        }
        break;
    }
    case 128267L: {
        double d = 0, off = 0, range = 0;
        if (ParseN2kPGN128267(msg, sid, d, off, range)) {
            snprintf(out, cap, "Depth %s m, offset %s m", num(a, sizeof(a), d, "%.1f"), num(b, sizeof(b), off, "%.2f"));
        }
        break;
    }
    case 129025L: {
        double lat = 0, lon = 0;
        if (ParseN2kPGN129025(msg, lat, lon)) {
            snprintf(out, cap, "Position %s, %s", num(a, sizeof(a), lat, "%.5f"), num(b, sizeof(b), lon, "%.5f"));
        }
        break;
    }
    case 129026L: {
        tN2kHeadingReference ref = N2khr_true;
        double cog = 0, sog = 0;
        if (ParseN2kPGN129026(msg, sid, ref, cog, sog)) {
            snprintf(out, cap, "COG %s° %s, SOG %s kn", num(a, sizeof(a), na(cog) ? cog : cog * kRadToDeg, "%.1f"),
                     ref == N2khr_magnetic ? "M" : "T", num(b, sizeof(b), na(sog) ? sog : sog * kMsToKn, "%.1f"));
        }
        break;
    }
    case 129029L: {
        unsigned char sats = 0, nref = 0;
        uint16_t days = 0, station = 0;
        double secs = 0, lat = 0, lon = 0, alt = 0, hdop = 0, pdop = 0, geoid = 0, age = 0;
        tN2kGNSStype t = N2kGNSSt_GPS, rt = N2kGNSSt_GPS;
        tN2kGNSSmethod m = N2kGNSSm_noGNSS;
        if (ParseN2kPGN129029(msg, sid, days, secs, lat, lon, alt, t, m, sats, hdop, pdop, geoid, nref, rt, station,
                              age)) {
            snprintf(out, cap, "GNSS fix method %d, %u sats, HDOP %s, %s, %s", static_cast<int>(m),
                     static_cast<unsigned>(N2kIsNA(sats) ? 0 : sats), num(c, sizeof(c), hdop, "%.1f"),
                     num(a, sizeof(a), lat, "%.5f"), num(b, sizeof(b), lon, "%.5f"));
        }
        break;
    }
    case 129038L:
    case 129039L:
        if (msg.DataLen >= 5) {
            const unsigned long mmsi = msg.Data[1] | (msg.Data[2] << 8) | (static_cast<unsigned long>(msg.Data[3]) << 16) |
                                       (static_cast<unsigned long>(msg.Data[4]) << 24);
            snprintf(out, cap, "AIS class %s position, MMSI %lu", msg.PGN == 129038L ? "A" : "B", mmsi);
        }
        break;
    case 129794L:
    case 129809L:
    case 129810L:
        if (msg.DataLen >= 5) {
            const unsigned long mmsi = msg.Data[1] | (msg.Data[2] << 8) | (static_cast<unsigned long>(msg.Data[3]) << 16) |
                                       (static_cast<unsigned long>(msg.Data[4]) << 24);
            snprintf(out, cap, "AIS static data, MMSI %lu", mmsi);
        }
        break;
    case 130306L: {
        double s = 0, ang = 0;
        tN2kWindReference ref = N2kWind_Apparent;
        if (ParseN2kPGN130306(msg, sid, s, ang, ref)) {
            const char *kind = ref == N2kWind_Apparent ? "apparent"
                               : (ref == N2kWind_True_North || ref == N2kWind_Magnetic) ? "ground" : "true";
            snprintf(out, cap, "Wind %s kn at %s°, %s", num(a, sizeof(a), na(s) ? s : s * kMsToKn, "%.1f"),
                     num(b, sizeof(b), na(ang) ? ang : ang * kRadToDeg, "%.0f"), kind);
        }
        break;
    }
    case 130312L:
    case 130316L: {
        unsigned char inst = 0;
        tN2kTempSource src = N2kts_SeaTemperature;
        double t = 0, set = 0;
        const bool ok = msg.PGN == 130312L ? ParseN2kPGN130312(msg, sid, inst, src, t, set)
                                            : ParseN2kPGN130316(msg, sid, inst, src, t, set);
        if (ok) {
            snprintf(out, cap, "Temperature %s °C (source %d)", num(a, sizeof(a), na(t) ? t : t - 273.15, "%.1f"),
                     static_cast<int>(src));
        }
        break;
    }
    default:
        break;
    }
}
