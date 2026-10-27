#pragma once

#include <cstddef>
#include <cstdint>

class tN2kMsg;

/* The NMEA 2000 network as the standard lets a node see it: every device's
 * address and NAME (60928), product information (126996), configuration
 * information with the installer's location text (126998), PGN lists
 * (126464) and heartbeat (126993). CAN carries no position, so this is the
 * logical network, not the order of the tees.
 *
 * The terminal asks for what it has not heard, the way a chart plotter
 * builds its device list: one global ISO request for address claims, then
 * one addressed request at a time for the rest, 150 ms apart, three tries
 * each, nothing asked twice once answered. Node task only; n2k.cpp holds
 * the lock that the web server and the screen read under. */

/* Health of one device. A device can have several. */
enum N2kNetIssue : uint16_t {
    kNetNoName = 1u << 0,           /* never answered for its address claim */
    kNetNoProduct = 1u << 1,        /* never sent product information */
    kNetHeartbeatLost = 1u << 2,    /* heartbeat stopped (2.5 intervals) */
    kNetAddressChanged = 1u << 3,   /* moved address since it was first seen */
    kNetInstanceConflict = 1u << 4, /* same class, function and instance as another device */
    kNetSilent = 1u << 5,           /* nothing at all for 10 s */
};

void n2k_net_reset(void);

/* Every frame: a source not seen before becomes a device to ask about. */
void n2k_net_frame(uint8_t src, uint32_t now_ms);

/* Every reassembled message: claims, product and configuration
 * information, PGN lists, heartbeats, and NAKs to the terminal's requests. */
void n2k_net_message(const tN2kMsg &msg, uint32_t now_ms);

/* The terminal itself, listed with the others. */
struct N2kNetSelf {
    uint8_t address; /* 254 while it has none */
    uint32_t unique;
    uint16_t manufacturer;
    uint8_t device_class;
    uint8_t function;
    uint8_t load_equivalency;
    const char *model;
    const char *software;
    const char *model_version;
    const char *serial;
    const char *install1;
};
void n2k_net_set_self(const N2kNetSelf &self);

/* Node task, every pass: the next ISO request to send, if one is due.
 * can_send: the node holds an address. Returns false when nothing is due. */
bool n2k_net_next_request(uint32_t now_ms, bool can_send, uint8_t *dst, unsigned long *pgn);

/* Ask everyone again, from the address claims up (the Refresh button). */
void n2k_net_refresh(void);

/* A device for the topology map, in drawing order: the terminal first, then
 * by class and address. */
struct N2kNetView {
    uint8_t address;
    bool self;
    uint8_t device_class;
    uint8_t function;
    uint8_t load_equivalency; /* 0xff unknown */
    uint16_t issues;
    uint32_t age_ms;
    char title[40];   /* "Garmin GPSMAP 740s", "Sim GPS", "Address 35" */
    char role[28];    /* "GNSS position", "Display", ... */
    char install[41]; /* installation description 1, may be empty */
};
size_t n2k_net_view(N2kNetView *out, size_t max, uint32_t now_ms);

/* Sum of every device's declared load equivalency (x 50 mA). */
uint32_t n2k_net_len_total(void);
size_t n2k_net_device_count(void);

/* "Display", "GNSS position", ... for a class and function, or "". */
const char *n2k_net_role(uint8_t device_class, uint8_t function);

/* "no_name", "heartbeat_lost", ... and a sentence for each. */
const char *n2k_net_issue_code(N2kNetIssue issue);
const char *n2k_net_issue_text(N2kNetIssue issue);

/* GET /n2k/network.json. 0 if it did not fit. */
size_t n2k_net_json(char *out, size_t cap, uint32_t now_ms);

/* One device's identity and health as a JSON object (no brackets around
 * it), for /n2k/device.json. 0 if the address is unknown. */
size_t n2k_net_device_json(char *out, size_t cap, uint8_t address, uint32_t now_ms);

/* One device's identity and health, for the terminal's node detail page. */
struct N2kNetInfo {
    N2kNetView view;
    bool named;
    bool have_product;
    bool have_config;
    char maker[24];
    char model[33];
    char software[33];
    char model_version[33];
    char serial[33];
    char install2[71];
    uint32_t unique;
    uint16_t manufacturer;
    uint8_t instance;
    uint8_t system_instance;
    uint8_t certification;
    uint16_t n2k_version;
    uint32_t heartbeat_ms;     /* 0 none seen */
    uint32_t heartbeat_age_ms;
    uint16_t address_changes;
    uint8_t previous_address;
    uint8_t transmits;         /* PGNs in its transmit list, 0xff unknown */
};
bool n2k_net_info(uint8_t address, N2kNetInfo *out, uint32_t now_ms);
