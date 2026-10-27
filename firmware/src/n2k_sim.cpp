#include "n2k_sim.h"

#include <N2kMessages.h>
#include <NMEA2000.h>

#include <cmath>
#include <cstdio>
#include <cstring>

/* Device addresses on the virtual bus. A real backbone assigns these by
 * address claim; fixed values keep the PGN list stable between runs. */
static constexpr uint8_t kSrcCompass = 8;
static constexpr uint8_t kSrcWind = 7;
static constexpr uint8_t kSrcTransducer = 12;
static constexpr uint8_t kSrcBattery = 20;
static constexpr uint8_t kSrcGps = 35;
static constexpr uint8_t kSrcAis = 43;

static constexpr double kPi = 3.14159265358979323846;
static constexpr double kDegToRad = kPi / 180.0;
static constexpr double kKnToMs = 1.0 / 1.9438444924406046;
static constexpr double kMetresPerDegLat = 111320.0;

/* English Bay, west of Stanley Park. The course is a box about 0.4 nm a side. */
static constexpr double kCentreLat = 49.2880;
static constexpr double kCentreLon = -123.1900;
static constexpr double kLegLat = 0.0030;
static constexpr double kLegLon = 0.0046;

/* 2026-07-01 18:00:00 UTC. The clock runs from boot. */
static constexpr uint16_t kBaseDays = 20635;
static constexpr double kBaseSeconds = 18.0 * 3600.0;

uint32_t n2k_can_id(uint8_t priority, uint32_t pgn, uint8_t source, uint8_t destination)
{
    const uint32_t pf = (pgn >> 8) & 0xffu;
    uint32_t id = (static_cast<uint32_t>(priority & 0x7u) << 26) | (source & 0xffu);
    if (pf < 240u) {
        id |= ((pgn & 0x3ff00u) << 8) | (static_cast<uint32_t>(destination) << 8);
    } else {
        id |= (pgn & 0x3ffffu) << 8;
    }
    return id;
}

size_t n2k_frames_from_msg(const tN2kMsg &msg, uint8_t sequence, N2kFrameSink sink, void *ctx)
{
    if (sink == nullptr || msg.DataLen <= 0) {
        return 0;
    }
    N2kFrame frame = {};
    frame.id = n2k_can_id(msg.Priority, msg.PGN, msg.Source, msg.Destination);
    const size_t total = static_cast<size_t>(msg.DataLen);

    if (total <= 8) {
        frame.len = static_cast<uint8_t>(total);
        memcpy(frame.data, msg.Data, total);
        return sink(frame, ctx) ? 1 : 0;
    }

    size_t sent = 0;
    size_t at = 0;
    uint8_t index = 0;
    const uint8_t seq = static_cast<uint8_t>((sequence & 0x7u) << 5);
    while (at < total) {
        memset(frame.data, 0xff, sizeof(frame.data));
        frame.data[0] = static_cast<uint8_t>(seq | (index & 0x1fu));
        size_t room = 7;
        size_t offset = 1;
        if (index == 0) {
            frame.data[1] = static_cast<uint8_t>(total);
            room = 6;
            offset = 2;
        }
        const size_t n = (total - at) < room ? (total - at) : room;
        memcpy(frame.data + offset, msg.Data + at, n);
        at += n;
        frame.len = 8;
        if (sink(frame, ctx)) {
            ++sent;
        }
        ++index;
    }
    return sent;
}

namespace {

struct Schedule {
    uint32_t period_ms;
    uint32_t next_ms;
};

enum Slot : uint8_t {
    kPosition,
    kHeading,
    kCogSog,
    kWind,
    kDepth,
    kWaterSpeed,
    kGnss,
    kTime,
    kSeaTemp,
    kBattery,
    kFerry,
    kFerryStatic,
    kFreighter,
    kFreighterStatic,
    kYacht,
    kYachtStatic,
    kSlots
};

/* Period and first offset, so the PGNs do not all land on one tick. */
constexpr uint32_t kPeriod[kSlots] = {
    100, 100, 250, 500, 1000, 1000, 1000, 1000, 2000, 2000,
    6000, 60000, 30000, 60000, 30000, 60000,
};
constexpr uint32_t kOffset[kSlots] = {
    0, 50, 20, 70, 130, 170, 210, 240, 330, 410,
    900, 1300, 1700, 2300, 2900, 3700,
};

struct Target {
    double lat;
    double lon;
    double cog_deg;
    double sog_kn;
};

struct Sim {
    bool started;
    bool gnss;
    uint32_t start_ms;
    uint32_t last_ms;
    Schedule due[kSlots];
    uint8_t fp_seq[kSlots];
    uint8_t sid;

