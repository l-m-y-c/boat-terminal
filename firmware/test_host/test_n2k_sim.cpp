/* Host test for the simulator and the receive path behind it.
 *
 * The simulator's CAN frames go through the real NMEA2000 library's
 * fast-packet reassembly (the same tNMEA2000 code the board runs, with a
 * frame queue for a CAN controller), then the converter. Checks:
 *  - framing: single frames, fast-packet split and sequence bits;
 *  - every message the simulator sent is reassembled, none lost;
 *  - the converter turns them into valid 0183 with sensible values;
 *  - GNSS off stops the GPS PGNs and nothing else. */

#include "n2k_sim.h"
#include "n2k_to_0183.h"
#include "nmea.h"

#include <NMEA2000.h>
#include <N2kMessages.h>

#include <cmath>
#include <cstdio>
#include <unistd.h>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <vector>

static uint32_t g_now = 0;
extern "C" uint32_t millis(void) { return g_now; }

static int g_fail = 0;
static int g_pass = 0;

static void expect_true(const char *what, bool ok)
{
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        printf("FAIL %s\n", what);
    }
}

/* tNMEA2000 with a frame queue where the TWAI controller would be. */
class HostBus : public tNMEA2000 {
public:
    std::deque<N2kFrame> frames;

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
        return true;
    }
};

static HostBus g_bus;
static std::map<unsigned long, int> g_sent;     /* messages started on the wire, per PGN */
static std::map<unsigned long, int> g_received; /* messages the library delivered, per PGN */
static std::vector<std::string> g_sentences;
static std::vector<N2kFrame> g_capture;

static unsigned long pgn_of(uint32_t id)
{
    const unsigned long pf = (id >> 16) & 0xff;
    unsigned long pgn = (id >> 8) & 0x3ffff;
    if (pf < 240) {
        pgn &= 0x3ff00;
    }
    return pgn;
}

static bool is_fast(unsigned long pgn)
{
    return pgn == 129029UL || pgn == 129038UL || pgn == 129039UL || pgn == 129794UL ||
           pgn == 129809UL || pgn == 129810UL;
}

static bool to_bus(const N2kFrame &frame, void *)
{
    const unsigned long pgn = pgn_of(frame.id);
    /* A message starts with a single frame, or fast-packet frame index 0. */
    if (!is_fast(pgn) || (frame.data[0] & 0x1f) == 0) {
        ++g_sent[pgn];
    }
    g_bus.frames.push_back(frame);
    return true;
}

static bool capture(const N2kFrame &frame, void *)
{
    g_capture.push_back(frame);
    return true;
}

static void on_msg(const tN2kMsg &msg)
{
    ++g_received[msg.PGN];
    n2k_to_0183_handle(msg);
}

static void on_sentence(const char *sentence, size_t, void *)
{
    g_sentences.emplace_back(sentence);
}

static void drain(void)
{
    while (!g_bus.frames.empty()) {
        g_bus.ParseMessages();
    }
}

static void test_framing(void)
{
    tN2kMsg m;
    SetN2kPGN128267(m, 1, 12.3, 0.4);
    m.Source = 12;
    g_capture.clear();
    expect_true("single frame count", n2k_frames_from_msg(m, 0, capture, nullptr) == 1);
    expect_true("single frame id", g_capture.size() == 1 && g_capture[0].id == ((3u << 26) | (128267u << 8) | 12u));
    expect_true("single frame length", !g_capture.empty() && g_capture[0].len == m.DataLen);

    SetN2kPGN129029(m, 1, 20635, 64800.0, 49.28, -123.19, 2.0, N2kGNSSt_GPS, N2kGNSSm_GNSSfix, 10, 0.8,
                    1.4, -17.0);
    m.Source = 35;
    g_capture.clear();
    const size_t frames = n2k_frames_from_msg(m, 5, capture, nullptr);
    const size_t want = 1 + (static_cast<size_t>(m.DataLen) - 6 + 6) / 7;
    expect_true("fast packet frame count", frames == want && g_capture.size() == want);
    expect_true("fast packet frame 0 carries the length", !g_capture.empty() && g_capture[0].data[1] == m.DataLen);
    bool seq_ok = true;
    for (size_t i = 0; i < g_capture.size(); ++i) {
        seq_ok = seq_ok && (g_capture[i].data[0] >> 5) == 5 && (g_capture[i].data[0] & 0x1f) == i;
    }
    expect_true("fast packet sequence and index", seq_ok);
    expect_true("fast packet pads with 0xff", !g_capture.empty() && g_capture.back().data[7] == 0xff);

    expect_true("PDU1 id keeps the destination", n2k_can_id(6, 59904, 22, 0x33) == ((6u << 26) | (0xEA33u << 8) | 22u));
}

static bool checksum_ok(const std::string &s)
{
    const size_t star = s.rfind('*');
    if (star == std::string::npos || star + 3 > s.size()) {
        return false;
    }
    uint8_t x = 0;
    for (size_t i = 1; i < star; ++i) {
        x ^= static_cast<uint8_t>(s[i]);
    }
    return strtoul(s.substr(star + 1, 2).c_str(), nullptr, 16) == x;
}

static int count_prefix(const char *prefix)
{
    int n = 0;
    for (const auto &s : g_sentences) {
        if (s.compare(0, strlen(prefix), prefix) == 0) {
            ++n;
        }
    }
    return n;
}

