#include "n2k_network.h"

#include "n2k_diag.h"

#include <NMEA2000.h>
#include <N2kMsg.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

static constexpr size_t kDevicesMax = 32;
static constexpr size_t kListMax = 24;
static constexpr size_t kTextMax = 33;
static constexpr size_t kDescMax = 71;
static constexpr uint32_t kSpacingMs = 150;    /* between two requests */
static constexpr uint32_t kRetryMs = 2000;     /* before asking the same thing again */
static constexpr uint32_t kClaimWaitMs = 1000; /* answers to the global claim request */
static constexpr uint8_t kTries = 3;
static constexpr uint32_t kSilentMs = 10000;

/* What the terminal still wants from a device. */
enum Want : uint8_t { kWantName, kWantProduct, kWantConfig, kWantLists, kWantCount };
static constexpr unsigned long kWantPgn[kWantCount] = {60928UL, 126996UL, 126998UL, 126464UL};

struct Device {
    bool used;
    uint8_t address;
    bool named;
    uint64_t name;
    uint32_t first_ms;
    uint32_t last_ms;
    uint16_t address_changes;
    uint8_t previous_address;
    /* 126996 */
    bool have_product;
    uint16_t n2k_version;
    uint16_t product_code;
    uint8_t certification;
    uint8_t load_equivalency;
    char model[kTextMax];
    char software[kTextMax];
    char model_version[kTextMax];
    char serial[kTextMax];
    /* 126998 */
    bool have_config;
    char install1[kDescMax];
    char install2[kDescMax];
    char maker_info[kDescMax];
    /* 126464 */
    bool have_tx;
    bool have_rx;
    uint8_t ntx;
    uint8_t nrx;
    uint32_t tx[kListMax];
    uint32_t rx[kListMax];
    /* 126993 */
    uint32_t heartbeat_ms;
    uint32_t heartbeat_last_ms;
    /* Asking */
    uint8_t tries[kWantCount];
    bool refused[kWantCount]; /* NAKed: not supported, never ask again */
    uint32_t asked_ms[kWantCount];
};

static Device *g_dev = nullptr;
static bool g_sweep = true;
static bool g_sweep_sent = false;
static uint32_t g_sweep_ms = 0;
static uint32_t g_next_ms = 0;
static uint32_t g_requests = 0;
static uint32_t g_answers = 0;
static size_t g_cursor = 0;

static N2kNetSelf g_self = {};
static bool g_have_self = false;

static Device *table(void)
{
    if (g_dev == nullptr) {
        const size_t bytes = kDevicesMax * sizeof(Device);
#if defined(ESP_PLATFORM)
        g_dev = static_cast<Device *>(heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (g_dev == nullptr) {
            g_dev = static_cast<Device *>(calloc(1, bytes));
        }
#else
        g_dev = static_cast<Device *>(calloc(1, bytes));
#endif
    }
    return g_dev;
}

void n2k_net_reset(void)
{
    Device *t = table();
    if (t != nullptr) {
        memset(t, 0, kDevicesMax * sizeof(Device));
    }
    g_sweep = true;
    g_sweep_sent = false;
    g_sweep_ms = 0;
    g_next_ms = 0;
    g_requests = 0;
    g_answers = 0;
    g_cursor = 0;
}

static Device *by_address(uint8_t address)
{
    Device *t = table();
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        if (t[i].used && t[i].address == address) {
            return &t[i];
        }
    }
    return nullptr;
}

static Device *by_name(uint64_t name)
{
    Device *t = table();
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        if (t[i].used && t[i].named && t[i].name == name) {
            return &t[i];
        }
    }
    return nullptr;
}

static Device *add(uint8_t address, uint32_t now)
{
    Device *t = table();
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        if (!t[i].used) {
            memset(&t[i], 0, sizeof(Device));
            t[i].used = true;
            t[i].address = address;
            t[i].first_ms = now;
            t[i].last_ms = now;
            t[i].load_equivalency = 0xff;
            return &t[i];
        }
    }
    return nullptr;
}

static bool is_self(uint8_t address) { return g_have_self && address == g_self.address; }

