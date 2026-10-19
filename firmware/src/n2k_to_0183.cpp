#include "n2k_to_0183.h"

#include "ais0183.h"
#include "nmea.h"

#include <Arduino.h>
#include <N2kMessages.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

/* PGN → NMEA 0183. Phone-facing sentences stay 0183 so Navionics and the
 * LMYC app do not change.
 *
 *  129025 position rapid          RMC/GLL cache
 *  129026 COG/SOG rapid           RMC/VTG cache
 *  129029 GNSS position           RMC + GGA + GLL (GGA only with 129029)
 *  126992 system time             RMC time
 *  128267 depth                   DPT
 *  128259 water speed             VHW
 *  130306 wind                    MWV (apparent / boat-true), MWD (ground)
 *  127250 heading                 HDG or HDT
 *  129038 AIS Class A position    !AIVDM/!AIVDO type 1
 *  129039 AIS Class B position    type 18
 *  129040 AIS Class B extended    type 19
 *  129794 AIS Class A static      type 5
 *  129809 AIS Class B static A    type 24 part A
 *  129810 AIS Class B static B    type 24 part B
 *  129808 DSC call                $PLMYC,DSC,<hex> (raw, not a synthesized distress)
 *
 * GNSS, depth, wind, and heading are emitted at 1 Hz. AIS goes out as received.
 * GNSS older than 3 s is dropped rather than repeated.
 * v1 does not transmit on NMEA 2000.
 */

static constexpr double kMsToKn = 1.9438444924406046;
static constexpr double kRadToDeg = 180.0 / 3.14159265358979323846;
static constexpr uint32_t kEmitMs = 1000;
/* Cached GNSS older than this is not re-sent. Matches the 3 s AUTO quiet
 * window, so a dead GPS never keeps repeating its last fix. */
static constexpr uint32_t kStaleMs = 3000;

static bool fresh(uint32_t stamp_ms, uint32_t now)
{
    return stamp_ms != 0 && (now - stamp_ms) <= kStaleMs;
}

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

/* ddmm.mmmm / dddmm.mmmm. Minutes are rounded to 1/10000 first so
 * 59.99996' carries into the degree instead of printing "60.0000". */
static void fmt_ddmm(double a, int deg_digits, char *out, size_t cap)
{
    long ticks = lround(a * 600000.0); /* 1/10000 minute */
    const long deg = ticks / 600000;
    ticks -= deg * 600000;
    snprintf(out, cap, "%0*ld%02ld.%04ld", deg_digits, deg, ticks / 10000, ticks % 10000);
}

static void fmt_lat(double deg, char *out, size_t cap, char *ns)
{
    out[0] = '\0';
    *ns = 'N';
    if (missing(deg) || deg > 90.0 || deg < -90.0) {
        return;
    }
    *ns = deg < 0.0 ? 'S' : 'N';
    fmt_ddmm(fabs(deg), 2, out, cap);
}

static void fmt_lon(double deg, char *out, size_t cap, char *ew)
{
    out[0] = '\0';
    *ew = 'E';
    if (missing(deg) || deg > 180.0 || deg < -180.0) {
        return;
    }
    *ew = deg < 0.0 ? 'W' : 'E';
    fmt_ddmm(fabs(deg), 3, out, cap);
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
    bool have_gnss; /* 129029 seen: quality / sats / HDOP are real */
    bool have_hdop;
    bool have_geoid;
    uint32_t ll_ms;
    uint32_t sog_ms;
    uint32_t time_ms;
    uint32_t gnss_ms;
    uint32_t last_ms;
};

static FixCache g_fix = {};
static uint32_t g_depth_ms = 0;
static uint32_t g_wind_ms = 0;
static uint32_t g_hdg_ms = 0;
static uint32_t g_stw_ms = 0;

/* Eight newest (PGN, source) pairs for the cabin screen. A type this
 * file does not convert is still stored, named "other", so a quiet tile
 * can be told apart from a bus that is talking. */
