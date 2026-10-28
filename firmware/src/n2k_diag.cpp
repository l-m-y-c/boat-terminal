#include "n2k_diag.h"

#include "n2k_describe.h"
#include "n2k_node.h"
#include "n2k_to_0183.h"

#include <N2kMsg.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

static constexpr size_t kPgnMax = 64;
static constexpr size_t kLastBytes = 64;
static constexpr size_t kRecentBytes = 24;
static constexpr size_t kRecentMax = 192;
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
    uint16_t rate_x10;  /* messages per second, tenths, averaged over ~10 s */
    uint32_t rate_x1000; /* the average itself, thousandths */
    /* The newest message, for the node detail page. */
    uint8_t last_dst;
    uint16_t last_total;
    uint8_t last_len;
    uint8_t last_data[kLastBytes];
};

/* Recent messages from every device, newest last, for "what is this node
 * saying". In PSRAM on the board. */
struct Recent {
    uint32_t ms;
    uint32_t pgn;
    uint8_t src;
    uint8_t dst;
    uint16_t total;
    uint8_t len;
    uint8_t data[kRecentBytes];
};
static Recent *g_recent = nullptr;
static uint32_t g_recent_n = 0; /* total ever written */


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
    if (g_recent == nullptr) {
#if defined(ESP_PLATFORM)
        g_recent = static_cast<Recent *>(heap_caps_calloc(kRecentMax, sizeof(Recent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#endif
        if (g_recent == nullptr) {
            g_recent = static_cast<Recent *>(calloc(kRecentMax, sizeof(Recent)));
        }
    }
    g_recent_n = 0;
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
    const uint16_t total = msg.DataLen > 0 ? static_cast<uint16_t>(msg.DataLen) : 0;
    if (r != nullptr) {
        ++r->msgs;
        r->last_ms = now_ms;
        r->last_dst = msg.Destination;
        r->last_total = total;
        r->last_len = static_cast<uint8_t>(total < kLastBytes ? total : kLastBytes);
        memcpy(r->last_data, msg.Data, r->last_len);
    }
    if (g_recent != nullptr) {
        Recent &e = g_recent[g_recent_n % kRecentMax];
        e.ms = now_ms;
        e.pgn = static_cast<uint32_t>(msg.PGN);
        e.src = msg.Source;
        e.dst = msg.Destination;
        e.total = total;
        e.len = static_cast<uint8_t>(total < kRecentBytes ? total : kRecentBytes);
        memcpy(e.data, msg.Data, e.len);
        ++g_recent_n;
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
    static uint32_t last_ms = 0;
    const uint32_t span = (last_ms == 0 || now_ms - last_ms == 0) ? 1000 : now_ms - last_ms;
    last_ms = now_ms;
    for (size_t i = 0; i < g_pgn_n; ++i) {
        PgnRow &r = g_pgns[i];
        /* Exponential average, ~10 s: a 0.5 Hz PGN reads 0.5, not 0 or 1. */
        const uint32_t d = r.msgs - r.msgs_at_tick;
        r.msgs_at_tick = r.msgs;
        /* Per second, whatever the gap between ticks. */
        const uint32_t sample = static_cast<uint32_t>((static_cast<uint64_t>(d > 4000 ? 4000 : d) * 1000000u) / span);
        r.rate_x1000 = r.rate_x1000 == 0 && r.msgs == d ? sample : (r.rate_x1000 * 9 + sample) / 10;
        const uint32_t x10 = (r.rate_x1000 + 50) / 100;
        r.rate_x10 = static_cast<uint16_t>(x10 > 65535 ? 65535 : x10);
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

const char *n2k_class_name(uint8_t cls)
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
        "\"self\":{\"node\":\"%s\",\"address\":%u,\"unique\":%lu,\"mfr\":%u,\"model\":\"%s\","
        "\"tx_frames\":%lu,\"tx_failed\":%lu,\"tx_pending\":%lu,\"bus_off\":%lu,\"lost_msgs\":%lu},",
        node_code(s.node), static_cast<unsigned>(s.address), static_cast<unsigned long>(s.unique_number),
        static_cast<unsigned>(s.manufacturer_code), kN2kNodeModelId, static_cast<unsigned long>(s.tx_frames),
        static_cast<unsigned long>(s.tx_failed), static_cast<unsigned long>(s.tx_pending),
        static_cast<unsigned long>(s.bus_off_count),
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
                static_cast<unsigned>(cls), n2k_class_name(cls), static_cast<unsigned long>(unique));
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

/* ---- one device, for the node detail page -------------------------------- */

static void hex_of(const uint8_t *data, uint8_t len, char *out, size_t cap)
{
    size_t n = 0;
    for (uint8_t i = 0; i < len && n + 3 < cap; ++i) {
        n += static_cast<size_t>(snprintf(out + n, cap - n, "%02X", data[i]));
    }
    out[n] = '\0';
}

/* Decoded text, only for a payload kept whole: a cut one would misread. */
static void text_of(uint32_t pgn, uint8_t src, uint8_t dst, const uint8_t *data, uint8_t len, uint16_t total,
                    char *out, size_t cap)
{
    out[0] = '\0';
    if (len != total || len == 0) {
        if (pgn == 126996 || pgn == 126998 || pgn == 126464) {
            snprintf(out, cap, "%s: details in the identity panel", pgn_label(pgn));
        }
        return;
    }
    tN2kMsg m;
    m.SetPGN(pgn);
    m.Source = src;
    m.Destination = dst;
    for (uint8_t i = 0; i < len; ++i) {
        m.AddByte(data[i]);
    }
    n2k_describe(m, out, cap);
    for (char *p = out; *p != '\0'; ++p) {
        if (*p == '"' || *p == '\\') {
            *p = '\'';
        }
    }
}

size_t n2k_diag_device_json(char *out, size_t cap, uint8_t src, uint32_t now_ms)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    Out o = {out, cap, 0, false};
    char hex[kLastBytes * 2 + 1];
    char text[120];
    put(o, "\"pgns\":[");
    bool first = true;
    for (size_t i = 0; i < g_pgn_n; ++i) {
        const PgnRow &r = g_pgns[i];
        if (r.src != src) {
            continue;
        }
        hex_of(r.last_data, r.last_len, hex, sizeof(hex));
        text_of(r.pgn, r.src, r.last_dst, r.last_data, r.last_len, r.last_total, text, sizeof(text));
        put(o,
            "%s{\"pgn\":%lu,\"name\":\"%s\",\"fast\":%s,\"frames\":%lu,\"starts\":%lu,\"msgs\":%lu,\"rate\":%u.%u,"
            "\"age_ms\":%lu,\"len\":%u,\"hex\":\"%s\",\"text\":\"%s\"}",
            first ? "" : ",", static_cast<unsigned long>(r.pgn), pgn_label(r.pgn), r.fast ? "true" : "false",
            static_cast<unsigned long>(r.frames), static_cast<unsigned long>(r.starts),
            static_cast<unsigned long>(r.msgs), static_cast<unsigned>(r.rate_x10 / 10),
            static_cast<unsigned>(r.rate_x10 % 10), static_cast<unsigned long>(age(now_ms, r.last_ms)),
            static_cast<unsigned>(r.last_total), hex, text);
        first = false;
    }
    put(o, "],\"recent\":[");
    first = true;
    size_t shown = 0;
    if (g_recent != nullptr) {
        const uint32_t n = g_recent_n < kRecentMax ? g_recent_n : kRecentMax;
        for (uint32_t k = 0; k < n && shown < 40; ++k) {
            const Recent &e = g_recent[(g_recent_n - 1 - k) % kRecentMax];
            if (e.src != src) {
                continue;
            }
            hex_of(e.data, e.len, hex, sizeof(hex));
            text_of(e.pgn, e.src, e.dst, e.data, e.len, e.total, text, sizeof(text));
            put(o, "%s{\"age_ms\":%lu,\"pgn\":%lu,\"name\":\"%s\",\"dst\":%u,\"len\":%u,\"hex\":\"%s\",\"text\":\"%s\"}",
                first ? "" : ",", static_cast<unsigned long>(now_ms - e.ms), static_cast<unsigned long>(e.pgn),
                pgn_label(e.pgn), static_cast<unsigned>(e.dst), static_cast<unsigned>(e.total), hex, text);
            first = false;
            ++shown;
        }
    }
    put(o, "]");
    return o.full ? 0 : o.n;
}

static void line_text(uint32_t pgn, uint8_t src, uint8_t dst, const uint8_t *data, uint8_t len, uint16_t total,
                      char *out, size_t cap)
{
    text_of(pgn, src, dst, data, len, total, out, cap);
    if (out[0] == '\0') {
        hex_of(data, len, out, cap);
    }
}

size_t n2k_diag_device_pgns(uint8_t src, N2kPgnLine *out, size_t max, uint32_t now_ms)
{
    size_t n = 0;
    for (size_t i = 0; out != nullptr && i < g_pgn_n && n < max; ++i) {
        const PgnRow &r = g_pgns[i];
        if (r.src != src) {
            continue;
        }
        N2kPgnLine &l = out[n++];
        l.pgn = r.pgn;
        snprintf(l.name, sizeof(l.name), "%s", pgn_label(r.pgn));
        l.rate_x10 = r.rate_x10;
        l.msgs = r.msgs;
        l.age_ms = age(now_ms, r.last_ms);
        line_text(r.pgn, r.src, r.last_dst, r.last_data, r.last_len, r.last_total, l.text, sizeof(l.text));
    }
    return n;
}

size_t n2k_diag_device_recent(uint8_t src, N2kPgnLine *out, size_t max, uint32_t now_ms)
{
    size_t n = 0;
    if (out == nullptr || g_recent == nullptr) {
        return 0;
    }
    const uint32_t have = g_recent_n < kRecentMax ? g_recent_n : kRecentMax;
    for (uint32_t k = 0; k < have && n < max; ++k) {
        const Recent &e = g_recent[(g_recent_n - 1 - k) % kRecentMax];
        if (e.src != src) {
            continue;
        }
        N2kPgnLine &l = out[n++];
        l.pgn = e.pgn;
        snprintf(l.name, sizeof(l.name), "%s", pgn_label(e.pgn));
        l.rate_x10 = 0;
        l.msgs = 0;
        l.age_ms = now_ms - e.ms;
        line_text(e.pgn, e.src, e.dst, e.data, e.len, e.total, l.text, sizeof(l.text));
    }
    return n;
}