void n2k_net_frame(uint8_t src, uint32_t now)
{
    if (src >= 252 || is_self(src)) {
        return; /* 254 "cannot claim", 255 global: not a device */
    }
    Device *d = by_address(src);
    if (d == nullptr) {
        d = add(src, now);
    }
    if (d != nullptr) {
        d->last_ms = now;
    }
}

static void copy_text(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (; src != nullptr && src[n] != '\0' && n + 1 < cap; ++n) {
        const unsigned char c = static_cast<unsigned char>(src[n]);
        dst[n] = (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') ? static_cast<char>(c) : '?';
    }
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '@')) {
        --n;
    }
    dst[n] = '\0';
}

static void on_claim(const tN2kMsg &msg, uint32_t now)
{
    if (msg.DataLen < 8 || msg.Source >= 252) {
        return;
    }
    uint64_t name = 0;
    for (int i = 7; i >= 0; --i) {
        name = (name << 8) | msg.Data[i];
    }
    Device *here = by_address(msg.Source);
    Device *known = by_name(name);
    if (known != nullptr && known != here) {
        /* A device the terminal knows moved here. Whatever was recorded at
         * this address has gone (lost the address, or left). */
        if (here != nullptr) {
            here->used = false;
        }
        known->previous_address = known->address;
        known->address = msg.Source;
        ++known->address_changes;
        known->last_ms = now;
        return;
    }
    if (here != nullptr && here->named && here->name != name) {
        /* A different device holds this address now: start its record afresh. */
        const uint32_t first = here->first_ms;
        memset(here, 0, sizeof(Device));
        here->used = true;
        here->address = msg.Source;
        here->first_ms = first;
        here->load_equivalency = 0xff;
    }
    if (here == nullptr) {
        here = add(msg.Source, now);
    }
    if (here == nullptr) {
        return;
    }
    if (!here->named) {
        ++g_answers;
    }
    here->named = true;
    here->name = name;
    here->last_ms = now;
}

static void read_list(Device *d, const tN2kMsg &msg)
{
    if (msg.DataLen < 1) {
        return;
    }
    const uint8_t kind = msg.Data[0];
    uint32_t *list = kind == 0 ? d->tx : d->rx;
    uint8_t n = 0;
    for (int i = 1; i + 2 < msg.DataLen && n < kListMax; i += 3) {
        const uint32_t pgn = msg.Data[i] | (msg.Data[i + 1] << 8) | (static_cast<uint32_t>(msg.Data[i + 2]) << 16);
        if (pgn != 0 && pgn != 0xffffff) {
            list[n++] = pgn;
        }
    }
    if (kind == 0) {
        d->ntx = n;
        d->have_tx = true;
    } else if (kind == 1) {
        d->nrx = n;
        d->have_rx = true;
    }
}

void n2k_net_message(const tN2kMsg &msg, uint32_t now)
{
    if (msg.PGN == 60928L) {
        on_claim(msg, now);
        return;
    }
    if (msg.Source >= 252 || is_self(msg.Source)) {
        return;
    }
    Device *d = by_address(msg.Source);
    if (d == nullptr) {
        d = add(msg.Source, now);
        if (d == nullptr) {
            return;
        }
    }
    d->last_ms = now;
    switch (msg.PGN) {
    case 126996L: {
        unsigned short version = 0, code = 0;
        char model[kTextMax] = {}, sw[kTextMax] = {}, mv[kTextMax] = {}, serial[kTextMax] = {};
        unsigned char cert = 0, len = 0;
        if (ParseN2kPGN126996(msg, version, code, sizeof(model), model, sizeof(sw), sw, sizeof(mv), mv,
                              sizeof(serial), serial, cert, len)) {
            if (!d->have_product) {
                ++g_answers;
            }
            d->have_product = true;
            d->n2k_version = version;
            d->product_code = code;
            d->certification = cert;
            d->load_equivalency = len;
            copy_text(d->model, sizeof(d->model), model);
            copy_text(d->software, sizeof(d->software), sw);
            copy_text(d->model_version, sizeof(d->model_version), mv);
            copy_text(d->serial, sizeof(d->serial), serial);
        }
        break;
    }
    case 126998L: {
        char maker[kDescMax] = {}, d1[kDescMax] = {}, d2[kDescMax] = {};
        size_t lm = sizeof(maker), l1 = sizeof(d1), l2 = sizeof(d2);
        if (ParseN2kPGN126998(msg, lm, maker, l1, d1, l2, d2)) {
            if (!d->have_config) {
                ++g_answers;
            }
            d->have_config = true;
            copy_text(d->maker_info, sizeof(d->maker_info), maker);
            copy_text(d->install1, sizeof(d->install1), d1);
            copy_text(d->install2, sizeof(d->install2), d2);
        }
        break;
    }
    case 126464L:
        if (!d->have_tx && !d->have_rx) {
            ++g_answers;
        }
        read_list(d, msg);
        break;
    case 126993L:
        if (msg.DataLen >= 3) {
            const uint32_t interval = msg.Data[0] | (msg.Data[1] << 8);
            d->heartbeat_ms = (interval >= 1000 && interval < 0xfffe) ? interval : 60000;
            d->heartbeat_last_ms = now;
        }
        break;
    case 59392L:
        /* NAK to a request of ours: that device does not do it. */
        if (msg.DataLen >= 8 && msg.Data[0] == 1 && is_self(msg.Destination)) {
            const unsigned long pgn = msg.Data[5] | (msg.Data[6] << 8) | (static_cast<unsigned long>(msg.Data[7]) << 16);
            for (int w = 0; w < kWantCount; ++w) {
                if (kWantPgn[w] == pgn) {
                    d->refused[w] = true;
                }
            }
        }
        break;
    default:
        break;
    }
}