static constexpr int kSeenMax = 8;

struct SeenPgn {
    unsigned long pgn;
    unsigned char src;
    uint32_t count;
    uint32_t last_ms;
};

static SeenPgn g_seen[kSeenMax];
static int g_seen_n = 0;

static const char *pgn_name(unsigned long pgn)
{
    switch (pgn) {
    case 59392UL:
        return "ISO Ack";
    case 59904UL:
        return "ISO Req";
    case 60928UL:
        return "Addr claim";
    case 126992UL:
        return "Time";
    case 126993UL:
        return "Heartbeat";
    case 126996UL:
        return "Product";
    case 126998UL:
        return "Config";
    case 127245UL:
        return "Rudder";
    case 127250UL:
        return "Heading";
    case 127251UL:
        return "Rate";
    case 127257UL:
        return "Attitude";
    case 127488UL:
        return "RPM";
    case 127489UL:
        return "Engine";
    case 127505UL:
        return "Fluid";
    case 128259UL:
        return "Water spd";
    case 128267UL:
        return "Depth";
    case 128275UL:
        return "Log";
    case 129025UL:
        return "Position";
    case 129026UL:
        return "COG/SOG";
    case 129029UL:
        return "GNSS";
    case 129033UL:
        return "Time/date";
    case 129038UL:
        return "AIS A";
    case 129039UL:
        return "AIS B";
    case 129040UL:
        return "AIS B ext";
    case 129283UL:
        return "XTE";
    case 129284UL:
        return "Nav";
    case 129285UL:
        return "Route";
    case 129291UL:
        return "Set/drift";
    case 129539UL:
        return "GNSS DOP";
    case 129540UL:
        return "GNSS sats";
    case 129794UL:
        return "AIS static";
    case 129808UL:
        return "DSC";
    case 129809UL:
        return "AIS B A";
    case 129810UL:
        return "AIS B B";
    case 130306UL:
        return "Wind";
    case 130310UL:
        return "Env";
    case 130311UL:
        return "Environ";
    case 130312UL:
        return "Temp";
    case 130314UL:
        return "Pressure";
    case 130316UL:
        return "Temp ext";
    default:
        return "other";
    }
}

void n2k_pgn_reset(void)
{
    memset(g_seen, 0, sizeof(g_seen));
    g_seen_n = 0;
}

static void note_seen(const tN2kMsg &msg)
{
    const uint32_t now = millis();
    int slot = -1;
    for (int i = 0; i < g_seen_n; ++i) {
        if (g_seen[i].pgn == msg.PGN && g_seen[i].src == msg.Source) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        if (g_seen_n < kSeenMax) {
            slot = g_seen_n++;
        } else {
            slot = kSeenMax - 1;
        }
        g_seen[slot].pgn = msg.PGN;
        g_seen[slot].src = msg.Source;
        g_seen[slot].count = 0;
    }
    g_seen[slot].count++;
    g_seen[slot].last_ms = now;
    const SeenPgn kept = g_seen[slot];
    while (slot > 0) {
        g_seen[slot] = g_seen[slot - 1];
        --slot;
    }
    g_seen[0] = kept;
}

void n2k_pgn_lines(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    out[0] = '\0';
    const uint32_t now = millis();
    size_t used = 0;
    for (int i = 0; i < g_seen_n; ++i) {
        const SeenPgn &s = g_seen[i];
        uint32_t age_s = (now - s.last_ms) / 1000UL;
        if (age_s > 9999UL) {
            age_s = 9999UL;
        }
        uint32_t n = s.count;
        if (n > 99999UL) {
            n = 99999UL;
        }
        char line[64];
        snprintf(line, sizeof(line), "%lu %s  src %u  %lus  %lu", s.pgn, pgn_name(s.pgn),
                 static_cast<unsigned>(s.src), static_cast<unsigned long>(age_s),
                 static_cast<unsigned long>(n));
        const size_t len = strlen(line);
        const size_t extra = (used > 0 ? 1 : 0) + len;
        if (used + extra + 1 > cap) {
            break;
        }
        if (used > 0) {
            out[used++] = '\n';
        }
        memcpy(out + used, line, len);
        used += len;
        out[used] = '\0';
    }
}