    double lat;
    double lon;
    int leg;
    double heading_deg;
    double stw_kn;
    double cog_deg;
    double sog_kn;

    Target ferry;
    Target yacht;
};

Sim g_sim = {};
bool g_gnss_wanted = true;

double wrap360(double deg)
{
    deg = fmod(deg, 360.0);
    return deg < 0.0 ? deg + 360.0 : deg;
}

double wrap180(double deg)
{
    deg = wrap360(deg);
    return deg > 180.0 ? deg - 360.0 : deg;
}

double metres_per_deg_lon(double lat)
{
    return kMetresPerDegLat * cos(lat * kDegToRad);
}

/* Corners of the box, worked anticlockwise. */
void waypoint(int leg, double *lat, double *lon)
{
    static constexpr int kCornerLat[4] = {1, 1, -1, -1};
    static constexpr int kCornerLon[4] = {1, -1, -1, 1};
    *lat = kCentreLat + kCornerLat[leg & 3] * kLegLat;
    *lon = kCentreLon + kCornerLon[leg & 3] * kLegLon;
}

void advance(Target *t, double dt_s)
{
    const double v = t->sog_kn * kKnToMs;
    const double c = t->cog_deg * kDegToRad;
    t->lat += v * cos(c) * dt_s / kMetresPerDegLat;
    t->lon += v * sin(c) * dt_s / metres_per_deg_lon(t->lat);
}

void step_physics(double t_s, double dt_s)
{
    Sim &s = g_sim;
    double wlat = 0.0, wlon = 0.0;
    waypoint(s.leg, &wlat, &wlon);
    double dn = (wlat - s.lat) * kMetresPerDegLat;
    double de = (wlon - s.lon) * metres_per_deg_lon(s.lat);
    if (sqrt(dn * dn + de * de) < 40.0) {
        s.leg = (s.leg + 1) & 3;
        waypoint(s.leg, &wlat, &wlon);
        dn = (wlat - s.lat) * kMetresPerDegLat;
        de = (wlon - s.lon) * metres_per_deg_lon(s.lat);
    }
    const double bearing = wrap360(atan2(de, dn) / kDegToRad);
    /* Helm wander of a few degrees and a slow change of pace. */
    s.heading_deg = wrap360(bearing + 3.0 * sin(t_s / 20.0));
    s.stw_kn = 5.8 + 0.6 * sin(t_s / 45.0) + 0.15 * sin(t_s / 3.7);

    /* A 0.4 kn ebb setting south-east. */
    const double h = s.heading_deg * kDegToRad;
    const double set = 120.0 * kDegToRad;
    const double vn = s.stw_kn * cos(h) + 0.4 * cos(set);
    const double ve = s.stw_kn * sin(h) + 0.4 * sin(set);
    s.sog_kn = sqrt(vn * vn + ve * ve);
    s.cog_deg = wrap360(atan2(ve, vn) / kDegToRad);
    s.lat += vn * kKnToMs * dt_s / kMetresPerDegLat;
    s.lon += ve * kKnToMs * dt_s / metres_per_deg_lon(s.lat);

    advance(&s.ferry, dt_s);
    advance(&s.yacht, dt_s);
    /* The ferry crosses the bay and starts again from the north-west. */
    if (s.ferry.lat < kCentreLat - 0.03) {
        s.ferry.lat = kCentreLat + 0.03;
        s.ferry.lon = kCentreLon - 0.05;
    }
    if (s.yacht.lat < kCentreLat - 0.03) {
        s.yacht.lat = kCentreLat + 0.02;
    }
}

void true_wind(double t_s, double *from_deg, double *speed_kn)
{
    *from_deg = wrap360(295.0 + 8.0 * sin(t_s / 150.0) + 3.0 * sin(t_s / 11.0));
    *speed_kn = 14.0 + 2.5 * sin(t_s / 90.0) + 0.8 * sin(t_s / 7.0);
}

/* Apparent wind over the boat moving through the water. */
void apparent_wind(double t_s, double *angle_deg, double *speed_kn)
{
    const Sim &s = g_sim;
    double from = 0.0, tws = 0.0;
    true_wind(t_s, &from, &tws);
    const double to = (from + 180.0) * kDegToRad;
    const double h = s.heading_deg * kDegToRad;
    const double an = tws * cos(to) - s.stw_kn * cos(h);
    const double ae = tws * sin(to) - s.stw_kn * sin(h);
    *speed_kn = sqrt(an * an + ae * ae);
    const double apparent_from = atan2(-ae, -an) / kDegToRad;
    *angle_deg = wrap360(wrap180(apparent_from - s.heading_deg));
}

/* Soundings vary across the course: about 12 m inshore to 34 m offshore. */
double depth_m(void)
{
    const Sim &s = g_sim;
    const double x = (s.lon - kCentreLon) * metres_per_deg_lon(s.lat) / 1000.0;
    const double y = (s.lat - kCentreLat) * kMetresPerDegLat / 1000.0;
    const double d = 23.0 + 9.0 * sin(x * 2.1 + 0.4) * cos(y * 1.7) - 4.0 * y;
    return d < 4.0 ? 4.0 : d;
}

struct Emit {
    N2kFrameSink sink;
    void *ctx;
    size_t frames;
};

void send(Emit &out, Slot slot, tN2kMsg &msg, uint8_t source)
{
    msg.Source = source;
    msg.Destination = 0xff;
    const uint8_t seq = g_sim.fp_seq[slot];
    g_sim.fp_seq[slot] = static_cast<uint8_t>((seq + 1) & 0x7u);
    out.frames += n2k_frames_from_msg(msg, seq, out.sink, out.ctx);
}

void emit_slot(Emit &out, Slot slot, double t_s)
{
    Sim &s = g_sim;
    tN2kMsg msg;
    const unsigned char sid = s.sid;
    const double clock = kBaseSeconds + t_s;
    const uint16_t days = static_cast<uint16_t>(kBaseDays + static_cast<uint32_t>(clock / 86400.0));
    const double seconds = fmod(clock, 86400.0);

    switch (slot) {
    case kPosition:
        SetN2kPGN129025(msg, s.lat, s.lon);
        send(out, slot, msg, kSrcGps);
        break;
    case kHeading:
        SetN2kPGN127250(msg, sid, s.heading_deg * kDegToRad, N2kDoubleNA, 16.5 * kDegToRad,
                        N2khr_true);
        send(out, slot, msg, kSrcCompass);
        break;
    case kCogSog:
        SetN2kPGN129026(msg, sid, N2khr_true, s.cog_deg * kDegToRad, s.sog_kn * kKnToMs);
        send(out, slot, msg, kSrcGps);
        break;
    case kWind: {
        double awa = 0.0, aws = 0.0;
        apparent_wind(t_s, &awa, &aws);
        SetN2kPGN130306(msg, sid, aws * kKnToMs, awa * kDegToRad, N2kWind_Apparent);
        send(out, slot, msg, kSrcWind);
        break;
    }
    case kDepth:
        SetN2kPGN128267(msg, sid, depth_m(), 0.4);
        send(out, slot, msg, kSrcTransducer);
        break;
    case kWaterSpeed:
        SetN2kPGN128259(msg, sid, s.stw_kn * kKnToMs);
        send(out, slot, msg, kSrcTransducer);
        break;
    case kGnss:
        SetN2kPGN129029(msg, sid, days, seconds, s.lat, s.lon, 2.0, N2kGNSSt_GPS,
                        N2kGNSSm_GNSSfix, 10, 0.8, 1.4, -17.0);
        send(out, slot, msg, kSrcGps);
        break;
    case kTime:
        SetN2kPGN126992(msg, sid, days, seconds, N2ktimes_GPS);
        send(out, slot, msg, kSrcGps);
        break;
    case kSeaTemp:
        SetN2kPGN130316(msg, sid, 0, N2kts_SeaTemperature,
                        CToKelvin(12.4 + 0.1 * sin(t_s / 300.0)));
        send(out, slot, msg, kSrcTransducer);
        break;
    case kBattery:
        SetN2kPGN127508(msg, 0, 12.72 - 0.02 * sin(t_s / 60.0), -4.2 + 0.3 * sin(t_s / 13.0),
                        CToKelvin(19.0), sid);
        send(out, slot, msg, kSrcBattery);
        break;
    case kFerry:
        SetN2kPGN129038(msg, 1, N2kaisr_Initial, 316999001, s.ferry.lat, s.ferry.lon, true, false,
                        static_cast<uint8_t>(static_cast<uint32_t>(t_s) % 60), s.ferry.cog_deg * kDegToRad,
                        s.ferry.sog_kn * kKnToMs, N2kaischannel_A_VDL_reception,
                        s.ferry.cog_deg * kDegToRad, 0.0, N2kaisns_Under_Way_Motoring);
        send(out, slot, msg, kSrcAis);
        break;
    case kFerryStatic:
        SetN2kPGN129794(msg, 5, N2kaisr_Initial, 316999001, 9999001, "VHAR1", "HARBOUR STAR", 60,
                        96.0, 18.0, 9.0, 40.0, kBaseDays, 19.0 * 3600.0, 3.8, "NANAIMO",
                        N2kaisv_ITU_R_M_1371_1, N2kGNSSt_GPS, N2kaisdte_Ready);
        send(out, slot, msg, kSrcAis);
        break;
    case kFreighter:
        SetN2kPGN129038(msg, 1, N2kaisr_Initial, 316999002, kCentreLat - 0.012, kCentreLon - 0.021,
                        false, false, static_cast<uint8_t>(static_cast<uint32_t>(t_s) % 60),
                        212.0 * kDegToRad, 0.1 * kKnToMs, N2kaischannel_B_VDL_reception,
                        212.0 * kDegToRad, 0.0, N2kaisns_At_Anchor);
        send(out, slot, msg, kSrcAis);
        break;
    case kFreighterStatic:
        SetN2kPGN129794(msg, 5, N2kaisr_Initial, 316999002, 9999002, "VPAC2", "PACIFIC DAWN", 70,
                        229.0, 32.0, 16.0, 190.0, kBaseDays, 6.0 * 3600.0, 11.6, "VANCOUVER",
                        N2kaisv_ITU_R_M_1371_1, N2kGNSSt_GPS, N2kaisdte_Ready,
                        N2kaischannel_B_VDL_reception);
        send(out, slot, msg, kSrcAis);
        break;
    case kYacht:
        SetN2kPGN129039(msg, 18, N2kaisr_Initial, 316999003, s.yacht.lat, s.yacht.lon, false, false,
                        static_cast<uint8_t>(static_cast<uint32_t>(t_s) % 60), s.yacht.cog_deg * kDegToRad,
                        s.yacht.sog_kn * kKnToMs, N2kaischannel_B_VDL_reception, N2kDoubleNA,
                        N2kaisunit_ClassB_CS, true, false, true, true, N2kaismode_Autonomous, true);
        send(out, slot, msg, kSrcAis);
        break;
    case kYachtStatic:
        SetN2kPGN129809(msg, 24, N2kaisr_Initial, 316999003, "WIND DANCER",
                        N2kaischannel_B_VDL_reception);
        send(out, slot, msg, kSrcAis);
        SetN2kPGN129810(msg, 24, N2kaisr_Initial, 316999003, 36, "LMYCSIM", "VWD3", 11.0, 3.6, 1.8,
                        8.0, 0);
        send(out, slot, msg, kSrcAis);
        break;
    case kSlots:
        break;
    }
}

bool is_gnss(Slot slot)
{
    return slot == kPosition || slot == kCogSog || slot == kGnss || slot == kTime;
}

/* Network management of the virtual devices, further down. */
void step_nodes(Emit &out, uint32_t now_ms);
void reset_nodes(void);

} // namespace

