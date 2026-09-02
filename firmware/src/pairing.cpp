/**
 * QR + BLE pairing bootstrap with OOB confirmation (NimBLE 1.4 API).
 *
 * Advertising budget is 31 bytes. Name stays in the primary ADV packet;
 * the LMYC 128-bit UUID is only in the scan response.
 *
 * Default ATT MTU is 23 (20-byte writes). "PAIR " + 32 hex chars is 37 bytes
 * and dies on the phone before the radio (dataLen 37 > max 20). Accept a raw
 * 16-byte OOB write, 32-char hex, or chunked PAIR <hex>. Preferred MTU is
 * advertised; we do not require the phone to request it.
 */

#include "pairing.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <esp_mac.h>
#include <esp_random.h>

static constexpr const char *kLmycServiceUuid = "6c6d7963-0001-4000-8000-000000000001";
static constexpr const char *kLmycPayloadUuid = "6c6d7963-0001-4000-8000-000000000010";
static constexpr const char *kLmycSessionUuid = "6c6d7963-0001-4000-8000-000000000020";
/* Read/notify only — never written by the phone (avoids Android write-cache garbage). */
static constexpr const char *kLmycStatusUuid = "6c6d7963-0001-4000-8000-000000000021";
static constexpr size_t kEventLogLines = 5;
static constexpr size_t kEventLineLen = 40;
static constexpr size_t kRxAccMax = 80;

static char g_oob_hex[(kPairingOobBytes * 2) + 1];
static uint8_t g_oob_raw[kPairingOobBytes];
static char g_payload[kPairingPayloadMax];
static char g_ble_name[24];
static char g_mac_str[18] = "--:--:--:--:--:--";
static char g_status_buf[48] = "Idle";
static char g_last_event[48] = "boot";
static char g_adv_layout[40] = "ADV:name  SR:uuid";
static char g_peer[22] = "-";
static char g_last_rx[48] = "none";
static char g_hint[96] = "Waiting for LMYC app (not Settings)";
static char g_event_lines[kEventLogLines][kEventLineLen];
static char g_event_log[kEventLogLines * kEventLineLen];
static char g_rx_acc[kRxAccMax];
static size_t g_rx_acc_len = 0;
static char g_session_status[8] = "WAIT";
static const char *g_status = g_status_buf;
static bool g_ble_ok = false;
static bool g_confirmed = false;
static bool g_connected = false;
static bool g_got_payload_read = false;
static bool g_got_session_write = false;
static uint32_t g_connect_count = 0;
static uint32_t g_write_count = 0;
static uint32_t g_read_count = 0;
static uint16_t g_mtu = 23;
static uint8_t g_event_head = 0;
static uint8_t g_event_count = 0;

static NimBLEServer *g_server = nullptr;
static NimBLECharacteristic *g_payload_chr = nullptr;
static NimBLECharacteristic *g_session_chr = nullptr;
static NimBLECharacteristic *g_status_chr = nullptr;

static void rebuild_event_log(void)
{
    g_event_log[0] = '\0';
    const uint8_t n = g_event_count;
    for (uint8_t i = 0; i < n; ++i) {
        const uint8_t idx = (g_event_head + kEventLogLines - n + i) % kEventLogLines;
        if (i > 0) {
            strncat(g_event_log, "\n", sizeof(g_event_log) - strlen(g_event_log) - 1);
        }
        strncat(g_event_log, g_event_lines[idx], sizeof(g_event_log) - strlen(g_event_log) - 1);
    }
}

static void push_event(const char *s)
{
    const unsigned long sec = millis() / 1000UL;
    snprintf(g_event_lines[g_event_head], kEventLineLen, "t+%lus %s", sec, s);
    g_event_head = (g_event_head + 1) % kEventLogLines;
    if (g_event_count < kEventLogLines) {
        g_event_count++;
    }
    rebuild_event_log();
}

