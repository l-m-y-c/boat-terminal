/* Host test: network discovery. The terminal (its real n2k_node identity on
 * the NMEA2000 library) and the simulator's six virtual devices share a
 * virtual bus. The terminal asks, the devices answer, and the topology must
 * come out complete; then the health checks against devices that misbehave. */

#include "n2k_diag.h"
#include "n2k_network.h"
#include "n2k_node.h"
#include "n2k_sim.h"
#include "n2k_to_0183.h"
#include "nmea.h"

#include <NMEA2000.h>
#include <N2kMessages.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

static uint32_t g_now = 1;
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

static std::deque<N2kFrame> g_to_terminal;
static uint32_t g_requests_sent = 0;

static bool to_terminal(const N2kFrame &f, void *)
{
    g_to_terminal.push_back(f);
    return true;
}

class Terminal : public tNMEA2000 {
public:
    bool fast(unsigned long pgn) { return IsFastPacketPGN(pgn); }
    bool ready(void) const { return IsReadyToSend(); }

protected:
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool) override
    {
        N2kFrame f = {};
        f.id = id;
        f.len = len;
        memcpy(f.data, buf, len);
        if (((id >> 8) & 0x3ff00u) == 0xEA00u) {
            ++g_requests_sent;
        }
        n2k_sim_receive(f, g_now, to_terminal, nullptr);
        return true;
    }
    bool CANOpen(void) override { return true; }
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override
    {
        if (g_to_terminal.empty()) {
            return false;
        }
        const N2kFrame f = g_to_terminal.front();
        g_to_terminal.pop_front();
        id = f.id;
        len = f.len;
        memcpy(buf, f.data, f.len);
        n2k_diag_frame(f.id, f.len, f.data, fast(n2k_diag_pgn(f.id)), g_now);
        n2k_net_frame(static_cast<uint8_t>(f.id & 0xff), g_now);
        return true;
    }
};

static Terminal g_terminal;

static void on_msg(const tN2kMsg &msg)
{
    n2k_diag_message(msg, g_now);
    n2k_net_message(msg, g_now);
    n2k_to_0183_handle(msg);
}

static void self_update(void)
{
    N2kNetSelf me = {};
    me.address = g_terminal.GetN2kSource();
    me.unique = n2k_node_unique_number();
    me.manufacturer = n2k_node_manufacturer_code();
    me.device_class = kN2kNodeDeviceClass;
    me.function = kN2kNodeDeviceFunction;
    me.load_equivalency = kN2kNodeLoadEquivalency;
    me.model = kN2kNodeModelId;
    me.software = "test";
    me.model_version = "host";
    me.serial = n2k_node_serial();
    me.install1 = "BENCH-01";
    n2k_net_set_self(me);
}

static void run(uint32_t ms, bool sim_data = true)
{
    for (uint32_t i = 0; i < ms; ++i) {
        ++g_now;
        if (sim_data) {
            n2k_sim_step(g_now, to_terminal, nullptr);
        }
        g_terminal.ParseMessages();
        if (g_now % 1000 == 0) {
            self_update();
            n2k_diag_tick(g_now);
        }
        uint8_t dst = 0;
        unsigned long pgn = 0;
        const bool ready = g_terminal.ready() && g_terminal.GetN2kSource() < 252;
        if (n2k_net_next_request(g_now, ready, &dst, &pgn)) {
            tN2kMsg req;
            SetN2kPGN59904(req, dst, pgn);
            g_terminal.SendMsg(req);
        }
    }
}

static const N2kNetView *find(const N2kNetView *v, size_t n, uint8_t addr)
{
    for (size_t i = 0; i < n; ++i) {
        if (v[i].address == addr) {
            return &v[i];
        }
    }
    return nullptr;
}

static void inject(const tN2kMsg &m)
{
    n2k_frames_from_msg(m, 0, to_terminal, nullptr);
}

