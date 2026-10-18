#include "n2k_to_0183.h"

#include "ais0183.h"
#include "nmea.h"

#include <Arduino.h>
#include <N2kMessages.h>

#include <cmath>
#include <cstdio>
#include <cstring>

/* PGN → NMEA 0183. Phone-facing sentences stay 0183 so Navionics and the
 * LMYC app do not change.
 *
 *  129025 position rapid          GLL + RMC/GGA cache
 *  129026 COG/SOG rapid           RMC/VTG cache
 *  129029 GNSS position           RMC + GGA
 *  126992 system time             RMC time
 *  128267 depth                   DPT
 *  128259 water speed             VHW
 *  130306 wind                    MWV
 *  127250 heading                 HDG or HDT
 *  129038 AIS Class A position    !AIVDM/!AIVDO type 1
 *  129039 AIS Class B position    type 18
 *  129040 AIS Class B extended    type 19
 *  129794 AIS Class A static      type 5
 *  129809 AIS Class B static A    type 24 part A
 *  129810 AIS Class B static B    type 24 part B
 *  129808 DSC call                $CDDSE (not a synthesized distress)
 *
 * GNSS, depth, wind, and heading are emitted at 1 Hz. AIS goes out as received.
 * v1 does not transmit on NMEA 2000.
 */

static constexpr double kMsToKn = 1.9438444924406046;
static constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
static constexpr uint32_t kEmitMs = 1000;

static void emit_sentence(char *line, size_t cap)
{
    nmea_append_checksum(line, cap);
    if (strlen(line) >= 6 && strlen(line) <= kNmeaMaxSentence) {
        nmea_feed_line(line);
    }
}

static bool missing(double v)
{
    return N2kIsNA(v) || !std::isfinite(v);
}

static double wrap360(double deg)
{
    while (deg < 0.0) {
        deg += 360.0;
    }
    while (deg >= 360.0) {
        deg -= 360.0;
    }
    return deg;
}

static void fmt_lat(double deg, char *out, size_t cap, char *ns)
{
    out[0] = '\0';
    *ns = 'N';
    if (missing(deg) || deg > 90.0 || deg < -90.0) {
        return;
    }
    *ns = deg < 0.0 ? 'S' : 'N';
    const double a = fabs(deg);
    const int d = static_cast<int>(a);
    snprintf(out, cap, "%02d%07.4f", d, (a - d) * 60.0);
}

static void fmt_lon(double deg, char *out, size_t cap, char *ew)
{
    out[0] = '\0';
    *ew = 'E';
    if (missing(deg) || deg > 180.0 || deg < -180.0) {
        return;
    }
    *ew = deg < 0.0 ? 'W' : 'E';
    const double a = fabs(deg);
    const int d = static_cast<int>(a);
    snprintf(out, cap, "%03d%07.4f", d, (a - d) * 60.0);
}

static void civil_from_days(long z, int *year, int *month, int *day)
{
    z += 719468;
    const long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = static_cast<int>(yoe) + static_cast<int>(era * 400);
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    y += (m <= 2);
    *year = y;
    *month = static_cast<int>(m);
    *day = static_cast<int>(d);
}

struct FixCache {
    bool have_ll;
    bool have_sog;
    bool have_time;
    bool have_alt;
    double lat;
    double lon;
    double sog_kn;
    double cog_deg;
    double alt_m;
    double hdop;
    double geoid_m;
    uint16_t days;
    double seconds;
    int sats;
    int quality;
    uint32_t last_ms;
};

static FixCache g_fix = {};
static uint32_t g_depth_ms = 0;
static uint32_t g_wind_ms = 0;
static uint32_t g_hdg_ms = 0;
static uint32_t g_stw_ms = 0;

static int gga_quality(tN2kGNSSmethod method)
{
    switch (method) {
    case N2kGNSSm_GNSSfix:
        return 1;
    case N2kGNSSm_DGNSS:
        return 2;
    case N2kGNSSm_PreciseGNSS:
        return 3;
    case N2kGNSSm_RTKFixed:
        return 4;
    case N2kGNSSm_RTKFloat:
        return 5;
    default:
        return 0;
    }
}