void n2k_net_set_self(const N2kNetSelf &self)
{
    g_self = self;
    g_have_self = true;
    /* The terminal is never in the table: it is reported from g_self. */
    Device *mine = by_address(self.address);
    if (mine != nullptr) {
        mine->used = false;
    }
}

void n2k_net_refresh(void)
{
    Device *t = table();
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        Device &d = t[i];
        memset(d.tries, 0, sizeof(d.tries));
        memset(d.asked_ms, 0, sizeof(d.asked_ms));
        d.have_product = d.have_config = d.have_tx = d.have_rx = false;
    }
    g_sweep = true;
    g_sweep_sent = false;
}

static bool has(const Device &d, int want)
{
    switch (want) {
    case kWantName:
        return d.named;
    case kWantProduct:
        return d.have_product;
    case kWantConfig:
        return d.have_config;
    case kWantLists:
        return d.have_tx;
    default:
        return true;
    }
}

bool n2k_net_next_request(uint32_t now, bool can_send, uint8_t *dst, unsigned long *pgn)
{
    Device *t = table();
    if (!can_send || t == nullptr || dst == nullptr || pgn == nullptr) {
        return false;
    }
    if (g_next_ms != 0 && static_cast<int32_t>(now - g_next_ms) < 0) {
        return false;
    }
    if (g_sweep && !g_sweep_sent) {
        /* Everyone, once: every device answers with its address claim. */
        g_sweep_sent = true;
        g_sweep_ms = now;
        g_next_ms = now + kClaimWaitMs;
        *dst = 0xff;
        *pgn = 60928UL;
        ++g_requests;
        return true;
    }
    for (size_t k = 0; k < kDevicesMax; ++k) {
        Device &d = t[(g_cursor + k) % kDevicesMax];
        if (!d.used || is_self(d.address)) {
            continue;
        }
        for (int w = 0; w < kWantCount; ++w) {
            if (has(d, w) || d.refused[w] || d.tries[w] >= kTries) {
                continue;
            }
            if (d.asked_ms[w] != 0 && now - d.asked_ms[w] < kRetryMs) {
                break; /* waiting for this device's answer; try the next device */
            }
            ++d.tries[w];
            d.asked_ms[w] = now;
            g_cursor = (g_cursor + k + 1) % kDevicesMax;
            g_next_ms = now + kSpacingMs;
            *dst = d.address;
            *pgn = kWantPgn[w];
            ++g_requests;
            return true;
        }
    }
    g_sweep = false;
    return false;
}

/* ---- health ------------------------------------------------------------ */

static uint8_t dev_class(uint64_t n) { return static_cast<uint8_t>((n >> 49) & 0x7f); }
static uint8_t dev_function(uint64_t n) { return static_cast<uint8_t>((n >> 40) & 0xff); }
static uint8_t dev_instance(uint64_t n) { return static_cast<uint8_t>((n >> 32) & 0xff); }
static uint16_t dev_maker(uint64_t n) { return static_cast<uint16_t>((n >> 21) & 0x7ff); }

