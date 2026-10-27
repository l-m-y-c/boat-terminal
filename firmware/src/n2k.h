#pragma once

#include <cstddef>
#include <cstdint>

/* Where instrument data comes from. The choice is saved on the terminal and
 * survives power-off and firmware updates. Changing it restarts the
 * terminal, so every boot runs exactly one source from a clean state.
 *
 * NMEA_SOURCE in secrets.h only sets the first-boot default:
 * 0 is the simulator, 1 is NMEA 2000. The old 2 (AUTO) now means 1. */
#define NMEA_SOURCE_SIM 0
#define NMEA_SOURCE_N2K 1
#define NMEA_SOURCE_AUTO 2

enum class DataSource : uint8_t {
    /* Bench data on a virtual bus. CAN stays closed and USB serial stays up. */
    Simulator = 0,
    /* A standard NMEA 2000 node on the CAN header (n2k_node.h). GPIO19/20
     * leave USB for the transceiver, so the serial log moves to the UART
     * Type-C socket. See docs/05. */
    Nmea2000 = 1,
};

/* The terminal as a node on the backbone. */
enum class NodeState : uint8_t {
    Off,       /* simulator: no bus, nothing sent */
    Claiming,  /* address claim sent, 250 ms not yet passed */
    Claimed,   /* holds a source address */
    NoAddress, /* every address is held by a device with a higher-priority NAME */
};

enum class BusState : uint8_t {
    Simulated, /* simulator running */
    Paused,    /* simulator paused by NMEA DEMO / NMEA PGN */
    Opening,   /* NMEA 2000 chosen, controller not up yet */
    Waiting,   /* listening, no frame since boot */
    Live,      /* frames in the last 3 s */
    Quiet,     /* listening, nothing for 3 s */
    BusOff,    /* bus-off after transmit errors, standard recovery running */
    Failed,    /* the controller would not start */
};

/* What to tell the person at the boat, most urgent first. Chosen once a
 * second by n2k_hint_pick() (n2k_diag.h) and held for 10 s. */
enum class N2kHint : uint8_t {
    None,
    Repeats,        /* the same frame back to back: some device gets no ACK */
    ErrorsNoFrames, /* error frames but nothing received */
    BusOff,         /* the terminal went bus-off */
    NoAddress,      /* the terminal could not claim an address */
    NoFrames,       /* NMEA 2000 chosen, silence */
    Losing,         /* frames lost before decoding */
    FastIncomplete, /* fast packets not coming out whole */
    BadValues,      /* error codes or impossible values rejected */
    TwoSources,     /* two devices send the same value; one is used */
};

/* Everything the screen, the phone and /status.json report. Counters run
 * from boot. "Lost" counters are frames that never reached the decoder. */
struct N2kStats {
    DataSource source;
    BusState state;
    uint32_t frames;            /* CAN frames received (or simulated) */
    uint32_t frames_per_s;      /* last second */
    uint32_t frames_per_s_peak;
    uint32_t messages;          /* whole PGNs after fast-packet reassembly */
    uint32_t messages_per_s;
    uint32_t queue_capacity;    /* frames the receive queue holds */
    uint32_t queue_depth;       /* waiting now */
    uint32_t queue_peak;        /* high-water mark since boot */
    uint32_t lost_queue;        /* receive queue was full */
    uint32_t lost_driver;       /* TWAI driver queue was full */
    uint32_t lost_controller;   /* controller FIFO overran (a lower bound: IDF counts
                                 * the frames it clears, not ones that never fit) */
    uint32_t ignored;           /* not NMEA 2000 (11-bit, remote), or flushed for DEMO */
    uint32_t bus_errors;        /* CAN error frames seen on the wire */
    uint32_t rx_error_counter;  /* controller REC right now */
    uint32_t pgn_types;         /* distinct PGNs heard */
    uint32_t devices;           /* distinct source addresses heard */
    uint32_t last_frame_ms;     /* age of the newest frame, UINT32_MAX for never */
    uint32_t parse_peak_us;     /* longest single drain of the queue */
    NodeState node;
    uint8_t address;            /* claimed source address, 254 for none */
    uint32_t tx_frames;         /* frames this node sent (network management only) */
    uint32_t tx_failed;         /* frames the driver would not take; the library resends */
    uint32_t bus_off_count;
    uint32_t lost_messages;     /* reassembled messages the converter queue had no room for */
    uint32_t unique_number;     /* this node's NAME */
    uint16_t manufacturer_code;
    uint32_t repeats;           /* a frame identical to the one before, within 2 ms */
    uint32_t repeats_per_s;
    uint32_t bus_errors_per_s;
    uint32_t fast_incomplete;   /* fast-packet messages started, never completed */
    uint32_t conv_bad;          /* values the converter rejected as impossible */
    N2kHint hint;
};

/* can_mode true connects the TJA1051. false connects the USB socket. */
using N2kCanSelectFn = void (*)(bool can_mode);

void n2k_init(N2kCanSelectFn select_can);
/* loop(): decode every queued frame, then the 1 Hz converter work. */
void n2k_poll(void);

DataSource n2k_source(void);
const char *n2k_source_name(DataSource source);
const char *n2k_state_name(BusState state);

/* Saves next and restarts the terminal a moment later, so the screen can
 * show that it is restarting. Returns false if the choice was not saved.
 * Choosing the source that is already running does nothing. */
bool n2k_switch_source(DataSource next);
bool n2k_restart_pending(void);

void n2k_stats(N2kStats *out);

/* Phone GNSS (LOCAL on the 7″) owns position: the simulator stops sending
 * GPS PGNs. Has no effect on a live bus. */
void n2k_set_sim_gnss(bool on);

/* "NMEA SIM|N2K|STATS|DEMO" or "NMEA PGN <number> [src=N] <hex>".
 * DEMO and PGN run only on the simulator. Returns true if it was a command. */
bool n2k_handle_command(const char *line);

/* True once the USB/CAN mux is on the transceiver and logs go to UART0. */
bool n2k_using_uart(void);

/* True when no frame has arrived for 3 s. */
bool n2k_quiet(void);

uint32_t n2k_frame_count(void);

/* "SIM", "PLAY" (simulator paused for canned PGNs), "N2K" (bus chosen,
 * nothing arriving) or "BOAT" (frames flowing). */
const char *n2k_fix_tag(void);

/* "Simulator - 412 frames/s" or "NMEA 2000 - live - 1482 frames". */
void n2k_status_line(char *out, size_t cap);

/* "NMEA 2000 node, address 23", "Claiming an address", ... for the screen. */
const char *n2k_node_state_name(NodeState state);

/* Raw frames for capture, as candump log lines
 * "(seconds.micros) can0 1DEFFF23#0102030405060708\n", seconds from boot.
 * after: the last sequence number already read, 0 to start from now.
 * *next: pass it back as after. *lost: frames overwritten before they
 * were read. Safe from any task. */
size_t n2k_capture_read(uint32_t after, char *out, size_t cap, uint32_t *next, uint32_t *lost);

/* GET /n2k.json: devices, PGNs, converter counters and owners, rebuilt
 * once a second by loop(). Safe from any task. Returns the length. */
size_t n2k_diag_copy(char *out, size_t cap);
