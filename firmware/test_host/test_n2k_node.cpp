/* Host test: the terminal as an NMEA 2000 node.
 *
 * The terminal's identity (n2k_node.cpp) runs on the real NMEA2000 library
 * on a virtual bus with a second node playing the chart plotter. Simulated
 * time drives the library's timers. Checks the behaviour the standard
 * requires and what the terminal must never do:
 *  - ISO Address Claim with the right NAME, defended and given up to a
 *    higher-priority NAME, the new address saved and restored at start;
 *  - answers to ISO requests: product information, configuration
 *    information, PGN list, address claim, NAK for what it does not send;
 *  - heartbeat every 60 s;
 *  - nothing on the bus but network-management PGNs. */

#include "n2k_node.h"

#include <NMEA2000.h>
#include <N2kMessages.h>

#include <Preferences.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <string>
#include <vector>

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

struct Frame {
    int from;
    uint32_t t;
    unsigned long id;
    unsigned char len;
    unsigned char data[8];
};

class VirtualNode;
static std::vector<VirtualNode *> g_nodes;
static std::vector<Frame> g_wire;

class VirtualNode : public tNMEA2000 {
public:
    explicit VirtualNode(int tag) : tag_(tag) {}
    std::deque<Frame> inbox;

protected:
    bool CANSendFrame(unsigned long id, unsigned char len, const unsigned char *buf, bool) override
    {
        Frame f = {tag_, g_now, id, len, {}};
        memcpy(f.data, buf, len);
        g_wire.push_back(f);
        for (VirtualNode *n : g_nodes) {
            if (n != this) {
                n->inbox.push_back(f);
            }
        }
        return true;
    }
    bool CANOpen(void) override { return true; }
    bool CANGetFrame(unsigned long &id, unsigned char &len, unsigned char *buf) override
    {
        if (inbox.empty()) {
            return false;
        }
        const Frame f = inbox.front();
        inbox.pop_front();
        id = f.id;
        len = f.len;
        memcpy(buf, f.data, f.len);
        return true;
    }

private:
    int tag_;
};

static constexpr int kTerminal = 1;
static constexpr int kPlotter = 2;

static unsigned long pgn_of(unsigned long id)
{
    const unsigned long pf = (id >> 16) & 0xff;
    unsigned long pgn = (id >> 8) & 0x3ffff;
    if (pf < 240) {
        pgn &= 0x3ff00;
    }
    return pgn;
}

/* What the plotter heard, reassembled. */
static std::vector<tN2kMsg> g_heard;

static void plotter_heard(const tN2kMsg &msg) { g_heard.push_back(msg); }

static void run(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; ++i) {
        ++g_now;
        for (VirtualNode *n : g_nodes) {
            n->ParseMessages();
        }
    }
}

static const tN2kMsg *heard(unsigned long pgn, unsigned char src)
{
    for (auto it = g_heard.rbegin(); it != g_heard.rend(); ++it) {
        if (it->PGN == pgn && it->Source == src) {
            return &*it;
        }
    }
    return nullptr;
}

static uint64_t name_of(const tN2kMsg &claim)
{
    uint64_t name = 0;
    for (int i = 7; i >= 0; --i) {
        name = (name << 8) | claim.Data[i];
    }
    return name;
}

static void request(VirtualNode &from, unsigned char dest, unsigned long pgn)
{
    tN2kMsg m;
    SetN2kPGN59904(m, dest, pgn);
    from.SendMsg(m);
}