static uint16_t issues_of(const Device &d, uint32_t now)
{
    uint16_t is = 0;
    if (!d.named && d.tries[kWantName] >= kTries) {
        is |= kNetNoName;
    }
    if (!d.have_product && (d.refused[kWantProduct] || d.tries[kWantProduct] >= kTries)) {
        is |= kNetNoProduct;
    }
    if (d.heartbeat_last_ms != 0 && now - d.heartbeat_last_ms > d.heartbeat_ms * 5 / 2) {
        is |= kNetHeartbeatLost;
    }
    if (d.address_changes > 0) {
        is |= kNetAddressChanged;
    }
    if (now - d.last_ms > kSilentMs) {
        is |= kNetSilent;
    }
    if (d.named) {
        Device *t = table();
        for (size_t i = 0; i < kDevicesMax; ++i) {
            const Device &o = t[i];
            if (&o != &d && o.used && o.named && dev_class(o.name) == dev_class(d.name) &&
                dev_function(o.name) == dev_function(d.name) && dev_instance(o.name) == dev_instance(d.name)) {
                is |= kNetInstanceConflict;
            }
        }
    }
    return is;
}

const char *n2k_net_issue_code(N2kNetIssue issue)
{
    switch (issue) {
    case kNetNoName:
        return "no_name";
    case kNetNoProduct:
        return "no_product";
    case kNetHeartbeatLost:
        return "heartbeat_lost";
    case kNetAddressChanged:
        return "address_changed";
    case kNetInstanceConflict:
        return "instance_conflict";
    case kNetSilent:
        return "silent";
    }
    return "?";
}

const char *n2k_net_issue_text(N2kNetIssue issue)
{
    switch (issue) {
    case kNetNoName:
        return "Never answered for its address claim. Not a standard node, or a gateway passing frames on.";
    case kNetNoProduct:
        return "Never sent product information, so its model and power draw are unknown.";
    case kNetHeartbeatLost:
        return "Its heartbeat stopped. It may have lost power or crashed.";
    case kNetAddressChanged:
        return "It moved to another address since the terminal first saw it.";
    case kNetInstanceConflict:
        return "Another device has the same class, function and instance. Plotters can mix them up: "
               "change one device's instance.";
    case kNetSilent:
        return "Nothing from it for 10 s.";
    }
    return "";
}

const char *n2k_net_role(uint8_t cls, uint8_t fn)
{
    struct Role {
        uint8_t cls;
        uint8_t fn;
        const char *name;
    };
    static const Role kRoles[] = {
        {25, 130, "PC gateway"},          {25, 131, "N2K to analog gateway"}, {25, 132, "Analog to N2K gateway"},
        {25, 135, "NMEA 0183 gateway"},   {25, 136, "Network gateway"},       {25, 137, "Wireless gateway"},
        {35, 170, "Battery"},             {40, 150, "Autopilot"},             {60, 130, "Depth"},
        {60, 135, "Depth / speed"},       {60, 136, "Depth / speed / temp"},  {60, 140, "Heading / attitude"},
        {60, 145, "GNSS position"},       {60, 155, "Speed"},                 {60, 160, "Rate of turn"},
        {60, 170, "Integrated navigation"}, {60, 190, "Navigation management"}, {60, 195, "AIS"},
        {60, 200, "Radar"},               {85, 130, "Weather / wind"},        {85, 160, "Aquatic"},
        {120, 130, "Display"},            {120, 140, "Alarm display"},
    };
    for (const Role &r : kRoles) {
        if (r.cls == cls && r.fn == fn) {
            return r.name;
        }
    }
    return "";
}

/* ---- views ------------------------------------------------------------- */

static void title_of(const Device &d, char *out, size_t cap)
{
    const char *maker = d.named ? n2k_maker_name(dev_maker(d.name)) : nullptr;
    const bool own_code = d.named && dev_maker(d.name) == 2046;
    if (d.model[0] != '\0') {
        snprintf(out, cap, "%s%s%s", (maker != nullptr && !own_code) ? maker : "",
                 (maker != nullptr && !own_code) ? " " : "", d.model);
    } else if (maker != nullptr && !own_code) {
        snprintf(out, cap, "%s device", maker);
    } else {
        snprintf(out, cap, "Address %u", static_cast<unsigned>(d.address));
    }
}

