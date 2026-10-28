#include "gateway.h"
#include "log_tee.h"

#include "nmea.h"
#include "pairing.h"

#include <Arduino.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>

#include <esp_heap_caps.h>

#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

/* Chart apps on the hotspot listen for UDP. In AP+STA mode a socket bound
 * to 0.0.0.0 sends broadcasts out the upstream interface, not to phones. */
static constexpr const char *kHotspotIp = "192.168.4.1";
static constexpr const char *kHotspotBroadcast = "192.168.4.255";

/* A non-blocking send() can take part of a line. Without a buffer the rest
 * is lost and the next sentence lands in the middle of it, so each chart
 * app gets its own queue and only whole lines go in. 8 KB is several
 * seconds of output, enough for a phone that pauses its Wi-Fi briefly. */
static constexpr size_t kClientBuffer = 8192;
/* A client whose queue stays full this long has stopped reading. */
static constexpr uint32_t kStalledMs = 30000;

struct ChartApp {
    int fd;
    uint8_t *buf;
    size_t len;
    uint32_t progress_ms;
};

static int g_listen_fd = -1;
static int g_udp_fd = -1;
static ChartApp g_clients[kGatewayMaxClients];
static bool g_started = false;
static uint32_t g_sentences = 0;
static uint32_t g_lost_tcp = 0;
static uint32_t g_lost_udp = 0;
static uint32_t g_queued_peak = 0;