static double field(const std::string &s, int index)
{
    size_t at = 0;
    for (int i = 0; i < index; ++i) {
        at = s.find(',', at);
        if (at == std::string::npos) {
            return NAN;
        }
        ++at;
    }
    return atof(s.c_str() + at);
}

static void test_run(void)
{
    nmea_init();
    nmea_set_sink(on_sentence, nullptr);
    g_bus.SetMode(tNMEA2000::N2km_ListenOnly, 22);
    g_bus.EnableForward(false);
    g_bus.SetN2kCANMsgBufSize(32);
    g_bus.SetMsgHandler(on_msg);
    /* The library waits 200 ms after opening. On Linux it times that with the
     * real clock, elsewhere with millis(), so let both pass. */
    g_bus.Open();
    for (int i = 0; i < 50 && !g_bus.IsOpen(); ++i) {
        g_now += 10;
        usleep(10000);
        g_bus.Open();
    }
    expect_true("library stack opens", g_bus.IsOpen());

    n2k_sim_reset();
    n2k_sim_set_gnss(true);
    size_t frames = 0;
    const uint32_t kRunMs = 120000;
    for (g_now = 1000; g_now <= 1000 + kRunMs; g_now += 10) {
        frames += n2k_sim_step(g_now, to_bus, nullptr);
        drain();
        n2k_to_0183_poll();
    }
    printf("sim: %zu frames, %.1f frames/s over %u s\n", frames, frames * 1000.0 / kRunMs, kRunMs / 1000);

    bool all = true;
    for (const auto &kv : g_sent) {
        const int got = g_received[kv.first];
        if (got != kv.second) {
            all = false;
            printf("  PGN %lu sent %d received %d\n", kv.first, kv.second, got);
        }
    }
    expect_true("every simulated message is reassembled", all);
    expect_true("position rapid at 10 Hz", std::abs(g_sent[129025UL] - 1200) <= 2);
    expect_true("heading at 10 Hz", std::abs(g_sent[127250UL] - 1200) <= 2);
    expect_true("COG/SOG at 4 Hz", std::abs(g_sent[129026UL] - 480) <= 2);
    expect_true("GNSS at 1 Hz", std::abs(g_sent[129029UL] - 120) <= 1);
    expect_true("AIS class A positions", g_sent[129038UL] >= 20);
    expect_true("AIS class A static", g_sent[129794UL] >= 2);
    expect_true("AIS class B position", g_sent[129039UL] >= 3);
    expect_true("AIS class B static A and B", g_sent[129809UL] >= 1 && g_sent[129810UL] >= 1);
    expect_true("battery and sea temperature", g_sent[127508UL] >= 50 && g_sent[130316UL] >= 50);
    expect_true("15 PGN types", g_sent.size() == 15);

    bool sums = true;
    for (const auto &s : g_sentences) {
        sums = sums && checksum_ok(s);
    }
    expect_true("every sentence has a valid checksum", sums && !g_sentences.empty());
    expect_true("RMC about once a second", std::abs(count_prefix("$GPRMC") - 120) <= 2);
    expect_true("GGA, DPT, MWV, HDT, VHW present",
                count_prefix("$GPGGA") > 100 && count_prefix("$SDDPT") > 100 && count_prefix("$WIMWV") > 100 &&
                    count_prefix("$HEHDT") > 100 && count_prefix("$VWVHW") > 100);
    expect_true("AIS sentences", count_prefix("!AIVDM") >= 25);

    bool rmc_ok = true, depth_ok = true, wind_ok = true;
    int rmc_seen = 0;
    for (const auto &s : g_sentences) {
        /* The first second can carry position before speed and time arrive. */
        if (s.compare(0, 6, "$GPRMC") == 0 && ++rmc_seen > 2) {
            const double lat = field(s, 3);
            const double sog = field(s, 7);
            const bool ok = s.find(",A,") != std::string::npos && lat > 4915.0 && lat < 4919.0 && sog > 3.5 &&
                            sog < 8.5;
            if (!ok && rmc_ok) {
                printf("  first bad RMC: %s\n", s.c_str());
            }
            rmc_ok = rmc_ok && ok;
        } else if (s.compare(0, 6, "$SDDPT") == 0) {
            const double d = field(s, 1);
            depth_ok = depth_ok && d >= 4.0 && d <= 45.0;
        } else if (s.compare(0, 6, "$WIMWV") == 0) {
            const double angle = field(s, 1);
            const double speed = field(s, 3);
            wind_ok = wind_ok && angle >= 0.0 && angle < 360.0 && speed > 5.0 && speed < 25.0;
        }
    }
    expect_true("RMC valid in English Bay at sailing speed", rmc_ok);
    expect_true("depth between 4 and 45 m", depth_ok);
    expect_true("apparent wind is sensible", wind_ok);

    /* Phone GNSS takes over: the GPS PGNs stop, the rest keep coming. */
    g_sent.clear();
    n2k_sim_set_gnss(false);
    for (uint32_t end = g_now + 10000; g_now <= end; g_now += 10) {
        n2k_sim_step(g_now, to_bus, nullptr);
        drain();
    }
    expect_true("GNSS off stops GPS PGNs",
                g_sent[129025UL] == 0 && g_sent[129026UL] == 0 && g_sent[129029UL] == 0 && g_sent[126992UL] == 0);
    expect_true("GNSS off keeps depth and wind", g_sent[128267UL] >= 9 && g_sent[130306UL] >= 18);
}

int main(void)
{
    test_framing();
    test_run();
    printf("n2k sim: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