static void refresh_hint(void)
{
    if (g_confirmed) {
        snprintf(g_hint, sizeof(g_hint), "OOB confirmed - paired");
        return;
    }
    if (g_connected) {
        if (g_got_session_write) {
            snprintf(g_hint, sizeof(g_hint), "Got session write - check Last / Rx");
        } else if (g_got_payload_read) {
            snprintf(g_hint, sizeof(g_hint),
                     "Phone read payload, no PAIR yet. 37B write dies at MTU 23.");
        } else {
            snprintf(g_hint, sizeof(g_hint),
                     "Link up MTU %u (20B payload). Waiting for GATT.",
                     static_cast<unsigned>(g_mtu));
        }
        return;
    }
    if (g_connect_count > 0 && g_got_payload_read && !g_got_session_write) {
        snprintf(g_hint, sizeof(g_hint),
                 "Last phone never wrote PAIR. Send 16B OOB, not 37B text.");
        return;
    }
    if (g_connect_count > 0) {
        snprintf(g_hint, sizeof(g_hint), "Advertising again after %lu connect(s)",
                 static_cast<unsigned long>(g_connect_count));
        return;
    }
    snprintf(g_hint, sizeof(g_hint), "Waiting for LMYC app (not Settings)");
}

static void set_status(const char *s)
{
    snprintf(g_status_buf, sizeof(g_status_buf), "%s", s);
    g_status = g_status_buf;
    Serial.printf("BLE status: %s\n", g_status_buf);
}

static void set_event(const char *s)
{
    snprintf(g_last_event, sizeof(g_last_event), "%s", s);
    push_event(s);
    refresh_hint();
    Serial.printf("BLE event: %s\n", g_last_event);
}

static void format_addr(char *out, size_t out_len, const ble_addr_t *addr)
{
    if (addr == nullptr) {
        snprintf(out, out_len, "-");
        return;
    }
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr->val[5], addr->val[4], addr->val[3],
             addr->val[2], addr->val[1], addr->val[0]);
}