size_t n2k_net_device_count(void)
{
    Device *t = table();
    size_t n = 0;
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        n += t[i].used ? 1 : 0;
    }
    return n + (g_have_self ? 1 : 0);
}

uint32_t n2k_net_len_total(void)
{
    Device *t = table();
    uint32_t sum = g_have_self ? g_self.load_equivalency : 0;
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        if (t[i].used && t[i].have_product && t[i].load_equivalency != 0xff) {
            sum += t[i].load_equivalency;
        }
    }
    return sum;
}

static void self_view(N2kNetView &v)
{
    memset(&v, 0, sizeof(v));
    v.address = g_self.address;
    v.self = true;
    v.device_class = g_self.device_class;
    v.function = g_self.function;
    v.load_equivalency = g_self.load_equivalency;
    snprintf(v.title, sizeof(v.title), "%s", g_self.model != nullptr ? g_self.model : "This terminal");
    snprintf(v.role, sizeof(v.role), "This terminal");
    copy_text(v.install, sizeof(v.install), g_self.install1);
}

static void device_view(const Device &d, N2kNetView &v, uint32_t now)
{
    memset(&v, 0, sizeof(v));
    v.address = d.address;
    v.device_class = d.named ? dev_class(d.name) : 0;
    v.function = d.named ? dev_function(d.name) : 0;
    v.load_equivalency = d.have_product ? d.load_equivalency : 0xff;
    v.issues = issues_of(d, now);
    v.age_ms = now - d.last_ms;
    title_of(d, v.title, sizeof(v.title));
    const char *role = n2k_net_role(v.device_class, v.function);
    if (role[0] != '\0') {
        snprintf(v.role, sizeof(v.role), "%s", role);
    } else if (d.named) {
        snprintf(v.role, sizeof(v.role), "%s", n2k_class_name(v.device_class));
    }
    copy_text(v.install, sizeof(v.install), d.install1);
}

size_t n2k_net_view(N2kNetView *out, size_t max, uint32_t now)
{
    if (out == nullptr || max == 0) {
        return 0;
    }
    size_t n = 0;
    if (g_have_self) {
        self_view(out[n++]);
    }
    Device *t = table();
    for (size_t i = 0; t != nullptr && i < kDevicesMax && n < max; ++i) {
        if (t[i].used) {
            device_view(t[i], out[n++], now);
        }
    }
    /* The terminal first, then by class (navigation, environment, power,
     * displays...), then by address. Insertion sort: at most 33 entries. */
    for (size_t i = 1; i < n; ++i) {
        N2kNetView key = out[i];
        size_t j = i;
        auto before = [](const N2kNetView &a, const N2kNetView &b) {
            if (a.self != b.self) {
                return a.self;
            }
            if (a.device_class != b.device_class) {
                return a.device_class < b.device_class;
            }
            return a.address < b.address;
        };
        while (j > 0 && before(key, out[j - 1])) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = key;
    }
    return n;
}

/* ---- JSON ---------------------------------------------------------------- */

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

static void put_issues(Out &o, uint16_t issues)
{
    put(o, "\"issues\":[");
    bool first = true;
    for (uint16_t bit = 1; bit != 0 && bit <= kNetSilent; bit = static_cast<uint16_t>(bit << 1)) {
        if (issues & bit) {
            const N2kNetIssue is = static_cast<N2kNetIssue>(bit);
            put(o, "%s{\"code\":\"%s\",\"text\":\"%s\"}", first ? "" : ",", n2k_net_issue_code(is),
                n2k_net_issue_text(is));
            first = false;
        }
    }
    put(o, "]");
}

static void put_list(Out &o, const char *key, const uint32_t *list, uint8_t n, bool have)
{
    if (!have) {
        return;
    }
    put(o, ",\"%s\":[", key);
    for (uint8_t i = 0; i < n; ++i) {
        put(o, "%s%lu", i == 0 ? "" : ",", static_cast<unsigned long>(list[i]));
    }
    put(o, "]");
}