static void note_time(uint16_t days, double seconds)
{
    if (!N2kIsNA(days)) {
        g_fix.days = days;
        g_fix.have_time = true;
    }
    if (!missing(seconds) && seconds >= 0.0 && seconds < 86400.0) {
        g_fix.seconds = seconds;
        g_fix.have_time = true;
    }
}

static void emit_ais(const AisBits &bits, tN2kAISTransceiverInformation info)
{
    const bool own = info == N2kaischannel_A_VDL_transmission ||
                     info == N2kaischannel_B_VDL_transmission ||
                     info == N2kaisown_information_not_broadcast;
    const char channel = (info == N2kaischannel_B_VDL_reception ||
                          info == N2kaischannel_B_VDL_transmission)
                             ? 'B'
                             : 'A';
    char lines[3][84];
    const int n = ais_finish(bits, channel, own, lines, 3);
    for (int i = 0; i < n; ++i) {
        nmea_feed_line(lines[i]);
    }
}

static int ais_sog(double ms)
{
    if (missing(ms) || ms < 0.0) {
        return 1023;
    }
    int v = static_cast<int>(lround(ms * kMsToKn * 10.0));
    if (v > 1022) {
        v = 1022;
    }
    return v;
}

static int ais_cog(double rad)
{
    if (missing(rad)) {
        return 3600;
    }
    int v = static_cast<int>(lround(wrap360(rad * kRadToDeg) * 10.0));
    if (v >= 3600) {
        v = 3599;
    }
    return v;
}

static int ais_hdg(double rad)
{
    if (missing(rad)) {
        return 511;
    }
    const int v = static_cast<int>(lround(rad * kRadToDeg));
    if (v < 0 || v >= 360) {
        return 511;
    }
    return v;
}

static int ais_lat(double deg)
{
    if (missing(deg) || deg > 90.0 || deg < -90.0) {
        return 0x3412140;
    }
    return static_cast<int>(lround(deg * 600000.0));
}

static int ais_lon(double deg)
{
    if (missing(deg) || deg > 180.0 || deg < -180.0) {
        return 0x6791AC0;
    }
    return static_cast<int>(lround(deg * 600000.0));
}

static int ais_rot(double rad_s)
{
    if (missing(rad_s)) {
        return -128;
    }
    const double deg_min = rad_s * kRadToDeg * 60.0;
    int v = static_cast<int>(lround(4.733 * sqrt(fabs(deg_min))));
    if (deg_min < 0.0) {
        v = -v;
    }
    if (v > 126) {
        return 127;
    }
    if (v < -126) {
        return -127;
    }
    return v;
}

static uint8_t ais_sec(uint8_t seconds)
{
    if (N2kIsNA(seconds) || seconds > 63) {
        return 60;
    }
    return seconds;
}

static void dims_from_ref(double length_m, double beam_m, double stbd_m, double bow_m,
                          int *bow, int *stern, int *port, int *stbd)
{
    auto clamp_i = [](double v, int lo, int hi) {
        if (missing(v)) {
            return 0;
        }
        int n = static_cast<int>(lround(v));
        if (n < lo) {
            n = lo;
        }
        if (n > hi) {
            n = hi;
        }
        return n;
    };
    *bow = clamp_i(bow_m, 0, 511);
    const double stern_m = (!missing(length_m) && !missing(bow_m)) ? (length_m - bow_m) : 0.0;
    *stern = clamp_i(stern_m, 0, 511);
    *stbd = clamp_i(stbd_m, 0, 63);
    const double port_m = (!missing(beam_m) && !missing(stbd_m)) ? (beam_m - stbd_m) : 0.0;
    *port = clamp_i(port_m, 0, 63);
}