static void bytes_to_hex(const uint8_t *in, size_t n, char *out)
{
    static const char kHex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = kHex[(in[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[in[i] & 0x0F];
    }
    out[n * 2] = '\0';
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool is_all_hex(const char *s, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (hex_nibble(s[i]) < 0) {
            return false;
        }
    }
    return n > 0;
}

static void mint_oob(void)
{
    esp_fill_random(g_oob_raw, sizeof(g_oob_raw));
    bytes_to_hex(g_oob_raw, sizeof(g_oob_raw), g_oob_hex);
}

static void build_ble_identity(void)
{
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(g_ble_name, sizeof(g_ble_name), "LMYC-%02X%02X", mac[4], mac[5]);
    snprintf(g_mac_str, sizeof(g_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void build_payload(void)
{
    snprintf(g_payload, sizeof(g_payload),
             "lmyc://pair?v=1&boat=%s&tid=%s&ble=%s&oob=%s",
             LMYC_BOAT_ID, LMYC_TERMINAL_ID, g_ble_name, g_oob_hex);
}

static bool oob_matches_hex(const char *candidate)
{
    if (candidate == nullptr) {
        return false;
    }
    return strcasecmp(candidate, g_oob_hex) == 0;
}

static bool oob_matches_raw(const uint8_t *data, size_t n)
{
    return n == kPairingOobBytes && memcmp(data, g_oob_raw, kPairingOobBytes) == 0;
}

static void describe_rx(const uint8_t *data, size_t n)
{
    char preview[17];
    const size_t shown = n < 8 ? n : 8;
    bytes_to_hex(data, shown, preview);
    if (n > 8) {
        snprintf(g_last_rx, sizeof(g_last_rx), "%uB %s..",
                 static_cast<unsigned>(n), preview);
    } else {
        snprintf(g_last_rx, sizeof(g_last_rx), "%uB %s",
                 static_cast<unsigned>(n), preview);
    }
}

static void set_session_status(const char *status)
{
    /* Clear first — char[8] passed to setValue can copy sizeof(buf), so a
     * leftover 'T' from "WAIT" after writing "OK" became BLE payload "OK\\0T". */
    memset(g_session_status, 0, sizeof(g_session_status));
    snprintf(g_session_status, sizeof(g_session_status), "%s", status);
    if (g_status_chr != nullptr) {
        g_status_chr->setValue(reinterpret_cast<const uint8_t *>(g_session_status),
                               strlen(g_session_status));
        if (g_status_chr->getSubscribedCount() > 0) {
            g_status_chr->notify();
        }
    }
}

static void apply_pair_result(bool ok, const char *how)
{
    g_confirmed = ok;
    if (ok) {
        set_status("Paired");
        char ev[48];
        snprintf(ev, sizeof(ev), "OOB OK (%s)", how);
        set_event(ev);
        set_session_status("OK");
        Serial.printf("BLE: OOB confirmed via %s - Paired\n", how);
    } else {
        set_status("OOB mismatch");
        char ev[48];
        snprintf(ev, sizeof(ev), "OOB FAIL (%s)", how);
        set_event(ev);
        set_session_status("FAIL");
        Serial.printf("BLE: OOB mismatch via %s\n", how);
    }
}

static bool try_handle_session_value(const uint8_t *data, size_t n)
{
    if (n == 0) {
        return false;
    }

    if (n == 4 && memcmp(data, "PING", 4) == 0) {
        set_session_status("PONG");
        set_event("PING");
        return true;
    }

    bool printable = true;
    for (size_t i = 0; i < n; ++i) {
        if (data[i] < 32 || data[i] > 126) {
            printable = false;
            break;
        }
    }

    /* Phone sends raw 16-byte OOB (fits ATT MTU 23). Treat any exact-length
     * 16-byte write as raw OOB — do not require non-printable bytes. A random
     * secret can be all ASCII; the old !printable gate dropped those writes. */
    if (n == kPairingOobBytes) {
        apply_pair_result(oob_matches_raw(data, n), "raw16");
        return true;
    }

    if (printable) {
        char text[kRxAccMax];
        if (n >= sizeof(text)) {
            return false;
        }
        memcpy(text, data, n);
        text[n] = '\0';

        if (strncmp(text, "PAIR ", 5) == 0) {
            if (n < 5 + (kPairingOobBytes * 2)) {
                return false;
            }
            const char *offered = text + 5;
            while (*offered == ' ') {
                ++offered;
            }
            apply_pair_result(oob_matches_hex(offered), "PAIR");
            return true;
        }

        if (n == (kPairingOobBytes * 2) && is_all_hex(text, n)) {
            apply_pair_result(oob_matches_hex(text), "hex32");
            return true;
        }

        if (n == kPairingOobBytes && is_all_hex(text, n)) {
            return false;
        }
    }

    if (n < 20 && n != 4) {
        return false;
    }

    set_session_status("ERR");
    set_event("bad session write");
    return true;
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer * /*pServer*/, ble_gap_conn_desc *desc) override
    {
        g_connect_count++;
        g_confirmed = false;
        g_connected = true;
        g_got_payload_read = false;
        g_got_session_write = false;
        g_rx_acc_len = 0;
        set_session_status("WAIT");
        NimBLEDevice::stopAdvertising();

        /* NimBLE 1.4 ble_gap_conn_desc has no att_mtu. Default ATT MTU
         * is 23 until the client exchanges; onMTUChange updates g_mtu. */
        g_mtu = 23;
        if (desc != nullptr) {
            format_addr(g_peer, sizeof(g_peer), &desc->peer_ota_addr);
            if (g_server != nullptr) {
                const uint16_t peer_mtu = g_server->getPeerMTU(desc->conn_handle);
                if (peer_mtu >= 23) {
                    g_mtu = peer_mtu;
                }
            }
        } else {
            snprintf(g_peer, sizeof(g_peer), "unknown");
        }

        char buf[48];
        snprintf(buf, sizeof(buf), "Connected #%lu MTU%u",
                 static_cast<unsigned long>(g_connect_count),
                 static_cast<unsigned>(g_mtu));
        set_status(buf);
        set_event(buf);
        Serial.printf("BLE: peer %s  MTU %u\n", g_peer, static_cast<unsigned>(g_mtu));
    }

    void handleDisconnect()
    {
        g_confirmed = false;
        g_connected = false;
        g_rx_acc_len = 0;
        set_session_status("WAIT");
        set_status("Advertising");
        if (g_got_payload_read && !g_got_session_write) {
            set_event("disc - no PAIR write");
        } else {
            set_event("disconnect -> adv");
        }
        Serial.println("BLE: disconnected - advertising again");
        NimBLEDevice::startAdvertising();
    }

    void onDisconnect(NimBLEServer * /*pServer*/) override
    {
        handleDisconnect();
    }

    void onDisconnect(NimBLEServer * /*pServer*/, ble_gap_conn_desc *desc) override
    {
        (void)desc;
        handleDisconnect();
    }

    void onMTUChange(uint16_t mtu, ble_gap_conn_desc *desc) override
    {
        g_mtu = mtu;
        if (desc != nullptr) {
            format_addr(g_peer, sizeof(g_peer), &desc->peer_ota_addr);
        }
        char buf[40];
        snprintf(buf, sizeof(buf), "MTU %u", static_cast<unsigned>(mtu));
        set_event(buf);
        refresh_hint();
        Serial.printf("BLE: MTU now %u\n", static_cast<unsigned>(mtu));
    }
};

class SessionCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *chr) override
    {
        /* Use length-aware accessors — OOB is binary and may contain 0x00. */
        const NimBLEAttValue value = chr->getValue();
        const uint8_t *bytes = value.data();
        const size_t n = value.size();

        g_write_count++;
        g_got_session_write = true;
        describe_rx(bytes, n);

        Serial.printf("BLE session write (%u bytes) MTU %u: ",
                      static_cast<unsigned>(n), static_cast<unsigned>(g_mtu));
        for (size_t i = 0; i < n && i < 24; ++i) {
            Serial.printf("%02x", bytes[i]);
        }
        Serial.println();

        if (g_rx_acc_len + n >= kRxAccMax) {
            g_rx_acc_len = 0;
        }
        memcpy(g_rx_acc + g_rx_acc_len, bytes, n);
        g_rx_acc_len += n;

        const bool handled = try_handle_session_value(
            reinterpret_cast<const uint8_t *>(g_rx_acc), g_rx_acc_len);
        if (handled) {
            g_rx_acc_len = 0;
        } else {
            char ev[40];
            snprintf(ev, sizeof(ev), "write +%uB acc %uB",
                     static_cast<unsigned>(n),
                     static_cast<unsigned>(g_rx_acc_len));
            set_event(ev);
        }
    }
};

class StatusCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic *chr) override
    {
        g_read_count++;
        chr->setValue(g_session_status);
        Serial.printf("BLE: status read -> %s\n", g_session_status);
        char ev[40];
        snprintf(ev, sizeof(ev), "status read %s", g_session_status);
        set_event(ev);
    }
};

class PayloadCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic * /*chr*/) override
    {
        g_read_count++;
        g_got_payload_read = true;
        const size_t n = strlen(g_payload);
        snprintf(g_last_rx, sizeof(g_last_rx), "payload %uB", static_cast<unsigned>(n));
        Serial.printf("BLE: payload read (%u bytes)\n", static_cast<unsigned>(n));
        set_event("payload read");
    }
};

static ServerCallbacks g_server_cb;
static SessionCallbacks g_session_cb;
static StatusCallbacks g_status_cb;
static PayloadCallbacks g_payload_cb;

static bool start_advertising(void)
{
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();

    NimBLEAdvertisementData advData;
    advData.setFlags(0x06);
    advData.setName(g_ble_name);

    NimBLEAdvertisementData scanData;
    scanData.setCompleteServices(NimBLEUUID(kLmycServiceUuid));

    adv->setAdvertisementData(advData);
    adv->setScanResponseData(scanData);
    adv->setScanResponse(true);
    adv->setMinInterval(0x20);
    adv->setMaxInterval(0x40);
    if (!adv->start()) {
        set_status("ADV failed");
        set_event("adv start failed");
        Serial.println("BLE: advertising start FAILED");
        return false;
    }

    snprintf(g_adv_layout, sizeof(g_adv_layout), "ADV:%s SR:uuid", g_ble_name);
    set_status("Advertising");
    set_event("advertising");
    Serial.printf("BLE(NimBLE): advertising as %s  mac %s\n", g_ble_name, g_mac_str);
    return true;
}

