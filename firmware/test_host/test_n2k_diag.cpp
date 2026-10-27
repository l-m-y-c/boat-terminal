/* Host test: bus diagnostics. Hint choice, device names from the address
 * claim, fast-packet completion counting, and the /n2k.json shape. */

#include "n2k_diag.h"
#include "n2k_sim.h"
#include "n2k_to_0183.h"
#include "nmea.h"

#include <N2kMessages.h>

#include <cstdio>
#include <cstring>
#include <string>

static uint32_t g_now = 1000;
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

static N2kHintInput live(void)
{
    N2kHintInput in = {};
    in.source = DataSource::Nmea2000;
    in.state = BusState::Live;
    in.node = NodeState::Claimed;
    in.frames_per_s = 200;
    return in;
}

static void test_hints(void)
{
    N2kHintInput in = live();
    expect_true("healthy bus: ok", n2k_hint_pick(in) == N2kHint::None);

    /* A transmitter getting no ACK: its frame back to back. */
    in.frames_per_s = 1600;
    in.repeats_per_s = 1590;
    expect_true("repeat flood: repeats", n2k_hint_pick(in) == N2kHint::Repeats);
    expect_true("repeats code", strcmp(n2k_hint_code(N2kHint::Repeats), "repeats") == 0);
    expect_true("no hint mentions ACK mode", strstr(n2k_hint_text(N2kHint::Repeats), "Turn on") == nullptr);

    in = live();
    in.node = NodeState::NoAddress;
    expect_true("no free address", n2k_hint_pick(in) == N2kHint::NoAddress);
    in = live();
    in.state = BusState::BusOff;
    expect_true("bus-off", n2k_hint_pick(in) == N2kHint::BusOff);

    in = live();
    in.frames_per_s = 0;
    in.bus_errors_per_s = 40;
    in.state = BusState::Waiting;
    expect_true("errors, no frames", n2k_hint_pick(in) == N2kHint::ErrorsNoFrames);
    in.bus_errors_per_s = 0;
    expect_true("silence: no_frames", n2k_hint_pick(in) == N2kHint::NoFrames);

    in = live();
    in.fast_starts_per_s = 20;
    in.fast_incomplete_per_s = 3;
    expect_true("15% fast packets broken", n2k_hint_pick(in) == N2kHint::FastIncomplete);
    in.fast_incomplete_per_s = 1;
    expect_true("5% is not flagged", n2k_hint_pick(in) == N2kHint::None);

    in = live();
    in.two_sources = true;
    expect_true("two sources is the least urgent", n2k_hint_pick(in) == N2kHint::TwoSources);
    in.bad_per_s = 1;
    expect_true("bad values before two sources", n2k_hint_pick(in) == N2kHint::BadValues);

    in = live();
    in.source = DataSource::Simulator;
    in.repeats_per_s = 1000;
    expect_true("simulator never hints", n2k_hint_pick(in) == N2kHint::None);
}

static uint32_t can_id(uint32_t pgn, uint8_t src) { return n2k_can_id(6, pgn, src, 0xff); }

static void test_devices_and_fast_packets(void)
{
    n2k_diag_reset();
    /* Address claim from a Garmin display at 35: unique 12345, maker 229,
     * function 130, class 120. */
    const uint64_t name = 12345ull | (229ull << 21) | (130ull << 40) | (120ull << 49) | (1ull << 63);
    tN2kMsg claim;
    claim.SetPGN(60928L);
    claim.Source = 35;
    claim.Destination = 0xff;
    for (int i = 0; i < 8; ++i) {
        claim.AddByte(static_cast<unsigned char>(name >> (8 * i)));
    }
    n2k_diag_message(claim, g_now);

    /* 126996: model ID at byte 4. */
    tN2kMsg product;
    product.SetPGN(126996L);
    product.Source = 35;
    product.Add2ByteUInt(2100);
    product.Add2ByteUInt(1234);
    product.AddStr("GPSMAP 740s", 32);
    n2k_diag_message(product, g_now);

    /* Five 129029 messages started; the third never completes. */
    const uint32_t id = can_id(129029UL, 35);
    expect_true("PGN from id", n2k_diag_pgn(id) == 129029UL);
    for (int i = 0; i < 5; ++i) {
        const uint8_t first[8] = {static_cast<uint8_t>(i << 5), 43, 0, 0, 0, 0, 0, 0};
        n2k_diag_frame(id, 8, first, true, g_now);
        const uint8_t second[8] = {static_cast<uint8_t>((i << 5) | 1), 0, 0, 0, 0, 0, 0, 0};
        n2k_diag_frame(id, 8, second, true, g_now);
        if (i != 2) {
            tN2kMsg m;
            m.SetPGN(129029L);
            m.Source = 35;
            m.AddByte(0);
            n2k_diag_message(m, g_now);
        }
    }
    expect_true("fast starts counted", n2k_diag_fast_starts() == 5);
    expect_true("one fast packet incomplete", n2k_diag_fast_incomplete() == 1);

    /* A PDU1 PGN keeps its PGN, not the destination byte. */
    expect_true("PDU1 PGN", n2k_diag_pgn(n2k_can_id(3, 59904UL, 10, 35)) == 59904UL);

    N2kStats s = {};
    s.source = DataSource::Nmea2000;
    s.state = BusState::Live;
    s.hint = N2kHint::Repeats;
    s.node = NodeState::Claimed;
    s.address = 23;
    s.unique_number = 12345;
    s.manufacturer_code = 2046;
    static char json[16384];
    const size_t n = n2k_diag_json(json, sizeof(json), s, g_now + 500);
    expect_true("json built", n > 0 && json[n - 1] == '\n');
    const std::string j(json);
    expect_true("maker Garmin", j.find("\"maker\":\"Garmin\"") != std::string::npos);
    expect_true("class Display", j.find("\"class_name\":\"Display\"") != std::string::npos);
    expect_true("model from 126996", j.find("\"model\":\"GPSMAP 740s\"") != std::string::npos);
    expect_true("hint in bus", j.find("\"hint\":\"repeats\"") != std::string::npos);
    expect_true("self node", j.find("\"self\":{\"node\":\"claimed\",\"address\":23,\"unique\":12345,\"mfr\":2046") != std::string::npos);
    expect_true("fast row", j.find("\"pgn\":129029,\"name\":\"GNSS position\",\"src\":35,\"fast\":true,"
                                   "\"frames\":10,\"starts\":5,\"msgs\":4") != std::string::npos);
    int depth = 0;
    bool balanced = true;
    for (char c : j) {
        depth += (c == '{' || c == '[') ? 1 : (c == '}' || c == ']') ? -1 : 0;
        balanced = balanced && depth >= 0;
    }
    expect_true("json brackets balance", balanced && depth == 0);

    expect_true("too small a buffer gives 0", n2k_diag_json(json, 64, s, g_now) == 0);
}

int main()
{
    nmea_init();
    test_hints();
    test_devices_and_fast_packets();
    printf("n2k diag: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