static void handle_class_a_pos(const tN2kMsg &msg)
{
    uint8_t id = 1;
    tN2kAISRepeat repeat = N2kaisr_Initial;
    uint32_t mmsi = 0;
    double lat = 0, lon = 0, cog = 0, sog = 0, hdg = 0, rot = 0;
    bool acc = false, raim = false;
    uint8_t sec = 60;
    tN2kAISNavStatus nav = N2kaisns_Under_Way_Motoring;
    tN2kAISTransceiverInformation info = N2kaischannel_A_VDL_reception;
    uint8_t sid = 0;
    if (!ParseN2kPGN129038(msg, id, repeat, mmsi, lat, lon, acc, raim, sec, cog, sog, hdg, rot, nav,
                           info, sid)) {
        return;
    }
    if (id < 1 || id > 3) {
        id = 1;
    }
    AisBits bits;
    bits.clear();
    bits.put(id, 6);
    bits.put(static_cast<uint32_t>(repeat) & 3u, 2);
    bits.put(mmsi & 0x3fffffffu, 30);
    bits.put(static_cast<uint32_t>(nav) & 0xfu, 4);
    bits.put_signed(ais_rot(rot), 8);
    bits.put(static_cast<uint32_t>(ais_sog(sog)), 10);
    bits.put(acc ? 1u : 0u, 1);
    bits.put_signed(ais_lon(lon), 28);
    bits.put_signed(ais_lat(lat), 27);
    bits.put(static_cast<uint32_t>(ais_cog(cog)), 12);
    bits.put(static_cast<uint32_t>(ais_hdg(hdg)), 9);
    bits.put(ais_sec(sec), 6);
    bits.put(0, 2);
    bits.put(0, 3);
    bits.put(raim ? 1u : 0u, 1);
    bits.put(0, 19);
    emit_ais(bits, info);
}

static void handle_class_b_pos(const tN2kMsg &msg)
{
    uint8_t id = 18;
    tN2kAISRepeat repeat = N2kaisr_Initial;
    uint32_t mmsi = 0;
    double lat = 0, lon = 0, cog = 0, sog = 0, hdg = 0;
    bool acc = false, raim = false, display = false, dsc = false, band = false, msg22 = false;
    bool state = false;
    uint8_t sec = 60, sid = 0;
    tN2kAISTransceiverInformation info = N2kaischannel_A_VDL_reception;
    tN2kAISUnit unit = N2kaisunit_ClassB_CS;
    tN2kAISMode mode = N2kaismode_Autonomous;
    if (!ParseN2kPGN129039(msg, id, repeat, mmsi, lat, lon, acc, raim, sec, cog, sog, info, hdg, unit,
                           display, dsc, band, msg22, mode, state, sid)) {
        return;
    }
    AisBits bits;
    bits.clear();
    bits.put(18, 6);
    bits.put(static_cast<uint32_t>(repeat) & 3u, 2);
    bits.put(mmsi & 0x3fffffffu, 30);
    bits.put(0, 8);
    bits.put(static_cast<uint32_t>(ais_sog(sog)), 10);
    bits.put(acc ? 1u : 0u, 1);
    bits.put_signed(ais_lon(lon), 28);
    bits.put_signed(ais_lat(lat), 27);
    bits.put(static_cast<uint32_t>(ais_cog(cog)), 12);
    bits.put(static_cast<uint32_t>(ais_hdg(hdg)), 9);
    bits.put(ais_sec(sec), 6);
    bits.put(0, 2);
    bits.put(unit == N2kaisunit_ClassB_CS ? 1u : 0u, 1);
    bits.put(display ? 1u : 0u, 1);
    bits.put(dsc ? 1u : 0u, 1);
    bits.put(band ? 1u : 0u, 1);
    bits.put(msg22 ? 1u : 0u, 1);
    bits.put(mode == N2kaismode_Assigned ? 1u : 0u, 1);
    bits.put(raim ? 1u : 0u, 1);
    bits.put(state ? 1u : 0u, 1);
    bits.put(0, 19);
    emit_ais(bits, info);
}

/* PGN 129040 is not in the NMEA2000 library. Layout follows canboat's
 * AIS Class B Extended Position Report (fast packet, LSB-first). */
