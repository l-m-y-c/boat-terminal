#include "gateway.h"

#include <Arduino.h>

#include <cerrno>
#include <cstring>
#include <fcntl.h>

#include "lwip/sockets.h"

static int g_listen_fd = -1;
static int g_udp_fd = -1;
static int g_clients[kGatewayMaxClients];
static bool g_started = false;

void gateway_init(void)
{
    g_listen_fd = -1;
    g_udp_fd = -1;
    g_started = false;
    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        g_clients[i] = -1;
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
        Serial.printf("NMEA gateway: TCP socket failed errno=%d\n", errno);
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
        Serial.printf("NMEA gateway: TCP bind/listen :%u failed errno=%d\n",
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
        uaddr.sin_addr.s_addr = htonl(INADDR_ANY);
        uaddr.sin_port = htons(0);
        bind(g_udp_fd, reinterpret_cast<sockaddr *>(&uaddr), sizeof(uaddr));
    }

    g_started = true;
    Serial.printf("NMEA gateway: TCP %u (%u clients) + UDP %u/%u\n",
                  static_cast<unsigned>(kGatewayTcpPort),
                  static_cast<unsigned>(kGatewayMaxClients),
                  static_cast<unsigned>(kGatewayUdpPort),
                  static_cast<unsigned>(kGatewayUdpAltPort));
}

static void drop_client(size_t i)
{
    if (g_clients[i] >= 0) {
        close(g_clients[i]);
        g_clients[i] = -1;
        Serial.println("NMEA gateway: client disconnected");
    }
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
            if (g_clients[i] < 0) {
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
            g_clients[slot] = fd;
            Serial.printf("NMEA gateway: TCP client #%d\n", slot);
        }
    }

    char sink[64];
    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        if (g_clients[i] < 0) {
            continue;
        }
        const int n = recv(g_clients[i], sink, sizeof(sink), 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            drop_client(i);
        }
    }
}

static void udp_send(const char *line, size_t len)
{
    if (g_udp_fd < 0) {
        return;
    }
    sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    dest.sin_port = htons(kGatewayUdpPort);
    sendto(g_udp_fd, line, len, 0, reinterpret_cast<sockaddr *>(&dest), sizeof(dest));
    dest.sin_port = htons(kGatewayUdpAltPort);
    sendto(g_udp_fd, line, len, 0, reinterpret_cast<sockaddr *>(&dest), sizeof(dest));
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

    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        if (g_clients[i] < 0) {
            continue;
        }
        if (send(g_clients[i], line, len, 0) < 0 &&
            errno != EAGAIN && errno != EWOULDBLOCK) {
            drop_client(i);
        }
    }
    udp_send(line, len);
}

uint32_t gateway_client_count(void)
{
    uint32_t n = 0;
    for (size_t i = 0; i < kGatewayMaxClients; ++i) {
        if (g_clients[i] >= 0) {
            ++n;
        }
    }
    return n;
}