void n2k_sim_reset(void)
{
    g_sim = {};
    reset_nodes();
    g_sim.gnss = g_gnss_wanted;
    waypoint(3, &g_sim.lat, &g_sim.lon);
    g_sim.leg = 0;
    g_sim.ferry = {kCentreLat + 0.02, kCentreLon - 0.035, 135.0, 14.0};
    g_sim.yacht = {kCentreLat + 0.01, kCentreLon + 0.012, 200.0, 5.2};
}

void n2k_sim_set_gnss(bool on)
{
    g_gnss_wanted = on;
    g_sim.gnss = on;
}

bool n2k_sim_gnss(void) { return g_gnss_wanted; }

size_t n2k_sim_step(uint32_t now_ms, N2kFrameSink sink, void *ctx)
{
    Sim &s = g_sim;
    if (!s.started) {
        const bool gnss = g_gnss_wanted;
        n2k_sim_reset();
        s.gnss = gnss;
        s.started = true;
        s.start_ms = now_ms;
        s.last_ms = now_ms;
        for (int i = 0; i < kSlots; ++i) {
            s.due[i] = {kPeriod[i], now_ms + kOffset[i]};
        }
    }

    const double t_s = static_cast<double>(now_ms - s.start_ms) / 1000.0;
    double dt_s = static_cast<double>(now_ms - s.last_ms) / 1000.0;
    s.last_ms = now_ms;
    /* After a long stall, move on instead of sailing a straight line off the course. */
    if (dt_s > 1.0) {
        dt_s = 1.0;
    }
    if (dt_s > 0.0) {
        step_physics(t_s, dt_s);
    }

    Emit out = {sink, ctx, 0};
    step_nodes(out, now_ms);
    for (int i = 0; i < kSlots; ++i) {
        Schedule &d = s.due[i];
        if (static_cast<int32_t>(now_ms - d.next_ms) < 0) {
            continue;
        }
        /* One message per due slot; a stalled caller does not get a burst. */
        d.next_ms += d.period_ms;
        if (static_cast<int32_t>(now_ms - d.next_ms) >= 0) {
            d.next_ms = now_ms + d.period_ms;
        }
        const Slot slot = static_cast<Slot>(i);
        if (is_gnss(slot) && !s.gnss) {
            continue;
        }
        emit_slot(out, slot, t_s);
        s.sid = static_cast<uint8_t>((s.sid + 1) % 253);
    }
    return out.frames;
}