static void put_device(Out &o, const Device &d, uint32_t now)
{
    char title[48];
    title_of(d, title, sizeof(title));
    put(o, "{\"address\":%u,\"self\":false,\"title\":\"%s\",\"age_ms\":%lu,\"known_ms\":%lu",
        static_cast<unsigned>(d.address), title, static_cast<unsigned long>(now - d.last_ms),
        static_cast<unsigned long>(now - d.first_ms));
    if (d.named) {
        const uint8_t cls = dev_class(d.name), fn = dev_function(d.name);
        const char *maker = n2k_maker_name(dev_maker(d.name));
        put(o,
            ",\"name\":\"%016llX\",\"mfr\":%u,\"maker\":\"%s\",\"class\":%u,\"class_name\":\"%s\",\"function\":%u,"
            "\"role\":\"%s\",\"instance\":%u,\"system_instance\":%u,\"unique\":%lu",
            static_cast<unsigned long long>(d.name), static_cast<unsigned>(dev_maker(d.name)),
            maker != nullptr ? maker : "", static_cast<unsigned>(cls), n2k_class_name(cls), static_cast<unsigned>(fn),
            n2k_net_role(cls, fn), static_cast<unsigned>(dev_instance(d.name)),
            static_cast<unsigned>((d.name >> 56) & 0x0f), static_cast<unsigned long>(d.name & 0x1fffff));
    }
    if (d.have_product) {
        put(o,
            ",\"model\":\"%s\",\"software\":\"%s\",\"model_version\":\"%s\",\"serial\":\"%s\",\"product_code\":%u,"
            "\"n2k_version\":%u,\"certification\":%u,\"len\":%u",
            d.model, d.software, d.model_version, d.serial, static_cast<unsigned>(d.product_code),
            static_cast<unsigned>(d.n2k_version), static_cast<unsigned>(d.certification),
            static_cast<unsigned>(d.load_equivalency));
    }
    if (d.have_config) {
        put(o, ",\"install1\":\"%s\",\"install2\":\"%s\",\"maker_info\":\"%s\"", d.install1, d.install2,
            d.maker_info);
    }
    put_list(o, "transmits", d.tx, d.ntx, d.have_tx);
    put_list(o, "receives", d.rx, d.nrx, d.have_rx);
    if (d.heartbeat_last_ms != 0) {
        put(o, ",\"heartbeat_ms\":%lu,\"heartbeat_age_ms\":%lu", static_cast<unsigned long>(d.heartbeat_ms),
            static_cast<unsigned long>(now - d.heartbeat_last_ms));
    }
    if (d.address_changes > 0) {
        put(o, ",\"address_changes\":%u,\"previous_address\":%u", static_cast<unsigned>(d.address_changes),
            static_cast<unsigned>(d.previous_address));
    }
    put(o, ",\"complete\":%s,", (d.named && d.have_product && d.have_config && d.have_tx) ? "true" : "false");
    put_issues(o, issues_of(d, now));
    put(o, "}");
}

static void put_self(Out &o)
{
    char model[kTextMax], sw[kTextMax], mv[kTextMax], serial[kTextMax], install[kDescMax];
    copy_text(model, sizeof(model), g_self.model);
    copy_text(sw, sizeof(sw), g_self.software);
    copy_text(mv, sizeof(mv), g_self.model_version);
    copy_text(serial, sizeof(serial), g_self.serial);
    copy_text(install, sizeof(install), g_self.install1);
    put(o,
        "{\"address\":%u,\"self\":true,\"title\":\"%s\",\"age_ms\":0,\"mfr\":%u,\"maker\":\"%s\",\"class\":%u,"
        "\"class_name\":\"%s\",\"function\":%u,\"role\":\"This terminal\",\"unique\":%lu,\"model\":\"%s\","
        "\"software\":\"%s\",\"model_version\":\"%s\",\"serial\":\"%s\",\"len\":%u,\"install1\":\"%s\","
        "\"complete\":true,\"issues\":[]}",
        static_cast<unsigned>(g_self.address), model, static_cast<unsigned>(g_self.manufacturer),
        n2k_maker_name(g_self.manufacturer) != nullptr ? n2k_maker_name(g_self.manufacturer) : "",
        static_cast<unsigned>(g_self.device_class), n2k_class_name(g_self.device_class),
        static_cast<unsigned>(g_self.function), static_cast<unsigned long>(g_self.unique), model, sw, mv, serial,
        static_cast<unsigned>(g_self.load_equivalency), install);
}