static bool start_ble_peripheral(void)
{
    NimBLEDevice::init(g_ble_name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setMTU(185);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(&g_server_cb);

    NimBLEService *dis = g_server->createService("180A");
    dis->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue("LMYC");
    dis->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue("Boat Terminal");
    dis->createCharacteristic("2A25", NIMBLE_PROPERTY::READ)->setValue(LMYC_TERMINAL_ID);
    dis->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue("nimble-oob-v3");
    dis->start();

    NimBLEService *svc = g_server->createService(kLmycServiceUuid);

    g_payload_chr = svc->createCharacteristic(
        kLmycPayloadUuid, NIMBLE_PROPERTY::READ);
    g_payload_chr->setCallbacks(&g_payload_cb);
    g_payload_chr->setValue(g_payload);

    /* Phone writes raw 16-byte OOB here. Do not use this char for status —
     * Android read-after-write returns stale/truncated binary (seen as 4B "<"). */
    g_session_chr = svc->createCharacteristic(
        kLmycSessionUuid,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    g_session_chr->setCallbacks(&g_session_cb);

    g_status_chr = svc->createCharacteristic(
        kLmycStatusUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    g_status_chr->setCallbacks(&g_status_cb);
    set_session_status("WAIT");

    svc->start();
    return start_advertising();
}

void pairing_init(void)
{
    mint_oob();
    build_ble_identity();
    build_payload();
    g_confirmed = false;
    g_event_head = 0;
    g_event_count = 0;
    g_event_log[0] = '\0';
    g_ble_ok = start_ble_peripheral();
    Serial.printf("Pairing payload: %s\n", g_payload);
    Serial.println("BLE: session WRITE raw16/hex; status READ/NOTIFY …000021");
}

void pairing_restart_advertising(void)
{
    if (!g_ble_ok) {
        return;
    }
    g_confirmed = false;
    g_connected = false;
    g_rx_acc_len = 0;
    snprintf(g_peer, sizeof(g_peer), "-");
    snprintf(g_last_rx, sizeof(g_last_rx), "none");
    g_mtu = 23;
    set_session_status("WAIT");
    NimBLEDevice::stopAdvertising();
    if (start_advertising()) {
        set_event("adv restarted");
    }
}

const char *pairing_payload(void) { return g_payload; }
const char *pairing_oob_hex(void) { return g_oob_hex; }
const char *pairing_ble_name(void) { return g_ble_name; }
const char *pairing_status(void) { return g_status; }
const char *pairing_mac(void) { return g_mac_str; }
const char *pairing_last_event(void) { return g_last_event; }
const char *pairing_adv_layout(void) { return g_adv_layout; }
const char *pairing_service_uuid(void) { return kLmycServiceUuid; }
const char *pairing_peer(void) { return g_peer; }
const char *pairing_last_rx(void) { return g_last_rx; }
const char *pairing_hint(void)
{
    refresh_hint();
    return g_hint;
}
const char *pairing_event_log(void)
{
    return g_event_log[0] != '\0' ? g_event_log : "t+0s boot";
}
uint16_t pairing_mtu(void) { return g_mtu; }
uint32_t pairing_connect_count(void) { return g_connect_count; }
uint32_t pairing_write_count(void) { return g_write_count; }
uint32_t pairing_read_count(void) { return g_read_count; }
bool pairing_ble_ok(void) { return g_ble_ok; }
bool pairing_is_confirmed(void) { return g_confirmed; }

void pairing_set_status(const char *status)
{
    if (status != nullptr) {
        set_status(status);
    }
}
