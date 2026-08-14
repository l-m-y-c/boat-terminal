#pragma once

#include <cstddef>
#include <cstdint>

/* Bench identity. A production unit gets a provisioned boat id + device key. */
#ifndef LMYC_BOAT_ID
#define LMYC_BOAT_ID "BENCH-01"
#endif

#ifndef LMYC_TERMINAL_ID
#define LMYC_TERMINAL_ID "WS7-001"
#endif

/* 16-byte OOB secret, hex-encoded in the QR. Regenerated on each boot. */
static constexpr size_t kPairingOobBytes = 16;
static constexpr size_t kPairingPayloadMax = 192;
static constexpr uint16_t kPairingBleAppearance = 0x0341; /* generic outdoor sports activity */

void pairing_init(void);
const char *pairing_payload(void);
const char *pairing_oob_hex(void);
const char *pairing_ble_name(void);
const char *pairing_status(void);
bool pairing_ble_ok(void);
bool pairing_is_confirmed(void);
void pairing_set_status(const char *status);