void n2k_pgn_names(char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    if (g_seen_n == 0) {
        snprintf(out, cap, "none");
        return;
    }
    size_t used = 0;
    out[0] = '\0';
    for (int i = 0; i < g_seen_n; ++i) {
        const char *name = pgn_name(g_seen[i].pgn);
        char piece[20];
        if (strcmp(name, "other") == 0) {
            snprintf(piece, sizeof(piece), "%lu", g_seen[i].pgn);
        } else {
            snprintf(piece, sizeof(piece), "%s", name);
        }
        const size_t piece_len = strlen(piece);
        const size_t extra = (used > 0 ? 2 : 0) + piece_len;
        if (used + extra + 1 > cap) {
            break;
        }
        if (used > 0) {
            out[used++] = ',';
            out[used++] = ' ';
        }
        memcpy(out + used, piece, piece_len);
        used += piece_len;
        out[used] = '\0';
    }
}

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
        g_fix.time_ms = millis();
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
    /* 359.96 rounds to 3600, which is 0.0, not 359.9. */
    return static_cast<int>(lround(wrap360(rad * kRadToDeg) * 10.0)) % 3600;
}

static int ais_hdg(double rad)
{
    if (missing(rad)) {
        return 511;
    }
    const double deg = rad * kRadToDeg;
    if (deg < 0.0 || deg >= 360.0) {
        return 511;
    }
    return static_cast<int>(lround(deg)) % 360;
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
    bits.put(0, 19); /* radio status: 1 selector bit above + 19 = 168 bits */
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
 * $CDDSC format byte would look like a distress call. A standard $CDDSE
 * with hex in its MMSI field would also be misread by chart plotters, so
 * the raw payload goes out as a proprietary $PLMYC,DSC sentence instead.
 * Third-party apps ignore it; the phone console can still show it. */
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
    snprintf(line, sizeof(line), "$PLMYC,DSC,%s", hex);
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
    char line[84];
    const double deg = wrap360(angle * kRadToDeg);
    if (ref == N2kWind_True_North || ref == N2kWind_Magnetic) {
        /* Ground-referenced direction is a compass bearing, not an angle off
         * the bow. MWV would show it as relative wind, so send MWD. */
        g_wind_ms = now;
        if (ref == N2kWind_True_North) {
            snprintf(line, sizeof(line), "$WIMWD,%.1f,T,,M,%.1f,N,%.1f,M", deg, speed * kMsToKn, speed);
        } else {
            snprintf(line, sizeof(line), "$WIMWD,,T,%.1f,M,%.1f,N,%.1f,M", deg, speed * kMsToKn, speed);
        }
        emit_sentence(line, sizeof(line));
        return;
    }
    if (ref != N2kWind_Apparent && ref != N2kWind_True_boat && ref != N2kWind_True_water) {
        return;
    }
    const char kind = (ref == N2kWind_Apparent) ? 'R' : 'T';
    g_wind_ms = now;
    snprintf(line, sizeof(line), "$WIMWV,%.1f,%c,%.1f,N,A", deg, kind, speed * kMsToKn);
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
        /* Value and E/W are both empty when N2K says not available. */
        char dev_s[20] = ",";
        char var_s[20] = ",";
        if (!missing(dev)) {
            snprintf(dev_s, sizeof(dev_s), "%.1f,%c", fabs(dev * kRadToDeg), dev < 0.0 ? 'W' : 'E');
        }
        if (!missing(var)) {
            snprintf(var_s, sizeof(var_s), "%.1f,%c", fabs(var * kRadToDeg), var < 0.0 ? 'W' : 'E');
        }
        snprintf(line, sizeof(line), "$HCHDG,%.1f,%s,%s", deg, dev_s, var_s);
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
    g_fix.ll_ms = millis();
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
    if (missing(sog) || missing(cog)) {
        return;
    }
    g_fix.sog_kn = sog * kMsToKn;
    g_fix.cog_deg = wrap360(cog * kRadToDeg);
    g_fix.have_sog = true;
    g_fix.sog_ms = millis();
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
    const uint32_t now = millis();
    if (!missing(lat) && !missing(lon)) {
        g_fix.lat = lat;
        g_fix.lon = lon;
        g_fix.have_ll = true;
        g_fix.ll_ms = now;
    }
    note_time(days, seconds);
    /* Per message: a field that goes not-available must not keep its old value. */
    g_fix.have_alt = !missing(alt);
    if (g_fix.have_alt) {
        g_fix.alt_m = alt;
    }
    g_fix.have_hdop = !missing(hdop);
    if (g_fix.have_hdop) {
        g_fix.hdop = hdop;
    }
    g_fix.have_geoid = !missing(geoid);
    if (g_fix.have_geoid) {
        g_fix.geoid_m = geoid;
    }
    g_fix.sats = N2kIsNA(sats) ? 0 : sats;
    g_fix.quality = gga_quality(method);
    g_fix.have_gnss = true;
    g_fix.gnss_ms = now;
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

bool n2k_play_pgn(unsigned long pgn, uint8_t src, const uint8_t *data, size_t len)
{
    if (data == nullptr || len == 0 || len > static_cast<size_t>(tN2kMsg::MaxDataLen)) {
        return false;
    }
    tN2kMsg msg;
    msg.SetPGN(pgn);
    msg.Source = src;
    for (size_t i = 0; i < len; ++i) {
        msg.AddByte(data[i]);
    }
    n2k_to_0183_handle(msg);
    return true;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool n2k_play_line(const char *args)
{
    if (args == nullptr || strncasecmp(args, "PGN", 3) != 0) {
        return false;
    }
    const char *p = args + 3;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    char *end = nullptr;
    const unsigned long pgn = strtoul(p, &end, 10);
    if (end == p || pgn == 0 || pgn > 0x3ffffUL) {
        return false;
    }
    p = end;
    unsigned long src = 1;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (strncasecmp(p, "src=", 4) == 0) {
        p += 4;
        src = strtoul(p, &end, 10);
        if (end == p || src > 251UL) {
            return false;
        }
        p = end;
    }
    uint8_t data[tN2kMsg::MaxDataLen];
    int n = 0;
    int hi = -1;
    for (; *p != '\0'; ++p) {
        if (*p == ' ' || *p == '\t') {
            continue;
        }
        const int v = hex_nibble(*p);
        if (v < 0) {
            return false;
        }
        if (hi < 0) {
            hi = v;
            continue;
        }
        if (n >= tN2kMsg::MaxDataLen) {
            return false;
        }
        data[n++] = static_cast<uint8_t>((hi << 4) | v);
        hi = -1;
    }
    if (hi >= 0 || n == 0) {
        return false;
    }
    return n2k_play_pgn(pgn, static_cast<uint8_t>(src), data, static_cast<size_t>(n));
}

void n2k_play_demo(void)
{
    tN2kMsg msg;
    SetN2kPGN128267(msg, 1, 42.0, 0.0);
    msg.Source = 12;
    n2k_to_0183_handle(msg);

    SetN2kPGN129026(msg, 1, N2khr_true, 90.0 / kRadToDeg, 7.5 / kMsToKn);
    msg.Source = 35;
    n2k_to_0183_handle(msg);

    SetN2kPGN130306(msg, 1, 18.0 / kMsToKn, 40.0 / kRadToDeg, N2kWind_Apparent);
    msg.Source = 7;
    n2k_to_0183_handle(msg);

    SetN2kPGN127250(msg, 1, 45.0 / kRadToDeg, N2kDoubleNA, N2kDoubleNA, N2khr_true);
    msg.Source = 8;
    n2k_to_0183_handle(msg);

    const uint8_t other[] = {0xde, 0xad};
    n2k_play_pgn(130816UL, 4, other, sizeof(other));
    n2k_to_0183_poll();
}

void n2k_to_0183_handle(const tN2kMsg &msg)
{
    note_seen(msg);
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
    const uint32_t now = millis();
    const bool ll_ok = g_fix.have_ll && fresh(g_fix.ll_ms, now);
    const bool sog_ok = g_fix.have_sog && fresh(g_fix.sog_ms, now);
    if (!ll_ok && !sog_ok) {
        return;
    }
    if (g_fix.last_ms != 0 && (now - g_fix.last_ms) < kEmitMs) {
        return;
    }
    g_fix.last_ms = now;

    char hhmmss[16] = "";
    char date[8] = "";
    if (g_fix.have_time && fresh(g_fix.time_ms, now)) {
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

    char lat[16] = "", lon[16] = "";
    char ns = 'N', ew = 'E';
    if (ll_ok) {
        fmt_lat(g_fix.lat, lat, sizeof(lat), &ns);
        fmt_lon(g_fix.lon, lon, sizeof(lon), &ew);
    }
    const bool pos = lat[0] != '\0' && lon[0] != '\0';
    /* 129029 can say "no fix". 129025 alone carries no quality, so trust it. */
    const bool gnss_ok = g_fix.have_gnss && fresh(g_fix.gnss_ms, now);
    const bool valid = pos && (!gnss_ok || g_fix.quality != 0);
    const char status = valid ? 'A' : 'V';
    const char mode = valid ? 'A' : 'N';

    char line[96];
    char sog_s[12] = "", cog_s[12] = "";
    if (sog_ok) {
        snprintf(sog_s, sizeof(sog_s), "%.1f", g_fix.sog_kn);
        snprintf(cog_s, sizeof(cog_s), "%.1f", g_fix.cog_deg);
    }
    if (pos) {
        snprintf(line, sizeof(line), "$GPRMC,%s,%c,%s,%c,%s,%c,%s,%s,%s,,,%c", hhmmss, status, lat, ns,
                 lon, ew, sog_s, cog_s, date, mode);
        emit_sentence(line, sizeof(line));

        /* GGA only with a live 129029: quality, satellites, and altitude come from it. */
        if (gnss_ok) {
            char hdop_s[12] = "";
            char alt_s[16] = "";
            char geoid_s[16] = "";
            if (g_fix.have_hdop) {
                snprintf(hdop_s, sizeof(hdop_s), "%.1f", g_fix.hdop);
            }
            if (g_fix.have_alt) {
                snprintf(alt_s, sizeof(alt_s), "%.1f", g_fix.alt_m);
            }
            if (g_fix.have_geoid) {
                snprintf(geoid_s, sizeof(geoid_s), "%.1f", g_fix.geoid_m);
            }
            snprintf(line, sizeof(line), "$GPGGA,%s,%s,%c,%s,%c,%d,%02d,%s,%s,M,%s,M,,", hhmmss, lat, ns,
                     lon, ew, g_fix.quality, g_fix.sats, hdop_s, alt_s, geoid_s);
            emit_sentence(line, sizeof(line));
        }

        snprintf(line, sizeof(line), "$GPGLL,%s,%c,%s,%c,%s,%c,%c", lat, ns, lon, ew, hhmmss, status, mode);
        emit_sentence(line, sizeof(line));
    }
    if (sog_ok) {
        snprintf(line, sizeof(line), "$GPVTG,%.1f,T,,M,%.1f,N,%.1f,K,%c", g_fix.cog_deg, g_fix.sog_kn,
                 g_fix.sog_kn * 1.852, valid || !ll_ok ? 'A' : 'N');
        emit_sentence(line, sizeof(line));
    }
}