static void handle_class_b_ext(const tN2kMsg &msg)
{
    /* 20-byte name ends at byte 52. A shorter fast packet is not a report. */
    if (msg.PGN != 129040L || msg.DataLen < 52) {
        return;
    }
    int index = 0;
    const unsigned char head = msg.GetByte(index);
    const uint8_t repeat = (head >> 6) & 0x03;
    const uint32_t mmsi = msg.Get4ByteUInt(index);
    const double lon = msg.Get4ByteDouble(1e-07, index);
    const double lat = msg.Get4ByteDouble(1e-07, index);
    const unsigned char flags = msg.GetByte(index);
    const bool acc = flags & 0x01;
    const bool raim = (flags >> 1) & 0x01;
    const uint8_t sec = (flags >> 2) & 0x3f;
    const double cog = msg.Get2ByteUDouble(1e-04, index);
    const double sog = msg.Get2ByteUDouble(0.01, index);
    (void)msg.GetByte(index);
    (void)msg.GetByte(index);
    const uint8_t ship = msg.GetByte(index);
    const double hdg = msg.Get2ByteUDouble(1e-04, index);
    const unsigned char gnss_b = msg.GetByte(index);
    const uint8_t epfd = (gnss_b >> 4) & 0x0f;
    const double length = msg.Get2ByteUDouble(0.1, index);
    const double beam = msg.Get2ByteUDouble(0.1, index);
    const double stbd_m = msg.Get2ByteUDouble(0.1, index);
    const double bow_m = msg.Get2ByteUDouble(0.1, index);
    char name[21];
    memset(name, 0, sizeof(name));
    for (int i = 0; i < 20 && index < msg.DataLen; ++i) {
        const unsigned char c = msg.GetByte(index);
        name[i] = (c == 0xff) ? '@' : static_cast<char>(c);
    }
    tN2kAISTransceiverInformation info = N2kaischannel_A_VDL_reception;
    if (index + 1 < msg.DataLen) {
        const unsigned char b0 = msg.GetByte(index);
        const unsigned char b1 = msg.GetByte(index);
        const unsigned info_bits = ((b1 & 0x07) << 2) | ((b0 >> 6) & 0x03);
        info = static_cast<tN2kAISTransceiverInformation>(info_bits & 0x1f);
    }

    int bow = 0, stern = 0, port = 0, stbd = 0;
    dims_from_ref(length, beam, stbd_m, bow_m, &bow, &stern, &port, &stbd);

    AisBits bits;
    bits.clear();
    bits.put(19, 6);
    bits.put(repeat, 2);
    bits.put(mmsi & 0x3fffffffu, 30);
    bits.put(0, 8);
    bits.put(static_cast<uint32_t>(ais_sog(sog)), 10);
    bits.put(acc ? 1u : 0u, 1);
    bits.put_signed(ais_lon(lon), 28);
    bits.put_signed(ais_lat(lat), 27);
    bits.put(static_cast<uint32_t>(ais_cog(cog)), 12);
    bits.put(static_cast<uint32_t>(ais_hdg(hdg)), 9);
    bits.put(ais_sec(sec), 6);
    bits.put(0, 4);
    bits.put_text(name, 20);
    bits.put(ship, 8);
    bits.put(static_cast<uint32_t>(bow), 9);
    bits.put(static_cast<uint32_t>(stern), 9);
    bits.put(static_cast<uint32_t>(port), 6);
    bits.put(static_cast<uint32_t>(stbd), 6);
    bits.put(epfd & 0x0f, 4);
    bits.put(raim ? 1u : 0u, 1);
    bits.put(0, 1);
    bits.put(0, 1);
    bits.put(0, 4);
    emit_ais(bits, info);
}

