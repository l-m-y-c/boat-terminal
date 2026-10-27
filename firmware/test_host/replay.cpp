/* Replay a candump capture through the terminal's real receive path on the
 * host: the NMEA2000 library's reassembly, the diagnostics and the 0183
 * converter, timed by the capture's own timestamps. A recording from the
 * boat becomes a bench test.
 *
 *   replay CAPTURE.log [--sentences OUT.txt] [--json OUT.json]
 *   replay --sim SECONDS --write CAPTURE.log      (a capture from the simulator)
 *
 * Prints the converter counters, the source owners and a sentence count per
 * type; --sentences keeps every 0183 line, --json the /n2k.json picture. */

#include "n2k_diag.h"
#include "n2k_sim.h"
#include "n2k_to_0183.h"
#include "nmea.h"

#include <NMEA2000.h>
#include <N2kMessages.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <string>

static uint32_t g_now = 1;
extern "C" uint32_t millis(void) { return g_now; }

class ReplayBus : public tNMEA2000 {
public:
    std::deque<N2kFrame> frames;
    bool fast(unsigned long pgn) { return IsFastPacketPGN(pgn); }

protected:
    bool CANSendFrame(unsigned long, unsigned char, const unsigned char *, bool) override { return true; }
    bool CANOpen(void) override { return true; }
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override
    {
        if (frames.empty()) {
            return false;
        }
        const N2kFrame f = frames.front();
        frames.pop_front();
        id = f.id;
        len = f.len;
        memcpy(buf, f.data, f.len);
        n2k_diag_frame(f.id, f.len, f.data, fast(n2k_diag_pgn(f.id)), g_now);
        return true;
    }
};

static ReplayBus g_bus;
static FILE *g_sentences = nullptr;
static std::map<std::string, int> g_counts;
static uint32_t g_messages = 0;

static void on_msg(const tN2kMsg &msg)
{
    ++g_messages;
    n2k_diag_message(msg, g_now);
    n2k_to_0183_handle(msg);
}

static void on_sentence(const char *s, size_t, void *)
{
    std::string key(s, strnlen(s, 6));
    ++g_counts[key];
    if (g_sentences != nullptr) {
        fprintf(g_sentences, "%10.3f %s\n", g_now / 1000.0, s);
    }
}

static void open_stack(void)
{
    g_bus.SetMode(tNMEA2000::N2km_ListenOnly, 22);
    g_bus.EnableForward(false);
    g_bus.SetN2kCANMsgBufSize(32);
    g_bus.SetMsgHandler(on_msg);
    g_bus.Open();
    for (int i = 0; i < 100 && !g_bus.IsOpen(); ++i) {
        g_now += 10;
        g_bus.Open();
    }
}

static bool parse_line(const char *line, double *t, N2kFrame *f)
{
    unsigned long id = 0;
    char hex[40] = {};
    if (sscanf(line, " (%lf) %*s %lx#%39s", t, &id, hex) < 2) {
        return false;
    }
    const size_t n = strlen(hex);
    if (n % 2 != 0 || n > 16) {
        return false;
    }
    memset(f, 0, sizeof(*f));
    f->id = static_cast<uint32_t>(id & 0x1fffffffUL);
    f->len = static_cast<uint8_t>(n / 2);
    for (size_t i = 0; i < f->len; ++i) {
        unsigned v = 0;
        sscanf(hex + 2 * i, "%2x", &v);
        f->data[i] = static_cast<uint8_t>(v);
    }
    return true;
}

static void drain(void)
{
    while (!g_bus.frames.empty()) {
        g_bus.ParseMessages();
    }
}

