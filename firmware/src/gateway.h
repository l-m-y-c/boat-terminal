#pragma once

#include <cstddef>
#include <cstdint>

static constexpr uint16_t kGatewayTcpPort = 10110;
static constexpr uint16_t kGatewayUdpPort = 10110;
static constexpr uint16_t kGatewayUdpAltPort = 2000;
static constexpr size_t kGatewayMaxClients = 4;

void gateway_init(void);
void gateway_begin(void);
void gateway_poll(void);
void gateway_send_sentence(const char *sentence);
uint32_t gateway_client_count(void);