/* ---- the simulated devices as NMEA 2000 nodes ---------------------------- */

namespace {

/* Each virtual device claims its address, sends a heartbeat, and answers
 * ISO requests for its address claim, product information, configuration
 * information and PGN lists, like real equipment. Manufacturer code 2046
 * (open source) so no real maker is impersonated. Class / function codes
 * are NMEA's: navigation 60 (attitude 140, position 145, depth/speed/temp
 * 136, AIS 195), external environment 85 (atmospheric 130), electrical
 * generation 35 (battery 170). */
struct SimDevice {
    uint8_t src;
    uint32_t unique;
    uint8_t device_class;
    uint8_t function;
    uint8_t load_equivalency;
    const char *model;
    const char *install;
    unsigned long transmit[12];
};

constexpr unsigned long kManagementTx[] = {59392UL, 60928UL, 126464UL, 126993UL, 126996UL, 126998UL};
constexpr unsigned long kManagementRx[] = {59392UL, 59904UL, 60928UL, 126208UL, 126464UL, 0};

constexpr SimDevice kDevices[] = {
    {kSrcGps, 1001, 60, 145, 1, "Sim GPS", "Sim: pushpit rail", {129025UL, 129026UL, 129029UL, 126992UL}},
    {kSrcCompass, 1002, 60, 140, 1, "Sim compass", "Sim: under the chart table", {127250UL}},
    {kSrcWind, 1003, 85, 130, 2, "Sim wind sensor", "Sim: masthead", {130306UL}},
    {kSrcTransducer, 1004, 60, 136, 1, "Sim depth / speed / temp", "Sim: forward of the keel",
     {128267UL, 128259UL, 130316UL}},
    {kSrcBattery, 1005, 35, 170, 1, "Sim battery monitor", "Sim: house bank", {127508UL}},
    {kSrcAis, 1006, 60, 195, 3, "Sim AIS receiver", "Sim: nav station",
     {129038UL, 129039UL, 129794UL, 129809UL, 129810UL}},
};
constexpr size_t kDeviceCount = sizeof(kDevices) / sizeof(kDevices[0]);
constexpr uint32_t kSimHeartbeatMs = 60000;

struct SimNode {
    uint8_t fp_seq;
    uint8_t heartbeat_seq;
    uint32_t next_heartbeat_ms;
    bool claimed;
};

SimNode g_nodes[kDeviceCount] = {};
bool g_nodes_started = false;
uint32_t g_nodes_start_ms = 0;

void reset_nodes(void) { g_nodes_started = false; }

void node_send(Emit &out, size_t dev, tN2kMsg &msg)
{
    msg.Source = kDevices[dev].src;
    const uint8_t seq = g_nodes[dev].fp_seq;
    g_nodes[dev].fp_seq = static_cast<uint8_t>((seq + 1) & 0x7u);
    out.frames += n2k_frames_from_msg(msg, seq, out.sink, out.ctx);
}

void send_claim(Emit &out, size_t dev)
{
    tN2kMsg msg;
    const SimDevice &d = kDevices[dev];
    SetN2kPGN60928(msg, d.unique, 2046, d.function, d.device_class, 0, 0, 4);
    msg.Destination = 0xff;
    node_send(out, dev, msg);
}

void send_product(Emit &out, size_t dev)
{
    tN2kMsg msg;
    const SimDevice &d = kDevices[dev];
    char serial[16];
    snprintf(serial, sizeof(serial), "SIM-%04lu", static_cast<unsigned long>(d.unique));
    SetN2kPGN126996(msg, 2101, 1000 + static_cast<unsigned>(dev), d.model, "sim 1.0", "virtual bus", serial, 1,
                    d.load_equivalency);
    msg.Destination = 0xff;
    node_send(out, dev, msg);
}

void send_config(Emit &out, size_t dev)
{
    tN2kMsg msg;
    SetN2kPGN126998(msg, "LMYC boat-terminal simulator", kDevices[dev].install, "");
    msg.Destination = 0xff;
    node_send(out, dev, msg);
}

void send_lists(Emit &out, size_t dev, uint8_t to)
{
    unsigned long tx[20] = {};
    size_t n = 0;
    for (unsigned long p : kDevices[dev].transmit) {
        if (p != 0 && n < 19) {
            tx[n++] = p;
        }
    }
    for (unsigned long p : kManagementTx) {
        if (n < 19) {
            tx[n++] = p;
        }
    }
    tx[n] = 0;
    tN2kMsg msg;
    SetN2kPGN126464(msg, to, N2kpgnl_transmit, tx);
    node_send(out, dev, msg);
    tN2kMsg rx;
    SetN2kPGN126464(rx, to, N2kpgnl_receive, kManagementRx);
    node_send(out, dev, rx);
}

void send_heartbeat(Emit &out, size_t dev)
{
    tN2kMsg msg;
    SetN2kPGN126993(msg, kSimHeartbeatMs, g_nodes[dev].heartbeat_seq);
    g_nodes[dev].heartbeat_seq = static_cast<uint8_t>((g_nodes[dev].heartbeat_seq + 1) % 253);
    msg.Destination = 0xff;
    node_send(out, dev, msg);
}

void send_nak(Emit &out, size_t dev, uint8_t to, unsigned long pgn)
{
    tN2kMsg msg;
    SetN2kPGN59392(msg, 1, 0xff, pgn);
    msg.Destination = to;
    node_send(out, dev, msg);
}

/* Claims at start-up, spaced like devices powering up, then heartbeats. */
void step_nodes(Emit &out, uint32_t now_ms)
{
    if (!g_nodes_started) {
        g_nodes_started = true;
        g_nodes_start_ms = now_ms;
        for (size_t i = 0; i < kDeviceCount; ++i) {
            g_nodes[i] = {};
            g_nodes[i].next_heartbeat_ms = now_ms + 5000 + static_cast<uint32_t>(i) * 1000;
        }
    }
    for (size_t i = 0; i < kDeviceCount; ++i) {
        SimNode &n = g_nodes[i];
        if (!n.claimed && now_ms - g_nodes_start_ms >= static_cast<uint32_t>(i) * 40) {
            n.claimed = true;
            send_claim(out, i);
        }
        if (static_cast<int32_t>(now_ms - n.next_heartbeat_ms) >= 0) {
            n.next_heartbeat_ms += kSimHeartbeatMs;
            send_heartbeat(out, i);
        }
    }
}

} // namespace