static int replay(const char *path, const char *json_path)
{
    FILE *in = fopen(path, "r");
    if (in == nullptr) {
        perror(path);
        return 1;
    }
    open_stack();
    const uint32_t base = g_now;
    double t0 = -1.0;
    uint32_t frames = 0, bad = 0;
    uint32_t next_poll = g_now;
    char line[256];
    while (fgets(line, sizeof(line), in) != nullptr) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }
        double t = 0;
        N2kFrame f;
        if (!parse_line(line, &t, &f)) {
            ++bad;
            continue;
        }
        if (t0 < 0) {
            t0 = t;
        }
        const uint32_t at = base + static_cast<uint32_t>((t - t0) * 1000.0);
        /* Advance the clock to the frame, running the 1 Hz output on the way. */
        while (next_poll <= at) {
            g_now = next_poll;
            n2k_to_0183_poll();
            next_poll += 100;
        }
        g_now = at;
        g_bus.frames.push_back(f);
        drain();
        ++frames;
    }
    fclose(in);
    n2k_to_0183_poll();
    n2k_diag_tick(g_now);

    const double secs = (g_now - base) / 1000.0;
    printf("%u frames, %u messages over %.1f s (%u unreadable lines)\n", frames, g_messages, secs, bad);
    printf("fast packets: %u started, %u never completed\n", n2k_diag_fast_starts(), n2k_diag_fast_incomplete());

    printf("\nConverter (rx = used + rate-limited + n/a + bad + other device)\n");
    N2kConvStats conv[32];
    const size_t nc = n2k_conv_stats(conv, 32);
    for (size_t i = 0; i < nc; ++i) {
        const N2kConvStats &c = conv[i];
        printf("  %6lu  rx %7u  used %7u  limited %7u  n/a %5u  bad %5u  other %5u%s\n", c.pgn, c.rx, c.used,
               c.throttled, c.na, c.bad, c.other_src, c.bad ? "  <-- BAD VALUES" : "");
    }
    printf("\nValues and the device they come from\n");
    N2kSourceLock locks[16];
    const size_t nl = n2k_conv_sources(locks, 16);
    for (size_t i = 0; i < nl; ++i) {
        printf("  %-14s from %3d%s\n", locks[i].what, locks[i].src,
               locks[i].other >= 0 ? "  (another device also sends it)" : "");
    }
    printf("\n0183 out\n");
    for (const auto &kv : g_counts) {
        printf("  %-7s %6d  %5.2f/s\n", kv.first.c_str(), kv.second, secs > 0 ? kv.second / secs : 0.0);
    }

    if (json_path != nullptr) {
        N2kStats s = {};
        s.source = DataSource::Nmea2000;
        s.state = BusState::Live;
        s.frames = frames;
        s.messages = g_messages;
        s.fast_incomplete = n2k_diag_fast_incomplete();
        s.conv_bad = n2k_conv_bad_total();
        s.node = NodeState::Off;
        s.address = 254;
        static char json[32768];
        const size_t n = n2k_diag_json(json, sizeof(json), s, g_now);
        FILE *out = fopen(json_path, "w");
        if (out != nullptr && n > 0) {
            fwrite(json, 1, n, out);
            fclose(out);
            printf("\nSaved %s\n", json_path);
        }
    }
    return 0;
}

static FILE *g_write = nullptr;

static bool write_frame(const N2kFrame &f, void *)
{
    fprintf(g_write, "(%lu.%06lu) can0 %08X#", static_cast<unsigned long>(g_now / 1000),
            static_cast<unsigned long>((g_now % 1000) * 1000), static_cast<unsigned>(f.id));
    for (uint8_t i = 0; i < f.len; ++i) {
        fprintf(g_write, "%02X", f.data[i]);
    }
    fputc('\n', g_write);
    return true;
}

static int simulate(int seconds, const char *path)
{
    g_write = fopen(path, "w");
    if (g_write == nullptr) {
        perror(path);
        return 1;
    }
    fprintf(g_write, "# LMYC boat terminal simulator, %d s, candump log format\n", seconds);
    n2k_sim_reset();
    n2k_sim_set_gnss(true);
    size_t n = 0;
    for (g_now = 1000; g_now <= 1000u + static_cast<uint32_t>(seconds) * 1000u; g_now += 10) {
        n += n2k_sim_step(g_now, write_frame, nullptr);
    }
    fclose(g_write);
    printf("Wrote %zu simulator frames to %s\n", n, path);
    return 0;
}

int main(int argc, char **argv)
{
    nmea_init();
    nmea_set_sink(on_sentence, nullptr);
    n2k_diag_reset();
    const char *capture = nullptr, *sentences = nullptr, *json = nullptr, *write = nullptr;
    int sim = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--sentences") == 0 && i + 1 < argc) {
            sentences = argv[++i];
        } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            json = argv[++i];
        } else if (strcmp(argv[i], "--sim") == 0 && i + 1 < argc) {
            sim = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--write") == 0 && i + 1 < argc) {
            write = argv[++i];
        } else if (argv[i][0] != '-') {
            capture = argv[i];
        } else {
            capture = nullptr;
            break;
        }
    }
    if (sim > 0 && write != nullptr) {
        return simulate(sim, write);
    }
    if (capture == nullptr) {
        fprintf(stderr, "usage: replay CAPTURE.log [--sentences OUT.txt] [--json OUT.json]\n"
                        "       replay --sim SECONDS --write CAPTURE.log\n");
        return 2;
    }
    if (sentences != nullptr) {
        g_sentences = fopen(sentences, "w");
    }
    const int rc = replay(capture, json);
    if (g_sentences != nullptr) {
        fclose(g_sentences);
        printf("Saved %s\n", sentences);
    }
    return rc;
}
