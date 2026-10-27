#include "n2k_diag.h"

#include "n2k_to_0183.h"

#include <N2kMsg.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

static constexpr size_t kPgnMax = 64;
static constexpr size_t kDeviceMax = 32;
/* A device or PGN not heard for this long is shown as gone, not dropped. */
static constexpr uint32_t kTwoSourceWindowMs = 10000;

struct PgnRow {
    uint32_t pgn;
    uint8_t src;
    bool fast;
    uint32_t frames;
    uint32_t starts;
    uint32_t msgs;
    uint32_t last_ms;
    uint32_t msgs_at_tick;
    uint16_t rate_x10; /* messages per second, tenths */
};

struct Device {
    uint8_t src;
    bool named;      /* 60928 seen */
    uint64_t name;   /* ISO NAME */
    char model[33];  /* 126996 model ID, "" until seen */
    uint32_t frames;
    uint32_t last_ms;
};

static PgnRow g_pgns[kPgnMax];
static size_t g_pgn_n = 0;
static uint32_t g_pgn_overflow = 0;
static Device g_devices[kDeviceMax];
static size_t g_device_n = 0;

void n2k_diag_reset(void)
{
    memset(g_pgns, 0, sizeof(g_pgns));
    g_pgn_n = 0;
    g_pgn_overflow = 0;
    memset(g_devices, 0, sizeof(g_devices));
    g_device_n = 0;
}

uint32_t n2k_diag_pgn(uint32_t can_id)
{
    const uint32_t pf = (can_id >> 16) & 0xffu;
    uint32_t pgn = (can_id >> 8) & 0x3ffffu;
    if (pf < 240) {
        pgn &= 0x3ff00u;
    }
    return pgn;
}

static PgnRow *pgn_row(uint32_t pgn, uint8_t src)
{
    for (size_t i = 0; i < g_pgn_n; ++i) {
        if (g_pgns[i].pgn == pgn && g_pgns[i].src == src) {
            return &g_pgns[i];
        }
    }
    if (g_pgn_n >= kPgnMax) {
        ++g_pgn_overflow;
        return nullptr;
    }
    PgnRow &r = g_pgns[g_pgn_n++];
    memset(&r, 0, sizeof(r));
    r.pgn = pgn;
    r.src = src;
    return &r;
}

static Device *device(uint8_t src)
{
    for (size_t i = 0; i < g_device_n; ++i) {
        if (g_devices[i].src == src) {
            return &g_devices[i];
        }
    }
    if (g_device_n >= kDeviceMax) {
        return nullptr;
    }
    Device &d = g_devices[g_device_n++];
    memset(&d, 0, sizeof(d));
    d.src = src;
    return &d;
}

void n2k_diag_frame(uint32_t can_id, uint8_t len, const uint8_t *data, bool fast, uint32_t now_ms)
{
    const uint8_t src = static_cast<uint8_t>(can_id & 0xffu);
    PgnRow *r = pgn_row(n2k_diag_pgn(can_id), src);
    if (r != nullptr) {
        r->fast = fast;
        ++r->frames;
        if (fast && len > 0 && data != nullptr && (data[0] & 0x1fu) == 0) {
            ++r->starts;
        }
        r->last_ms = now_ms;
    }
    Device *d = device(src);
    if (d != nullptr) {
        ++d->frames;
        d->last_ms = now_ms;
    }
}