size_t n2k_net_json(char *out, size_t cap, uint32_t now)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    Out o = {out, cap, 0, false};
    Device *t = table();
    size_t pending = 0;
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        if (!t[i].used) {
            continue;
        }
        for (int w = 0; w < kWantCount; ++w) {
            if (!has(t[i], w) && !t[i].refused[w] && t[i].tries[w] < kTries) {
                ++pending;
                break;
            }
        }
    }
    put(o,
        "{\"v\":1,\"devices_total\":%u,\"len_total\":%lu,\"discovery\":{\"requests\":%lu,\"answers\":%lu,"
        "\"asking\":%u},\"devices\":[",
        static_cast<unsigned>(n2k_net_device_count()), static_cast<unsigned long>(n2k_net_len_total()),
        static_cast<unsigned long>(g_requests), static_cast<unsigned long>(g_answers),
        static_cast<unsigned>(pending));
    bool first = true;
    if (g_have_self) {
        put_self(o);
        first = false;
    }
    for (size_t i = 0; t != nullptr && i < kDevicesMax; ++i) {
        if (!t[i].used) {
            continue;
        }
        if (!first) {
            put(o, ",");
        }
        put_device(o, t[i], now);
        first = false;
    }
    put(o, "]}\n");
    return o.full ? 0 : o.n;
}

size_t n2k_net_device_json(char *out, size_t cap, uint8_t address, uint32_t now)
{
    if (out == nullptr || cap == 0) {
        return 0;
    }
    Out o = {out, cap, 0, false};
    if (is_self(address)) {
        put_self(o);
    } else {
        const Device *d = by_address(address);
        if (d == nullptr) {
            return 0;
        }
        put_device(o, *d, now);
    }
    return o.full ? 0 : o.n;
}

bool n2k_net_info(uint8_t address, N2kNetInfo *out, uint32_t now)
{
    if (out == nullptr) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->transmits = 0xff;
    if (is_self(address)) {
        self_view(out->view);
        out->named = out->have_product = out->have_config = true;
        copy_text(out->model, sizeof(out->model), g_self.model);
        copy_text(out->software, sizeof(out->software), g_self.software);
        copy_text(out->model_version, sizeof(out->model_version), g_self.model_version);
        copy_text(out->serial, sizeof(out->serial), g_self.serial);
        out->unique = g_self.unique;
        out->manufacturer = g_self.manufacturer;
        const char *mk = n2k_maker_name(g_self.manufacturer);
        snprintf(out->maker, sizeof(out->maker), "%s", mk != nullptr ? mk : "");
        return true;
    }
    const Device *d = by_address(address);
    if (d == nullptr) {
        return false;
    }
    device_view(*d, out->view, now);
    out->named = d->named;
    out->have_product = d->have_product;
    out->have_config = d->have_config;
    if (d->named) {
        out->unique = static_cast<uint32_t>(d->name & 0x1fffff);
        out->manufacturer = dev_maker(d->name);
        out->instance = dev_instance(d->name);
        out->system_instance = static_cast<uint8_t>((d->name >> 56) & 0x0f);
        const char *mk = n2k_maker_name(out->manufacturer);
        snprintf(out->maker, sizeof(out->maker), "%s", mk != nullptr ? mk : "");
    }
    memcpy(out->model, d->model, sizeof(out->model));
    memcpy(out->software, d->software, sizeof(out->software));
    memcpy(out->model_version, d->model_version, sizeof(out->model_version));
    memcpy(out->serial, d->serial, sizeof(out->serial));
    memcpy(out->install2, d->install2, sizeof(out->install2));
    out->certification = d->certification;
    out->n2k_version = d->n2k_version;
    out->heartbeat_ms = d->heartbeat_last_ms != 0 ? d->heartbeat_ms : 0;
    out->heartbeat_age_ms = d->heartbeat_last_ms != 0 ? now - d->heartbeat_last_ms : 0;
    out->address_changes = d->address_changes;
    out->previous_address = d->previous_address;
    out->transmits = d->have_tx ? d->ntx : 0xff;
    return true;
}