void gateway_init(void)
{
    g_listen_fd = -1;
    g_udp_fd = -1;
    g_started = false;
    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        ChartApp &c = g_clients[i];
        c.fd = -1;
        c.len = 0;
        c.progress_ms = 0;
        if (c.buf == nullptr) {
            c.buf = static_cast<uint8_t *>(
                heap_caps_malloc(kClientBuffer, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        }
        if (c.buf == nullptr) {
            c.buf = static_cast<uint8_t *>(heap_caps_malloc(kClientBuffer, MALLOC_CAP_8BIT));
        }
    }
}

static void close_fd(int *fd)
{
    if (fd != nullptr && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

void gateway_begin(void)
{
    if (g_started) {
        return;
    }

    g_listen_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (g_listen_fd < 0) {
        g_log.printf("NMEA gateway: TCP socket failed errno=%d\n", errno);
        return;
    }
    int yes = 1;
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(kGatewayTcpPort);
    if (bind(g_listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0 ||
        listen(g_listen_fd, kGatewayMaxClients) != 0) {
        g_log.printf("NMEA gateway: TCP bind/listen :%u failed errno=%d\n",
                      static_cast<unsigned>(kGatewayTcpPort), errno);
        close_fd(&g_listen_fd);
        return;
    }
    fcntl(g_listen_fd, F_SETFL, O_NONBLOCK);

    g_udp_fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (g_udp_fd >= 0) {
        setsockopt(g_udp_fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
        setsockopt(g_udp_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in uaddr = {};
        uaddr.sin_family = AF_INET;
        uaddr.sin_addr.s_addr = ipaddr_addr(kHotspotIp);
        uaddr.sin_port = htons(0);
        if (bind(g_udp_fd, reinterpret_cast<sockaddr *>(&uaddr), sizeof(uaddr)) != 0) {
            g_log.printf("NMEA gateway: UDP bind %s failed errno=%d\n", kHotspotIp, errno);
            close_fd(&g_udp_fd);
        }
    }

    g_started = true;
    g_log.printf("NMEA gateway: TCP %u (%u clients) + UDP %u/%u\n",
                  static_cast<unsigned>(kGatewayTcpPort),
                  static_cast<unsigned>(kGatewayMaxClients),
                  static_cast<unsigned>(kGatewayUdpPort),
                  static_cast<unsigned>(kGatewayUdpAltPort));
}

static void drop_client(size_t i)
{
    ChartApp &c = g_clients[i];
    if (c.fd >= 0) {
        close(c.fd);
        c.fd = -1;
        c.len = 0;
        g_log.println("NMEA gateway: client disconnected");
    }
}

/* Sends what the socket will take. Returns false if the client went away. */
static bool flush_client(size_t i)
{
    ChartApp &c = g_clients[i];
    while (c.len > 0) {
        const int n = send(c.fd, c.buf, c.len, 0);
        if (n > 0) {
            const size_t sent = static_cast<size_t>(n);
            memmove(c.buf, c.buf + sent, c.len - sent);
            c.len -= sent;
            c.progress_ms = millis();
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        drop_client(i);
        return false;
    }
    c.progress_ms = millis();
    return true;
}

/* A quiet bus sends no sentences, and a phone cannot tell that from a
 * terminal that went away. The standard heartbeat proves the link: to each
 * new TCP client at once, and every 15 s on TCP, UDP and BLE notify. */
static constexpr uint32_t kHeartbeatMs = 15000;
static uint32_t g_heartbeat_ms = 0;
static uint8_t g_heartbeat_seq = 0;

static size_t heartbeat_line(char *out, size_t cap)
{
    snprintf(out, cap, "$IIHBT,%lu.0,A,%u", static_cast<unsigned long>(kHeartbeatMs / 1000),
             static_cast<unsigned>(g_heartbeat_seq));
    nmea_append_checksum(out, cap);
    return strlen(out);
}

void gateway_poll(void)
{
    if (!g_started || g_listen_fd < 0) {
        return;
    }

    sockaddr_in peer = {};
    socklen_t peer_len = sizeof(peer);
    const int fd = accept(g_listen_fd, reinterpret_cast<sockaddr *>(&peer), &peer_len);
    if (fd >= 0) {
        int slot = -1;
        for (size_t i = 0; i < kGatewayMaxClients; ++i) {
            if (g_clients[i].fd < 0 && g_clients[i].buf != nullptr) {
                slot = static_cast<int>(i);
                break;
            }
        }
        if (slot < 0) {
            close(fd);
        } else {
            fcntl(fd, F_SETFL, O_NONBLOCK);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            ChartApp &c = g_clients[slot];
            c.fd = fd;
            c.len = 0;
            c.progress_ms = millis();
            g_log.printf("NMEA gateway: TCP client #%d\n", slot);
            char hb[40];
            const size_t n = heartbeat_line(hb, sizeof(hb) - 2);
            memcpy(c.buf, hb, n);
            memcpy(c.buf + n, "\r\n", 2);
            c.len = n + 2;
            flush_client(static_cast<size_t>(slot));
        }
    }

    const uint32_t now = millis();
    if (now - g_heartbeat_ms >= kHeartbeatMs) {
        g_heartbeat_ms = now;
        g_heartbeat_seq = static_cast<uint8_t>((g_heartbeat_seq + 1) % 10);
        char hb[40];
        heartbeat_line(hb, sizeof(hb));
        gateway_send_sentence(hb);
        pairing_nmea_notify(hb);
    }

    char sink[64];
    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        if (g_clients[i].fd < 0) {
            continue;
        }
        const int n = recv(g_clients[i].fd, sink, sizeof(sink), 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            drop_client(i);
            continue;
        }
        flush_client(i);
    }
}

static void udp_send(const char *line, size_t len)
{
    if (g_udp_fd < 0) {
        return;
    }
    sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = ipaddr_addr(kHotspotBroadcast);
    /* UDP has no retry. A failed send here is lwIP out of buffers. */
    dest.sin_port = htons(kGatewayUdpPort);
    if (sendto(g_udp_fd, line, len, 0, reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) < 0) {
        ++g_lost_udp;
    }
    dest.sin_port = htons(kGatewayUdpAltPort);
    if (sendto(g_udp_fd, line, len, 0, reinterpret_cast<sockaddr *>(&dest), sizeof(dest)) < 0) {
        ++g_lost_udp;
    }
}

void gateway_send_sentence(const char *sentence)
{
    if (sentence == nullptr || sentence[0] == '\0') {
        return;
    }
    char line[96];
    const int n = snprintf(line, sizeof(line), "%s\r\n", sentence);
    if (n <= 0) {
        return;
    }
    const size_t len = static_cast<size_t>(n);
    ++g_sentences;

    const uint32_t now = millis();
    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        ChartApp &c = g_clients[i];
        if (c.fd < 0 || !flush_client(i)) {
            continue;
        }
        if (c.len + len > kClientBuffer) {
            ++g_lost_tcp;
            if (now - c.progress_ms > kStalledMs) {
                g_log.println("NMEA gateway: client stopped reading");
                drop_client(i);
            }
            continue;
        }
        memcpy(c.buf + c.len, line, len);
        c.len += len;
        if (c.len > g_queued_peak) {
            g_queued_peak = static_cast<uint32_t>(c.len);
        }
        flush_client(i);
    }
    udp_send(line, len);
}

void gateway_stats(GatewayStats *out)
{
    if (out == nullptr) {
        return;
    }
    out->clients = gateway_client_count();
    out->sentences = g_sentences;
    out->lost_tcp = g_lost_tcp;
    out->lost_udp = g_lost_udp;
    out->queued_peak = g_queued_peak;
    out->queue_capacity = static_cast<uint32_t>(kClientBuffer);
}

uint32_t gateway_client_count(void)
{
    uint32_t n = 0;
    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        if (g_clients[i].fd >= 0) {
            ++n;
        }
    }
    return n;
}