static void read_model(Device *d, const tN2kMsg &msg)
{
    /* 126996: N2K version (2), product code (2), model ID (32 chars). */
    if (msg.DataLen < 36) {
        return;
    }
    int n = 0;
    for (int i = 0; i < 32; ++i) {
        const unsigned char c = msg.Data[4 + i];
        if (c == 0x00 || c == 0xff) {
            break;
        }
        d->model[n++] = (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? static_cast<char>(c) : '?';
    }
    while (n > 0 && (d->model[n - 1] == ' ' || d->model[n - 1] == '@')) {
        --n;
    }
    d->model[n] = '\0';
}

void n2k_diag_message(const tN2kMsg &msg, uint32_t now_ms)
{
    PgnRow *r = pgn_row(static_cast<uint32_t>(msg.PGN), msg.Source);
    if (r != nullptr) {
        ++r->msgs;
        r->last_ms = now_ms;
    }
    if (msg.PGN == 60928L && msg.DataLen >= 8) {
        Device *d = device(msg.Source);
        if (d != nullptr) {
            uint64_t name = 0;
            for (int i = 7; i >= 0; --i) {
                name = (name << 8) | msg.Data[i];
            }
            d->name = name;
            d->named = true;
        }
    } else if (msg.PGN == 126996L) {
        Device *d = device(msg.Source);
        if (d != nullptr) {
            read_model(d, msg);
        }
    }
}

void n2k_diag_tick(uint32_t now_ms)
{
    (void)now_ms;
    for (size_t i = 0; i < g_pgn_n; ++i) {
        PgnRow &r = g_pgns[i];
        const uint32_t d = r.msgs - r.msgs_at_tick;
        r.rate_x10 = static_cast<uint16_t>(d > 6553 ? 65535 : d * 10);
        r.msgs_at_tick = r.msgs;
    }
}

uint32_t n2k_diag_fast_incomplete(void)
{
    uint32_t n = 0;
    for (size_t i = 0; i < g_pgn_n; ++i) {
        const PgnRow &r = g_pgns[i];
        if (r.fast && r.starts > r.msgs) {
            n += r.starts - r.msgs;
        }
    }
    return n;
}

/* ---- hint ----------------------------------------------------------- */

N2kHint n2k_hint_pick(const N2kHintInput &in)
{
    if (in.source != DataSource::Nmea2000) {
        return N2kHint::None;
    }
    /* A retransmitted frame arrives again within ~0.6 ms. Half the traffic
     * being exact repeats means a transmitter is not getting the ACK bit,
     * which this node sends: a wiring or termination fault. */
    if (in.repeats_per_s >= 10 && in.repeats_per_s * 2 >= in.frames_per_s) {
        return N2kHint::Repeats;
    }
    if (in.frames_per_s == 0 && in.bus_errors_per_s > 0) {
        return N2kHint::ErrorsNoFrames;
    }
    if (in.state == BusState::BusOff) {
        return N2kHint::BusOff;
    }
    if (in.node == NodeState::NoAddress) {
        return N2kHint::NoAddress;
    }
    if (in.state == BusState::Waiting || in.state == BusState::Quiet || in.state == BusState::Failed) {
        return N2kHint::NoFrames;
    }
    if (in.lost_per_s > 0) {
        return N2kHint::Losing;
    }
    if (in.fast_starts_per_s >= 2 && in.fast_incomplete_per_s * 10 >= in.fast_starts_per_s) {
        return N2kHint::FastIncomplete;
    }
    if (in.bad_per_s > 0) {
        return N2kHint::BadValues;
    }
    if (in.two_sources) {
        return N2kHint::TwoSources;
    }
    return N2kHint::None;
}

const char *n2k_hint_code(N2kHint hint)
{
    switch (hint) {
    case N2kHint::Repeats:
        return "repeats";
    case N2kHint::ErrorsNoFrames:
        return "errors_no_frames";
    case N2kHint::BusOff:
        return "bus_off";
    case N2kHint::NoAddress:
        return "no_address";
    case N2kHint::NoFrames:
        return "no_frames";
    case N2kHint::Losing:
        return "losing";
    case N2kHint::FastIncomplete:
        return "fast_incomplete";
    case N2kHint::BadValues:
        return "bad_values";
    case N2kHint::TwoSources:
        return "two_sources";
    case N2kHint::None:
        break;
    }
    return "ok";
}

const char *n2k_hint_text(N2kHint hint)
{
    switch (hint) {
    case N2kHint::Repeats:
        return "The same frame keeps repeating: a device is not getting acknowledgements. "
               "Check CAN-H / CAN-L and both terminators.";
    case N2kHint::ErrorsNoFrames:
        return "Bus errors and no frames. Check CAN-H / CAN-L, both terminators, and 12 V on the backbone.";
    case N2kHint::BusOff:
        return "The terminal went bus-off and is rejoining. Check CAN-H / CAN-L and both terminators.";
    case N2kHint::NoAddress:
        return "The terminal could not claim an address: every address is taken. Remove a device "
               "or check for one that claims many addresses.";
    case N2kHint::NoFrames:
        return "No frames. Check the drop cable, 12 V on the backbone, and that the plotter is on.";
    case N2kHint::Losing:
        return "Frames are being lost before decoding. Capture the bus from the phone and report it.";
    case N2kHint::FastIncomplete:
        return "Multi-frame messages (GNSS, AIS) are not arriving whole. Frames are going missing.";
    case N2kHint::BadValues:
        return "A device is sending error codes or impossible values. They are dropped, not shown.";
    case N2kHint::TwoSources:
        return "Two devices send the same value. One is used; the other takes over if it stops.";
    case N2kHint::None:
        break;
    }
    return "";
}

/* ---- names ---------------------------------------------------------- */

const char *n2k_maker_name(uint16_t code)
{
    switch (code) {
    case 135:
        return "Airmar";
    case 137:
        return "Maretron";
    case 140:
        return "Lowrance";
    case 144:
        return "Mercury";
    case 174:
        return "Volvo Penta";
    case 229:
        return "Garmin";
    case 273:
        return "Actisense";
    case 275:
        return "Navico";
    case 315:
        return "Icom";
    case 355:
        return "Mastervolt";
    case 358:
        return "Victron";
    case 381:
        return "B&G";
    case 419:
        return "Fusion";
    case 421:
        return "Standard Horizon";
    case 437:
        return "Digital Yacht";
    case 504:
        return "Vesper";
    case 717:
        return "Yacht Devices";
    case 1851:
        return "Raymarine";
    case 1853:
        return "JRC";
    case 1855:
        return "Furuno";
    case 1857:
        return "Simrad";
    case 1862:
        return "Yamaha";
    default:
        return nullptr;
    }
}

static const char *class_name(uint8_t cls)
{
    switch (cls) {
    case 10:
        return "System tools";
    case 20:
        return "Safety";
    case 25:
        return "Gateway";
    case 30:
        return "Electrical";
    case 35:
        return "Generation";
    case 40:
        return "Steering";
    case 50:
        return "Propulsion";
    case 60:
        return "Navigation";
    case 70:
        return "Communication";
    case 75:
        return "Sensor interface";
    case 80:
        return "Instrumentation";
    case 85:
        return "Environment";
    case 90:
        return "Internal environment";
    case 110:
        return "Human interface";
    case 120:
        return "Display";
    case 125:
        return "Entertainment";
    default:
        return "";
    }
}

static const char *pgn_label(uint32_t pgn)
{
    switch (pgn) {
    case 59392:
        return "ISO Ack";
    case 59904:
        return "ISO Request";
    case 60928:
        return "Address claim";
    case 126208:
        return "Group function";
    case 126464:
        return "PGN list";
    case 126720:
        return "Proprietary";
    case 126992:
        return "System time";
    case 126993:
        return "Heartbeat";
    case 126996:
        return "Product info";
    case 126998:
        return "Config info";
    case 127245:
        return "Rudder";
    case 127250:
        return "Heading";
    case 127251:
        return "Rate of turn";
    case 127257:
        return "Attitude";
    case 127258:
        return "Variation";
    case 127488:
        return "Engine rapid";
    case 127489:
        return "Engine";
    case 127505:
        return "Fluid level";
    case 127508:
        return "Battery";
    case 128259:
        return "Water speed";
    case 128267:
        return "Depth";
    case 128275:
        return "Distance log";
    case 129025:
        return "Position rapid";
    case 129026:
        return "COG/SOG rapid";
    case 129029:
        return "GNSS position";
    case 129033:
        return "Time & date";
    case 129038:
        return "AIS A position";
    case 129039:
        return "AIS B position";
    case 129040:
        return "AIS B extended";
    case 129283:
        return "Cross track";
    case 129284:
        return "Navigation";
    case 129285:
        return "Route/WP";
    case 129291:
        return "Set & drift";
    case 129539:
        return "GNSS DOPs";
    case 129540:
        return "GNSS sats";
    case 129794:
        return "AIS A static";
    case 129808:
        return "DSC";
    case 129809:
        return "AIS B static A";
    case 129810:
        return "AIS B static B";
    case 130306:
        return "Wind";
    case 130310:
        return "Environment";
    case 130311:
        return "Environment";
    case 130312:
        return "Temperature";
    case 130313:
        return "Humidity";
    case 130314:
        return "Pressure";
    case 130316:
        return "Temperature ext";
    default:
        if (pgn >= 130816 || (pgn >= 65280 && pgn <= 65535) || pgn == 61184) {
            return "Proprietary";
        }
        return "";
    }
}

/* ---- JSON ----------------------------------------------------------- */

struct Out {
    char *p;
    size_t cap;
    size_t n;
    bool full;
};

static void put(Out &o, const char *fmt, ...)
{
    if (o.full) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(o.p + o.n, o.cap - o.n, fmt, ap);
    va_end(ap);
    if (w < 0 || static_cast<size_t>(w) >= o.cap - o.n) {
        o.full = true;
        return;
    }
    o.n += static_cast<size_t>(w);
}

static const char *state_code(BusState state)
{
    switch (state) {
    case BusState::Simulated:
        return "sim";
    case BusState::Paused:
        return "paused";
    case BusState::Opening:
        return "opening";
    case BusState::Waiting:
        return "waiting";
    case BusState::Live:
        return "live";
    case BusState::Quiet:
        return "quiet";
    case BusState::BusOff:
        return "bus_off";
    case BusState::Failed:
        return "failed";
    }
    return "unknown";
}

static const char *node_code(NodeState node)
{
    switch (node) {
    case NodeState::Off:
        return "off";
    case NodeState::Claiming:
        return "claiming";
    case NodeState::Claimed:
        return "claimed";
    case NodeState::NoAddress:
        return "no_address";
    }
    return "unknown";
}

static uint32_t age(uint32_t now, uint32_t then) { return then == 0 ? UINT32_MAX : now - then; }

size_t n2k_diag_json(char *out, size_t cap, const N2kStats &s, uint32_t now_ms)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    Out o = {out, cap, 0, false};
    put(o,
        "{\"v\":1,\"bus\":{\"src\":\"%s\",\"state\":\"%s\",\"hint\":\"%s\",\"hint_text\":\"%s\","
        "\"fps\":%lu,\"mps\":%lu,\"frames\":%lu,\"msgs\":%lu,\"lost_q\":%lu,\"lost_drv\":%lu,\"lost_hw\":%lu,"
        "\"ignored\":%lu,\"bus_err\":%lu,\"rec\":%lu,\"repeats\":%lu,\"repeats_ps\":%lu,"
        "\"fast_incomplete\":%lu,\"bad_values\":%lu,\"q_peak\":%lu,\"q_cap\":%lu,\"parse_peak_us\":%lu,"
        "\"pgn_overflow\":%lu},",
        s.source == DataSource::Simulator ? "sim" : "n2k", state_code(s.state),
        n2k_hint_code(s.hint), n2k_hint_text(s.hint), static_cast<unsigned long>(s.frames_per_s),
        static_cast<unsigned long>(s.messages_per_s), static_cast<unsigned long>(s.frames),
        static_cast<unsigned long>(s.messages), static_cast<unsigned long>(s.lost_queue),
        static_cast<unsigned long>(s.lost_driver), static_cast<unsigned long>(s.lost_controller),
        static_cast<unsigned long>(s.ignored), static_cast<unsigned long>(s.bus_errors),
        static_cast<unsigned long>(s.rx_error_counter), static_cast<unsigned long>(s.repeats),
        static_cast<unsigned long>(s.repeats_per_s), static_cast<unsigned long>(s.fast_incomplete),
        static_cast<unsigned long>(s.conv_bad), static_cast<unsigned long>(s.queue_peak),
        static_cast<unsigned long>(s.queue_capacity), static_cast<unsigned long>(s.parse_peak_us),
        static_cast<unsigned long>(g_pgn_overflow));

    /* This terminal as a node: other devices list it under this NAME. */
    put(o,
        "\"self\":{\"node\":\"%s\",\"address\":%u,\"unique\":%lu,\"mfr\":%u,\"model\":\"LMYC Boat Terminal\","
        "\"tx_frames\":%lu,\"tx_failed\":%lu,\"bus_off\":%lu,\"lost_msgs\":%lu},",
        node_code(s.node), static_cast<unsigned>(s.address), static_cast<unsigned long>(s.unique_number),
        static_cast<unsigned>(s.manufacturer_code), static_cast<unsigned long>(s.tx_frames),
        static_cast<unsigned long>(s.tx_failed), static_cast<unsigned long>(s.bus_off_count),
        static_cast<unsigned long>(s.lost_messages));

    put(o, "\"devices\":[");
    for (size_t i = 0; i < g_device_n; ++i) {
        const Device &d = g_devices[i];
        size_t pgns = 0;
        for (size_t k = 0; k < g_pgn_n; ++k) {
            pgns += g_pgns[k].src == d.src ? 1 : 0;
        }
        const uint16_t mfr = static_cast<uint16_t>((d.name >> 21) & 0x7ffu);
        const uint8_t fn = static_cast<uint8_t>((d.name >> 40) & 0xffu);
        const uint8_t cls = static_cast<uint8_t>((d.name >> 49) & 0x7fu);
        const uint32_t unique = static_cast<uint32_t>(d.name & 0x1fffffu);
        const char *maker = d.named ? n2k_maker_name(mfr) : nullptr;
        put(o, "%s{\"src\":%u,\"frames\":%lu,\"pgns\":%u,\"age_ms\":%lu", i == 0 ? "" : ",",
            static_cast<unsigned>(d.src), static_cast<unsigned long>(d.frames), static_cast<unsigned>(pgns),
            static_cast<unsigned long>(age(now_ms, d.last_ms)));
        if (d.named) {
            put(o, ",\"mfr\":%u,\"maker\":\"%s\",\"function\":%u,\"class\":%u,\"class_name\":\"%s\",\"unique\":%lu",
                static_cast<unsigned>(mfr), maker != nullptr ? maker : "", static_cast<unsigned>(fn),
                static_cast<unsigned>(cls), class_name(cls), static_cast<unsigned long>(unique));
        }
        if (d.model[0] != '\0') {
            put(o, ",\"model\":\"%s\"", d.model);
        }
        put(o, "}");
    }
    put(o, "],\"pgns\":[");
    for (size_t i = 0; i < g_pgn_n; ++i) {
        const PgnRow &r = g_pgns[i];
        put(o,
            "%s{\"pgn\":%lu,\"name\":\"%s\",\"src\":%u,\"fast\":%s,\"frames\":%lu,\"starts\":%lu,\"msgs\":%lu,"
            "\"rate\":%u.%u,\"age_ms\":%lu}",
            i == 0 ? "" : ",", static_cast<unsigned long>(r.pgn), pgn_label(r.pgn), static_cast<unsigned>(r.src),
            r.fast ? "true" : "false", static_cast<unsigned long>(r.frames), static_cast<unsigned long>(r.starts),
            static_cast<unsigned long>(r.msgs), static_cast<unsigned>(r.rate_x10 / 10),
            static_cast<unsigned>(r.rate_x10 % 10), static_cast<unsigned long>(age(now_ms, r.last_ms)));
    }
    put(o, "],\"conv\":[");
    N2kConvStats conv[24];
    const size_t nc = n2k_conv_stats(conv, 24);
    for (size_t i = 0; i < nc; ++i) {
        const N2kConvStats &c = conv[i];
        put(o,
            "%s{\"pgn\":%lu,\"rx\":%lu,\"used\":%lu,\"na\":%lu,\"bad\":%lu,\"throttled\":%lu,\"other_src\":%lu}",
            i == 0 ? "" : ",", c.pgn, static_cast<unsigned long>(c.rx), static_cast<unsigned long>(c.used),
            static_cast<unsigned long>(c.na), static_cast<unsigned long>(c.bad),
            static_cast<unsigned long>(c.throttled), static_cast<unsigned long>(c.other_src));
    }
    put(o, "],\"sources\":[");
    N2kSourceLock locks[16];
    const size_t nl = n2k_conv_sources(locks, 16);
    for (size_t i = 0; i < nl; ++i) {
        const N2kSourceLock &l = locks[i];
        put(o, "%s{\"what\":\"%s\",\"src\":%d,\"age_ms\":%lu", i == 0 ? "" : ",", l.what, l.src,
            static_cast<unsigned long>(l.age_ms));
        if (l.other >= 0 && l.other_age_ms <= kTwoSourceWindowMs) {
            put(o, ",\"other\":%d,\"other_age_ms\":%lu", l.other, static_cast<unsigned long>(l.other_age_ms));
        }
        put(o, "}");
    }
    put(o, "]}\n");
    return o.full ? 0 : o.n;
}

uint32_t n2k_diag_fast_starts(void)
{
    uint32_t n = 0;
    for (size_t i = 0; i < g_pgn_n; ++i) {
        n += g_pgns[i].fast ? g_pgns[i].starts : 0;
    }
    return n;
}