static void handle_class_a_static(const tN2kMsg &msg)
{
    uint8_t id = 5, ship = 0, sid = 0;
    tN2kAISRepeat repeat = N2kaisr_Initial;
    uint32_t mmsi = 0, imo = 0;
    char callsign[8] = {};
    char name[21] = {};
    char dest[21] = {};
    double length = 0, beam = 0, stbd_m = 0, bow_m = 0, eta_time = 0, draught = 0;
    uint16_t eta_date = N2kUInt16NA;
    tN2kAISVersion ver = N2kaisv_ITU_R_M_1371_1;
    tN2kGNSStype gnss = N2kGNSSt_GPS;
    tN2kAISDTE dte = N2kaisdte_NotReady;
    tN2kAISTransceiverInformation info = N2kaischannel_A_VDL_reception;
    if (!ParseN2kPGN129794(msg, id, repeat, mmsi, imo, callsign, sizeof(callsign), name, sizeof(name),
                           ship, length, beam, stbd_m, bow_m, eta_date, eta_time, draught, dest,
                           sizeof(dest), ver, gnss, dte, info, sid)) {
        return;
    }
    int bow = 0, stern = 0, port = 0, stbd = 0;
    dims_from_ref(length, beam, stbd_m, bow_m, &bow, &stern, &port, &stbd);
    int month = 0, day = 0, hour = 24, minute = 60;
    if (!N2kIsNA(eta_date)) {
        int year = 0;
        civil_from_days(eta_date, &year, &month, &day);
        (void)year;
    }
    if (!missing(eta_time) && eta_time >= 0.0 && eta_time < 86400.0) {
        const int sec = static_cast<int>(eta_time);
        hour = sec / 3600;
        minute = (sec / 60) % 60;
    }
    int draught_dm = 0;
    if (!missing(draught) && draught >= 0.0) {
        draught_dm = static_cast<int>(lround(draught * 10.0));
        if (draught_dm > 255) {
            draught_dm = 255;
        }
    }
    AisBits bits;
    bits.clear();
    bits.put(5, 6);
    bits.put(static_cast<uint32_t>(repeat) & 3u, 2);
    bits.put(mmsi & 0x3fffffffu, 30);
    bits.put(static_cast<uint32_t>(ver) & 3u, 2);
    bits.put(N2kIsNA(imo) ? 0u : (imo & 0x3fffffffu), 30);
    bits.put_text(callsign, 7);
    bits.put_text(name, 20);
    bits.put(ship, 8);
    bits.put(static_cast<uint32_t>(bow), 9);
    bits.put(static_cast<uint32_t>(stern), 9);
    bits.put(static_cast<uint32_t>(port), 6);
    bits.put(static_cast<uint32_t>(stbd), 6);
    bits.put(static_cast<uint32_t>(gnss) & 0x0fu, 4);
    bits.put(static_cast<uint32_t>(month) & 0x0fu, 4);
    bits.put(static_cast<uint32_t>(day) & 0x1fu, 5);
    bits.put(static_cast<uint32_t>(hour) & 0x1fu, 5);
    bits.put(static_cast<uint32_t>(minute) & 0x3fu, 6);
    bits.put(static_cast<uint32_t>(draught_dm), 8);
    bits.put_text(dest, 20);
    bits.put(dte == N2kaisdte_Ready ? 0u : 1u, 1);
    bits.put(0, 1);
    emit_ais(bits, info);
}

static void handle_class_b_static_a(const tN2kMsg &msg)
{
    uint8_t id = 24, sid = 0;
    tN2kAISRepeat repeat = N2kaisr_Initial;
    uint32_t mmsi = 0;
    char name[21] = {};
    tN2kAISTransceiverInformation info = N2kaischannel_A_VDL_reception;
    if (!ParseN2kPGN129809(msg, id, repeat, mmsi, name, sizeof(name), info, sid)) {
        return;
    }
    AisBits bits;
    bits.clear();
    bits.put(24, 6);
    bits.put(static_cast<uint32_t>(repeat) & 3u, 2);
    bits.put(mmsi & 0x3fffffffu, 30);
    bits.put(0, 2);
    bits.put_text(name, 20);
    emit_ais(bits, info);
}

