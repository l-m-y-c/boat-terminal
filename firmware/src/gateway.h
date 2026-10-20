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