int main()
{
    nmea_init();
    n2k_diag_reset();
    n2k_net_reset();
    n2k_sim_reset();
    n2k_node_configure(g_terminal, true);
    g_terminal.SetN2kCANSendFrameBufSize(64);
    g_terminal.EnableForward(false);
    g_terminal.SetMsgHandler(on_msg);
    g_terminal.Open();
    run(10000);

    N2kNetView v[40];
    size_t n = n2k_net_view(v, 40, g_now);
    expect_true("terminal plus six simulated devices", n == 7);
    expect_true("the terminal is listed first", n > 0 && v[0].self && v[0].address == 22);
    const N2kNetView *depth = find(v, n, 12);
    expect_true("transducer found", depth != nullptr);
    if (depth != nullptr) {
        expect_true("transducer model from 126996", strcmp(depth->title, "Sim depth / speed / temp") == 0);
        expect_true("transducer role from its NAME", strcmp(depth->role, "Depth / speed / temp") == 0);
        expect_true("installation text from 126998", strcmp(depth->install, "Sim: forward of the keel") == 0);
        expect_true("transducer LEN", depth->load_equivalency == 1);
        expect_true("transducer healthy", depth->issues == 0);
    }
    bool healthy = true;
    for (size_t i = 0; i < n; ++i) {
        healthy = healthy && v[i].issues == 0;
    }
    expect_true("no issues on the simulated network", healthy);
    expect_true("LEN total 10 + 1+1+2+1+1+3", n2k_net_len_total() == 19);
    expect_true("sorted by class: navigation before environment", find(v, n, 7) > find(v, n, 35));
    const uint32_t settled = g_requests_sent;
    run(5000);
    expect_true("nothing asked twice once answered", g_requests_sent == settled);

    static char json[49152];
    size_t len = n2k_net_json(json, sizeof(json), g_now);
    std::string j(json, len);
    expect_true("network json built", len > 0);
    expect_true("network json has the AIS receiver", j.find("\"model\":\"Sim AIS receiver\"") != std::string::npos);
    expect_true("transmit list in json", j.find("\"transmits\":[128267,128259,130316") != std::string::npos);
    expect_true("everything complete", j.find("\"complete\":false") == std::string::npos);

    len = n2k_diag_device_json(json, sizeof(json), 12, g_now);
    j.assign(json, len);
    expect_true("device json: depth decoded", j.find("\"text\":\"Depth ") != std::string::npos);
    expect_true("device json: recent messages", j.find("\"recent\":[{") != std::string::npos);
    len = n2k_net_device_json(json, sizeof(json), 12, g_now);
    j.assign(json, len);
    expect_true("device identity json", j.find("\"role\":\"Depth / speed / temp\"") != std::string::npos);

    /* ---- misbehaving devices ---- */
    /* A second GPS, same class / function / instance as the simulated one. */
    tN2kMsg claim;
    SetN2kPGN60928(claim, 777, 2046, 145, 60, 0, 0, 4);
    claim.Source = 50;
    claim.Destination = 0xff;
    inject(claim);
    tN2kMsg beat;
    SetN2kPGN126993(beat, 1000, 0);
    beat.Source = 50;
    beat.Destination = 0xff;
    inject(beat);
    run(9000);
    n = n2k_net_view(v, 40, g_now);
    const N2kNetView *second = find(v, n, 50);
    const N2kNetView *gps = find(v, n, 35);
    expect_true("second GPS found", second != nullptr);
    if (second != nullptr && gps != nullptr) {
        expect_true("instance conflict on both", (second->issues & kNetInstanceConflict) && (gps->issues & kNetInstanceConflict));
        expect_true("heartbeat lost after 2.5 intervals", second->issues & kNetHeartbeatLost);
        expect_true("no product information after three tries", second->issues & kNetNoProduct);
    }
    run(2000);
    n = n2k_net_view(v, 40, g_now);
    second = find(v, n, 50);
    expect_true("silent after 10 s", second != nullptr && (second->issues & kNetSilent));

    /* The simulated GPS moves to address 60: same NAME, new address. */
    SetN2kPGN60928(claim, 1001, 2046, 145, 60, 0, 0, 4);
    claim.Source = 60;
    inject(claim);
    run(50, false);
    n = n2k_net_view(v, 40, g_now);
    const N2kNetView *moved = find(v, n, 60);
    expect_true("moved device kept its identity", moved != nullptr && strcmp(moved->title, "Sim GPS") == 0);
    expect_true("address change flagged", moved != nullptr && (moved->issues & kNetAddressChanged));
    expect_true("old address gone", find(v, n, 35) == nullptr);

    /* Refresh asks everyone again. */
    const uint32_t before = g_requests_sent;
    n2k_net_refresh();
    run(5000);
    expect_true("refresh asks again", g_requests_sent > before + 6);

    /* ---- the broadcast sweep: boot (once claimed) and Refresh only ---- */
    {
        n2k_net_reset();
        uint8_t dst = 0;
        unsigned long pgn = 0;
        uint32_t t = 100000;
        bool any = false;
        for (int i = 0; i < 200; ++i, t += 50) {
            any = any || n2k_net_next_request(t, false, &dst, &pgn);
        }
        expect_true("no request while the address is unclaimed", !any);
        const bool first = n2k_net_next_request(t, true, &dst, &pgn);
        expect_true("boot sweep once claimed", first && dst == 0xff && pgn == 60928UL);
        int broadcasts = 0;
        for (int i = 0; i < 6000; ++i, t += 50) { /* five minutes */
            if (n2k_net_next_request(t, true, &dst, &pgn) && dst == 0xff) {
                ++broadcasts;
            }
        }
        expect_true("no periodic broadcast sweep", broadcasts == 0);
        n2k_net_refresh();
        any = false;
        for (int i = 0; i < 200; ++i, t += 50) {
            any = any || n2k_net_next_request(t, false, &dst, &pgn);
        }
        expect_true("refresh waits while unclaimed", !any);
        const bool again = n2k_net_next_request(t, true, &dst, &pgn);
        expect_true("refresh sweeps once claimed", again && dst == 0xff && pgn == 60928UL);
        broadcasts = 0;
        for (int i = 0; i < 6000; ++i, t += 50) {
            if (n2k_net_next_request(t, true, &dst, &pgn) && dst == 0xff) {
                ++broadcasts;
            }
        }
        expect_true("one sweep per refresh", broadcasts == 0);
    }

    printf("n2k network: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