int main()
{
    VirtualNode terminal(kTerminal);
    VirtualNode plotter(kPlotter);

    /* ---- the terminal joins the bus ---- */
    n2k_node_configure(terminal, true);
    terminal.SetN2kCANSendFrameBufSize(64);
    terminal.EnableForward(false);
    g_nodes.push_back(&terminal);
    terminal.Open();
    run(1000);

    unsigned char addr = terminal.GetN2kSource();
    expect_true("first boot claims 22", addr == 22);
    const Frame *claim_frame = nullptr;
    for (const Frame &f : g_wire) {
        if (f.from == kTerminal && pgn_of(f.id) == 60928UL) {
            claim_frame = &f;
            break;
        }
    }
    expect_true("address claim sent", claim_frame != nullptr && (claim_frame->id & 0xff) == 22);
    if (claim_frame != nullptr) {
        uint64_t name = 0;
        for (int i = 7; i >= 0; --i) {
            name = (name << 8) | claim_frame->data[i];
        }
        expect_true("NAME unique number from MAC", (name & 0x1fffff) == (0x72d648u & 0x1fffffu));
        expect_true("NAME manufacturer code 2046", ((name >> 21) & 0x7ff) == 2046);
        expect_true("NAME function 130 PC gateway", ((name >> 40) & 0xff) == 130);
        expect_true("NAME class 25 internetwork", ((name >> 49) & 0x7f) == 25);
        expect_true("NAME industry group 4 marine", ((name >> 60) & 0x7) == 4);
        expect_true("NAME arbitrary address capable", (name >> 63) == 1);
    }

    /* ---- a plotter joins and asks the standard questions ---- */
    plotter.SetProductInformation("1", 1234, "Test plotter", "1.0", "1");
    /* Garmin's code: a lower NAME than 2046, so it wins any contention. */
    plotter.SetDeviceInformation(1, 130, 120, 229, 4);
    plotter.SetMode(tNMEA2000::N2km_ListenAndNode, 40);
    plotter.EnableForward(false);
    plotter.SetMsgHandler(plotter_heard);
    g_nodes.push_back(&plotter);
    plotter.Open();
    run(1000);
    expect_true("plotter on 40", plotter.GetN2kSource() == 40);
    expect_true("terminal keeps 22", terminal.GetN2kSource() == 22);

    request(plotter, 22, 126996UL);
    run(500);
    const tN2kMsg *product = heard(126996UL, 22);
    expect_true("answers product information", product != nullptr);
    if (product != nullptr) {
        unsigned short version = 0, code = 0;
        char model[33] = {}, sw[41] = {}, model_ver[33] = {}, serial[33] = {};
        unsigned char cert = 0, len = 0;
        ParseN2kPGN126996(*product, version, code, sizeof(model), model, sizeof(sw), sw, sizeof(model_ver),
                          model_ver, sizeof(serial), serial, cert, len);
        expect_true("model ID", strcmp(model, "LMYC Boat Terminal") == 0);
        expect_true("model version", strcmp(model_ver, "Waveshare ESP32-S3-Touch-LCD-7") == 0);
        expect_true("serial is the MAC", strcmp(serial, "58E6C572D648") == 0);
        expect_true("LEN 10 (0.5 A)", len == 10);
        expect_true("NMEA 2000 version 2.101", version == 2101);
        expect_true("product code from the build", code == 666);
    }

    request(plotter, 22, 126998UL);
    run(500);
    const tN2kMsg *config = heard(126998UL, 22);
    expect_true("answers configuration information", config != nullptr);
    if (config != nullptr) {
        char d1[72] = {}, d2[72] = {}, mfr[72] = {};
        size_t l1 = sizeof(d1), l2 = sizeof(d2), l3 = sizeof(mfr);
        ParseN2kPGN126998(*config, l3, mfr, l1, d1, l2, d2);
        expect_true("installation description 1 is the boat name", strcmp(d1, "BENCH-01") == 0);
        expect_true("manufacturer information", strcmp(mfr, "LMYC boat-terminal") == 0);
    }

    request(plotter, 22, 126464UL);
    run(500);
    expect_true("answers PGN list", heard(126464UL, 22) != nullptr);

    g_heard.clear();
    request(plotter, 0xff, 60928UL);
    run(500);
    expect_true("answers a global request for address claims", heard(60928UL, 22) != nullptr);

    request(plotter, 22, 129025UL);
    run(500);
    const tN2kMsg *nak = heard(59392UL, 22);
    expect_true("NAKs a PGN it does not send", nak != nullptr && nak->Data[0] == 1);

    /* ---- heartbeat ---- */
    run(125000);
    std::vector<uint32_t> beats;
    for (const Frame &f : g_wire) {
        if (f.from == kTerminal && pgn_of(f.id) == 126993UL) {
            beats.push_back(f.t);
        }
    }
    /* The library's scheduler sync is static and shared: the plotter's Open()
     * re-synced the terminal once, so only the steady interval is checked. */
    const size_t nb = beats.size();
    const bool every_minute =
        nb >= 2 && beats[nb - 1] - beats[nb - 2] >= 59900 && beats[nb - 1] - beats[nb - 2] <= 60100;
    if (!every_minute) {
        for (uint32_t t : beats) {
            printf("  heartbeat at %u ms\n", t);
        }
    }
    expect_true("heartbeat every 60 s", every_minute);

    /* ---- contention ---- */
    /* NAMEs compare as 64-bit numbers, highest bits first, so device class
     * outranks manufacturer: the plotter (class 120) loses to the terminal
     * (class 25) and must move. */
    plotter.SetN2kSource(22);
    plotter.SendIsoAddressClaim();
    run(1000);
    expect_true("terminal defends 22 against a higher NAME", terminal.GetN2kSource() == 22);
    expect_true("plotter moves off 22", plotter.GetN2kSource() != 22 && plotter.GetN2kSource() < 252);

    /* A safety device (class 20) has the lower NAME: the terminal yields. */
    VirtualNode safety(3);
    safety.SetProductInformation("2", 99, "Test safety", "1.0", "1");
    safety.SetDeviceInformation(7, 130, 20, 229, 4);
    safety.SetMode(tNMEA2000::N2km_ListenAndNode, 22);
    safety.EnableForward(false);
    g_nodes.push_back(&safety);
    n2k_node_save_changes(terminal);
    const int writes_before = PrefsStore::get().writes;
    safety.Open();
    run(1000);
    addr = terminal.GetN2kSource();
    expect_true("terminal gives 22 to a lower NAME", addr != 22 && addr < 252);
    expect_true("safety device holds 22", safety.GetN2kSource() == 22);
    expect_true("new address saved", n2k_node_save_changes(terminal) && PrefsStore::get().writes > writes_before);

    /* ---- restart: the saved address comes back ---- */
    VirtualNode again(kTerminal);
    n2k_node_configure(again, true);
    g_nodes.clear();
    g_nodes.push_back(&again);
    again.Open();
    run(1000);
    expect_true("restart claims the saved address first", again.GetN2kSource() == addr);

    /* ---- never anything but network management ---- */
    static const std::set<unsigned long> kManagement = {59392UL,  59904UL,  60928UL,  126208UL,
                                                        126464UL, 126993UL, 126996UL, 126998UL};
    std::set<unsigned long> sent;
    for (const Frame &f : g_wire) {
        if (f.from == kTerminal) {
            sent.insert(pgn_of(f.id));
        }
    }
    bool only = true;
    for (unsigned long p : sent) {
        if (kManagement.count(p) == 0) {
            only = false;
            printf("  terminal sent PGN %lu\n", p);
        }
    }
    expect_true("terminal sends only network-management PGNs", only);

    printf("n2k node: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
