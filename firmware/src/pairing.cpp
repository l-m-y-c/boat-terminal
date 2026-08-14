/**
 * QR + BLE pairing bootstrap with OOB confirmation (NimBLE 1.4 API).
 *
 * Bluedroid timed out Android GATT discovery under RGB+LVGL load.
 * NimBLE is lighter and answers ATT requests more reliably on ESP32-S3.
 */

#include "pairing.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstdio>
#include <cstring>
#include <esp_mac.h>
#include <esp_random.h>

static constexpr const char *kLmycServiceUuid = "6c6d7963-0001-4000-8000-000000000001";
static constexpr const char *kLmycPayloadUuid = "6c6d7963-0001-4000-8000-000000000010";
static constexpr const char *kLmycSessionUuid = "6c6d7963-0001-4000-8000-000000000020";

static char g_oob_hex[(kPairingOobBytes * 2) + 1];
static char g_payload[kPairingPayloadMax];
static char g_ble_name[24];
static char g_status_buf[48] = "Idle";
static const char *g_status = g_status_buf;
static bool g_ble_ok = false;
static bool g_confirmed = false;
static uint32_t g_connect_count = 0;

static NimBLEServer *g_server = nullptr;
static NimBLECharacteristic *g_payload_chr = nullptr;
static NimBLECharacteristic *g_session_chr = nullptr;

static void set_status(const char *s)
{
    snprintf(g_status_buf, sizeof(g_status_buf), "%s", s);
    g_status = g_status_buf;
    Serial.printf("BLE status: %s\n", g_status_buf);
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

static void mint_oob(void)
{
    uint8_t raw[kPairingOobBytes];
    esp_fill_random(raw, sizeof(raw));
    bytes_to_hex(raw, sizeof(raw), g_oob_hex);
}

static void build_ble_name(void)
{
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(g_ble_name, sizeof(g_ble_name), "LMYC-%02X%02X", mac[4], mac[5]);
}

static void build_payload(void)
{
    snprintf(g_payload, sizeof(g_payload),
             "lmyc://pair?v=1&boat=%s&tid=%s&ble=%s&oob=%s",
             LMYC_BOAT_ID, LMYC_TERMINAL_ID, g_ble_name, g_oob_hex);
}

static bool oob_matches(const char *candidate)
{
    if (candidate == nullptr) {
        return false;
    }
    return strcasecmp(candidate, g_oob_hex) == 0;
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer * /*pServer*/) override
    {
        g_connect_count++;
        g_confirmed = false;
        NimBLEDevice::stopAdvertising();
        char buf[48];
        snprintf(buf, sizeof(buf), "Connected #%lu",
                 static_cast<unsigned long>(g_connect_count));
        set_status(buf);
    }

    void onDisconnect(NimBLEServer * /*pServer*/) override
    {
        g_confirmed = false;
        if (g_session_chr != nullptr) {
            g_session_chr->setValue("WAIT");
        }
        set_status("Advertising");
        Serial.println("BLE: disconnected — advertising again");
        NimBLEDevice::startAdvertising();
    }
};

class SessionCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *chr) override
    {
        std::string value = chr->getValue();
        Serial.printf("BLE session write (%u bytes): %s\n",
                      static_cast<unsigned>(value.size()), value.c_str());

        if (value.rfind("PAIR ", 0) == 0) {
            const char *offered = value.c_str() + 5;
            while (*offered == ' ') {
                ++offered;
            }
            if (oob_matches(offered)) {
                g_confirmed = true;
                set_status("Paired");
                chr->setValue("OK");
                Serial.println("BLE: OOB confirmed — Paired");
            } else {
                g_confirmed = false;
                set_status("OOB mismatch");
                chr->setValue("FAIL");
                Serial.println("BLE: OOB mismatch");
            }
            return;
        }

        if (value == "PING") {
            chr->setValue("PONG");
            return;
        }

        chr->setValue("ERR");
    }

    void onRead(NimBLECharacteristic *chr) override
    {
        Serial.println("BLE: session read");
        chr->setValue(g_confirmed ? "OK" : "WAIT");
    }
};

class PayloadCallbacks : public NimBLECharacteristicCallbacks {
    void onRead(NimBLECharacteristic * /*chr*/) override
    {
        Serial.println("BLE: payload read");
    }
};

static ServerCallbacks g_server_cb;
static SessionCallbacks g_session_cb;
static PayloadCallbacks g_payload_cb;

static bool start_ble_peripheral(void)
{
    NimBLEDevice::init(g_ble_name);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    g_server = NimBLEDevice::createServer();
    g_server->setCallbacks(&g_server_cb);

    NimBLEService *dis = g_server->createService("180A");
    dis->createCharacteristic("2A29", NIMBLE_PROPERTY::READ)->setValue("LMYC");
    dis->createCharacteristic("2A24", NIMBLE_PROPERTY::READ)->setValue("Boat Terminal");
    dis->createCharacteristic("2A25", NIMBLE_PROPERTY::READ)->setValue(LMYC_TERMINAL_ID);
    dis->createCharacteristic("2A26", NIMBLE_PROPERTY::READ)->setValue("nimble-oob-v1");
    dis->start();

    NimBLEService *svc = g_server->createService(kLmycServiceUuid);

    g_payload_chr = svc->createCharacteristic(
        kLmycPayloadUuid, NIMBLE_PROPERTY::READ);
    g_payload_chr->setCallbacks(&g_payload_cb);
    g_payload_chr->setValue(g_payload);

    g_session_chr = svc->createCharacteristic(
        kLmycSessionUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    g_session_chr->setCallbacks(&g_session_cb);
    g_session_chr->setValue("WAIT");

    svc->start();

    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    adv->setName(g_ble_name);
    adv->addServiceUUID("180A");
    adv->addServiceUUID(kLmycServiceUuid);
    adv->setScanResponse(true);
    adv->setMinInterval(0x40);
    adv->setMaxInterval(0x80);
    adv->start();

    set_status("Advertising");
    Serial.printf("BLE(NimBLE): advertising as %s\n", g_ble_name);
    Serial.printf("BLE: service %s\n", kLmycServiceUuid);
    return true;
}

void pairing_init(void)
{
    mint_oob();
    build_ble_name();
    build_payload();
    g_confirmed = false;
    g_ble_ok = start_ble_peripheral();
    Serial.printf("Pairing payload: %s\n", g_payload);
}

const char *pairing_payload(void) { return g_payload; }
const char *pairing_oob_hex(void) { return g_oob_hex; }
const char *pairing_ble_name(void) { return g_ble_name; }
const char *pairing_status(void) { return g_status; }
bool pairing_ble_ok(void) { return g_ble_ok; }
bool pairing_is_confirmed(void) { return g_confirmed; }

void pairing_set_status(const char *status)
{
    if (status != nullptr) {
        set_status(status);
    }
}
