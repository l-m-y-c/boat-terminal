#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol.h" /* kGatewayTcpPort, kGatewayUdpPort, kGatewayUdpAltPort */

static constexpr size_t kGatewayMaxClients = 4;

void gateway_init(void);
void gateway_begin(void);
void gateway_poll(void);
void gateway_send_sentence(const char *sentence);
uint32_t gateway_client_count(void);

/* Delivery to chart apps. lost_tcp counts sentences a connected app did not
 * get because its queue was full; lost_udp counts failed broadcasts. */
struct GatewayStats {
    uint32_t clients;
    uint32_t sentences;
    uint32_t lost_tcp;
    uint32_t lost_udp;
    uint32_t queued_peak;    /* bytes, the fullest any app's queue has been */
    uint32_t queue_capacity; /* bytes per app */
};
void gateway_stats(GatewayStats *out);