static void handle_class_b_static_b(const tN2kMsg &msg)
{
    uint8_t id = 24, ship = 0, sid = 0;
    tN2kAISRepeat repeat = N2kaisr_Initial;
    uint32_t mmsi = 0, mother = 0;
    char vendor[8] = {};
    char callsign[8] = {};
    double length = 0, beam = 0, stbd_m = 0, bow_m = 0;
    tN2kAISTransceiverInformation info = N2kaischannel_A_VDL_reception;
    if (!ParseN2kPGN129810(msg, id, repeat, mmsi, ship, vendor, sizeof(vendor), callsign,
                           sizeof(callsign), length, beam, stbd_m, bow_m, mother, info, sid)) {
        return;
    }
    int bow = 0, stern = 0, port = 0, stbd = 0;
    dims_from_ref(length, beam, stbd_m, bow_m, &bow, &stern, &port, &stbd);
    (void)mother;
    AisBits bits;
    bits.clear();
    bits.put(24, 6);
    bits.put(static_cast<uint32_t>(repeat) & 3u, 2);
    bits.put(mmsi & 0x3fffffffu, 30);
    bits.put(1, 2);
    bits.put(ship, 8);
    bits.put_text(vendor, 7);
    bits.put_text(callsign, 7);
    bits.put(static_cast<uint32_t>(bow), 9);
    bits.put(static_cast<uint32_t>(stern), 9);
    bits.put(static_cast<uint32_t>(port), 6);
    bits.put(static_cast<uint32_t>(stbd), 6);
    bits.put(0, 6);
    emit_ais(bits, info);
}

/* 129808 has no stable parser in the NMEA2000 library we ship, and a wrong
 * $CDDSC format byte would look like a distress call. Emit the expanded
 * sentence with the payload hex so the phone console shows the call. */
static void handle_dsc(const tN2kMsg &msg)
{
    if (msg.PGN != 129808L || msg.DataLen == 0) {
        return;
    }
    char hex[49];
    const int n = msg.DataLen < 16 ? msg.DataLen : 16;
    for (int i = 0; i < n; ++i) {
        snprintf(hex + i * 2, 3, "%02X", msg.Data[i]);
    }
    char line[96];
    snprintf(line, sizeof(line), "$CDDSE,1,1,A,%s", hex);
    emit_sentence(line, sizeof(line));
}

static void handle_depth(const tN2kMsg &msg)
{
    const uint32_t now = millis();
    if (g_depth_ms != 0 && (now - g_depth_ms) < kEmitMs) {
        return;
    }
    unsigned char sid = 0;
    double depth = 0, offset = 0, range = 0;
    if (!ParseN2kPGN128267(msg, sid, depth, offset, range)) {
        return;
    }
    (void)range;
    if (missing(depth)) {
        return;
    }
    if (missing(offset)) {
        offset = 0.0;
    }
    g_depth_ms = now;
    char line[84];
    snprintf(line, sizeof(line), "$SDDPT,%.1f,%.1f,", depth, offset);
    emit_sentence(line, sizeof(line));
}

static void handle_wind(const tN2kMsg &msg)
{
    const uint32_t now = millis();
    if (g_wind_ms != 0 && (now - g_wind_ms) < kEmitMs) {
        return;
    }
    unsigned char sid = 0;
    double speed = 0, angle = 0;
    tN2kWindReference ref = N2kWind_Apparent;
    if (!ParseN2kPGN130306(msg, sid, speed, angle, ref)) {
        return;
    }
    if (missing(speed) || missing(angle)) {
        return;
    }
    const char kind = (ref == N2kWind_Apparent) ? 'R' : 'T';
    if (ref == N2kWind_Error || ref == N2kWind_Unavailable) {
        return;
    }
    g_wind_ms = now;
    char line[84];
    snprintf(line, sizeof(line), "$WIMWV,%.1f,%c,%.1f,N,A", wrap360(angle * kRadToDeg), kind,
             speed * kMsToKn);
    emit_sentence(line, sizeof(line));
}

static void handle_heading(const tN2kMsg &msg)
{
    const uint32_t now = millis();
    if (g_hdg_ms != 0 && (now - g_hdg_ms) < kEmitMs) {
        return;
    }
    unsigned char sid = 0;
    double heading = 0, dev = 0, var = 0;
    tN2kHeadingReference ref = N2khr_true;
    if (!ParseN2kPGN127250(msg, sid, heading, dev, var, ref)) {
        return;
    }
    if (missing(heading) || ref == N2khr_Unavailable || ref == N2khr_error) {
        return;
    }
    g_hdg_ms = now;
    const double deg = wrap360(heading * kRadToDeg);
    char line[84];
    if (ref == N2khr_magnetic) {
        char dev_s[16] = "";
        char var_s[16] = "";
        char dev_dir = 'E';
        char var_dir = 'E';
        if (!missing(dev)) {
            dev_dir = dev < 0.0 ? 'W' : 'E';
            snprintf(dev_s, sizeof(dev_s), "%.1f", fabs(dev * kRadToDeg));
        }
        if (!missing(var)) {
            var_dir = var < 0.0 ? 'W' : 'E';
            snprintf(var_s, sizeof(var_s), "%.1f", fabs(var * kRadToDeg));
        }
        snprintf(line, sizeof(line), "$HCHDG,%.1f,%s,%c,%s,%c", deg, dev_s, dev_dir, var_s, var_dir);
    } else {
        snprintf(line, sizeof(line), "$HEHDT,%.1f,T", deg);
    }
    emit_sentence(line, sizeof(line));
}