size_t n2k_sim_receive(const N2kFrame &frame, uint32_t /*now_ms*/, N2kFrameSink sink, void *ctx)
{
    const uint32_t pf = (frame.id >> 16) & 0xffu;
    const uint32_t pgn = (frame.id >> 8) & 0x3ff00u;
    if (pf >= 240u || pgn != 59904UL || frame.len < 3) {
        return 0; /* only ISO requests are answered */
    }
    const uint8_t dst = static_cast<uint8_t>((frame.id >> 8) & 0xffu);
    const uint8_t from = static_cast<uint8_t>(frame.id & 0xffu);
    const unsigned long wanted = static_cast<unsigned long>(frame.data[0]) |
                                 (static_cast<unsigned long>(frame.data[1]) << 8) |
                                 (static_cast<unsigned long>(frame.data[2]) << 16);
    Emit out = {sink, ctx, 0};
    for (size_t i = 0; i < kDeviceCount; ++i) {
        if (dst != 0xff && dst != kDevices[i].src) {
            continue;
        }
        switch (wanted) {
        case 60928UL:
            send_claim(out, i);
            break;
        case 126996UL:
            send_product(out, i);
            break;
        case 126998UL:
            send_config(out, i);
            break;
        case 126464UL:
            send_lists(out, i, from);
            break;
        case 126993UL:
            send_heartbeat(out, i);
            break;
        default:
            /* A global request for something not sent is not answered. */
            if (dst != 0xff) {
                send_nak(out, i, from, wanted);
            }
            break;
        }
    }
    return out.frames;
}