static void handle_stw(const tN2kMsg &msg)
{
    const uint32_t now = millis();
    if (g_stw_ms != 0 && (now - g_stw_ms) < kEmitMs) {
        return;
    }
    unsigned char sid = 0;
    double water = 0, ground = 0;
    tN2kSpeedWaterReferenceType kind = N2kSWRT_Paddle_wheel;
    if (!ParseN2kPGN128259(msg, sid, water, ground, kind)) {
        return;
    }
    (void)ground;
    (void)kind;
    if (missing(water)) {
        return;
    }
    g_stw_ms = now;
    const double kn = water * kMsToKn;
    const double kmh = water * 3.6;
    char line[84];
    snprintf(line, sizeof(line), "$VWVHW,,T,,M,%.1f,N,%.1f,K", kn, kmh);
    emit_sentence(line, sizeof(line));
}

static void handle_position(const tN2kMsg &msg)
{
    double lat = 0, lon = 0;
    if (!ParseN2kPGN129025(msg, lat, lon) || missing(lat) || missing(lon)) {
        return;
    }
    g_fix.lat = lat;
    g_fix.lon = lon;
    g_fix.have_ll = true;
}

static void handle_cogsog(const tN2kMsg &msg)
{
    unsigned char sid = 0;
    tN2kHeadingReference ref = N2khr_true;
    double cog = 0, sog = 0;
    if (!ParseN2kPGN129026(msg, sid, ref, cog, sog)) {
        return;
    }
    (void)ref;
    if (!missing(sog)) {
        g_fix.sog_kn = sog * kMsToKn;
        g_fix.have_sog = true;
    }
    if (!missing(cog)) {
        g_fix.cog_deg = wrap360(cog * kRadToDeg);
        g_fix.have_sog = true;
    }
}

static void handle_gnss(const tN2kMsg &msg)
{
    unsigned char sid = 0, sats = 0, nref = 0;
    uint16_t days = 0, station = 0;
    double seconds = 0, lat = 0, lon = 0, alt = 0, hdop = 0, pdop = 0, geoid = 0, age = 0;
    tN2kGNSStype kind = N2kGNSSt_GPS, ref_kind = N2kGNSSt_GPS;
    tN2kGNSSmethod method = N2kGNSSm_Unavailable;
    if (!ParseN2kPGN129029(msg, sid, days, seconds, lat, lon, alt, kind, method, sats, hdop, pdop, geoid,
                           nref, ref_kind, station, age)) {
        return;
    }
    (void)kind;
    (void)pdop;
    (void)nref;
    (void)ref_kind;
    (void)station;
    (void)age;
    if (!missing(lat) && !missing(lon)) {
        g_fix.lat = lat;
        g_fix.lon = lon;
        g_fix.have_ll = true;
    }
    note_time(days, seconds);
    if (!missing(alt)) {
        g_fix.alt_m = alt;
        g_fix.have_alt = true;
    }
    if (!missing(hdop)) {
        g_fix.hdop = hdop;
    }
    if (!missing(geoid)) {
        g_fix.geoid_m = geoid;
    }
    g_fix.sats = N2kIsNA(sats) ? 0 : sats;
    g_fix.quality = gga_quality(method);
}

static void handle_time(const tN2kMsg &msg)
{
    unsigned char sid = 0;
    uint16_t days = 0;
    double seconds = 0;
    tN2kTimeSource src = N2ktimes_GPS;
    if (!ParseN2kPGN126992(msg, sid, days, seconds, src)) {
        return;
    }
    (void)src;
    note_time(days, seconds);
}

void n2k_to_0183_handle(const tN2kMsg &msg)
{
    switch (msg.PGN) {
    case 126992L:
        handle_time(msg);
        break;
    case 127250L:
        handle_heading(msg);
        break;
    case 128259L:
        handle_stw(msg);
        break;
    case 128267L:
        handle_depth(msg);
        break;
    case 129025L:
        handle_position(msg);
        break;
    case 129026L:
        handle_cogsog(msg);
        break;
    case 129029L:
        handle_gnss(msg);
        break;
    case 129038L:
        handle_class_a_pos(msg);
        break;
    case 129039L:
        handle_class_b_pos(msg);
        break;
    case 129040L:
        handle_class_b_ext(msg);
        break;
    case 129794L:
        handle_class_a_static(msg);
        break;
    case 129808L:
        handle_dsc(msg);
        break;
    case 129809L:
        handle_class_b_static_a(msg);
        break;
    case 129810L:
        handle_class_b_static_b(msg);
        break;
    case 130306L:
        handle_wind(msg);
        break;
    default:
        break;
    }
}

void n2k_to_0183_poll(void)
{
    if (!g_fix.have_ll && !g_fix.have_sog) {
        return;
    }
    const uint32_t now = millis();
    if (g_fix.last_ms != 0 && (now - g_fix.last_ms) < kEmitMs) {
        return;
    }
    g_fix.last_ms = now;

    char hhmmss[16] = "";
    char date[8] = "";
    if (g_fix.have_time) {
        const int sec = static_cast<int>(g_fix.seconds);
        if (sec >= 0 && sec < 86400) {
            snprintf(hhmmss, sizeof(hhmmss), "%02d%02d%02d.00", sec / 3600, (sec / 60) % 60, sec % 60);
        }
        if (!N2kIsNA(g_fix.days)) {
            int year = 0, month = 0, day = 0;
            civil_from_days(g_fix.days, &year, &month, &day);
            snprintf(date, sizeof(date), "%02d%02d%02d", day, month, year % 100);
        }
    }

    char lat[16], lon[16];
    char ns = 'N', ew = 'E';
    fmt_lat(g_fix.lat, lat, sizeof(lat), &ns);
    fmt_lon(g_fix.lon, lon, sizeof(lon), &ew);
    const bool pos = lat[0] != '\0' && lon[0] != '\0';
    const char status = (pos && g_fix.quality != 0) || pos ? 'A' : 'V';

    char line[96];
    if (pos) {
        snprintf(line, sizeof(line), "$GPRMC,%s,%c,%s,%c,%s,%c,%.1f,%.1f,%s,,,A", hhmmss, status, lat, ns,
                 lon, ew, g_fix.have_sog ? g_fix.sog_kn : 0.0, g_fix.have_sog ? g_fix.cog_deg : 0.0, date);
        emit_sentence(line, sizeof(line));

        snprintf(line, sizeof(line), "$GPGGA,%s,%s,%c,%s,%c,%d,%02d,%.1f,%.1f,M,%.1f,M,,", hhmmss, lat, ns,
                 lon, ew, g_fix.quality > 0 ? g_fix.quality : 1, g_fix.sats,
                 g_fix.hdop > 0.0 ? g_fix.hdop : 1.0, g_fix.have_alt ? g_fix.alt_m : 0.0, g_fix.geoid_m);
        emit_sentence(line, sizeof(line));

        snprintf(line, sizeof(line), "$GPGLL,%s,%c,%s,%c,%s,%c", lat, ns, lon, ew, hhmmss, status);
        emit_sentence(line, sizeof(line));
    }
    if (g_fix.have_sog) {
        snprintf(line, sizeof(line), "$GPVTG,%.1f,T,,M,%.1f,N,%.1f,K,A", g_fix.cog_deg, g_fix.sog_kn,
                 g_fix.sog_kn * 1.852);
        emit_sentence(line, sizeof(line));
    }
}
