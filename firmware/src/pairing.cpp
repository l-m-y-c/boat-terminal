/**
 * QR + BLE pairing bootstrap with OOB confirmation.
 *
 * Flow:
 *  1. Terminal advertises as LMYC-XXYY and shows QR with oob=...
 *  2. Phone scans QR, connects over BLE, reads payload characteristic
 *  3. Phone writes "PAIR <oob_hex>" to the session characteristic
 *  4. Terminal verifies OOB matches the one minted for this boot → "Paired"
 *
 * Bench security: open GATT link (no forced bond). Application-level OOB
 * is the proof the phone scanned this terminal's QR. Full LE Secure
 * Connections can replace this later.
 */

#include "pairing.h"

#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <BLEServer.h>
#include <BLEUtils.h>
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
static const char *g_status = "Idle";
static bool g_ble_ok = false;
static bool g_confirmed = false;

static BLEServer *g_server = nullptr;
static BLECharacteristic *g_payload_chr = nullptr;
static BLECharacteristic *g_session_chr = nullptr;

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

class PairingServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer * /*server*/) override
    {
        g_status = "Connected";
        g_confirmed = false;
        Serial.println("BLE: phone connected");
    }

    void onDisconnect(BLEServer * /*server*/) override
    {
        g_status = "Advertising";
        g_confirmed = false;
        if (g_session_chr != nullptr) {
            g_session_chr->setValue("WAIT");
        }
        Serial.println("BLE: disconnected, advertising again");
        delay(80);
        BLEDevice::startAdvertising();
    }
};

class SessionCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *chr) override
    {
        String value = chr->getValue();
        Serial.printf("BLE session write (%u bytes): %s\n",
                      static_cast<unsigned>(value.length()), value.c_str());

        if (value.startsWith("PAIR ")) {
            String offered = value.substring(5);
            offered.trim();
            if (oob_matches(offered.c_str())) {
                g_confirmed = true;
                g_status = "Paired";
                chr->setValue("OK");
                Serial.println("BLE: OOB confirmed — Paired");
            } else {
                g_confirmed = false;
                g_status = "OOB mismatch";
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

    void onRead(BLECharacteristic *chr) override
    {
        if (g_confirmed) {
            chr->setValue("OK");
        } else {
            chr->setValue("WAIT");
        }
    }
};

static PairingServerCallbacks g_server_cb;
static SessionCallbacks g_session_cb;

static bool start_ble_peripheral(void)
{
    BLEDevice::init(g_ble_name);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);

    /* Bench: do not force bonding/encryption. Android often drops the link
     * during Just-Works negotiation before discoverServices completes.
     * Application OOB remains the real pairing proof. */
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_NONE);

    g_server = BLEDevice::createServer();
    g_server->setCallbacks(&g_server_cb);

    BLEService *dis = g_server->createService(BLEUUID((uint16_t)0x180A));
    BLECharacteristic *mfg = dis->createCharacteristic(
        BLEUUID((uint16_t)0x2A29), BLECharacteristic::PROPERTY_READ);
    mfg->setValue("LMYC");
    BLECharacteristic *model = dis->createCharacteristic(
        BLEUUID((uint16_t)0x2A24), BLECharacteristic::PROPERTY_READ);
    model->setValue("Boat Terminal");
    BLECharacteristic *serial = dis->createCharacteristic(
        BLEUUID((uint16_t)0x2A25), BLECharacteristic::PROPERTY_READ);
    serial->setValue(LMYC_TERMINAL_ID);
    BLECharacteristic *fw = dis->createCharacteristic(
        BLEUUID((uint16_t)0x2A26), BLECharacteristic::PROPERTY_READ);
    fw->setValue("bench-oob-v2");
    dis->start();

    BLEService *svc = g_server->createService(kLmycServiceUuid);

    g_payload_chr = svc->createCharacteristic(
        kLmycPayloadUuid,
        BLECharacteristic::PROPERTY_READ);
    g_payload_chr->setValue(g_payload);

    g_session_chr = svc->createCharacteristic(
        kLmycSessionUuid,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE |
            BLECharacteristic::PROPERTY_WRITE_NR);
    g_session_chr->setCallbacks(&g_session_cb);
    g_session_chr->setValue("WAIT");

    svc->start();

    BLEAdvertisementData adv;
    adv.setFlags(ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT);
    adv.setName(g_ble_name);
    adv.setCompleteServices(BLEUUID((uint16_t)0x180A));

    BLEAdvertisementData scan;
    scan.setCompleteServices(BLEUUID(kLmycServiceUuid));

    BLEAdvertising *advertising = BLEDevice::getAdvertising();
    advertising->setAdvertisementData(adv);
    advertising->setScanResponseData(scan);
    advertising->setScanResponse(true);
    advertising->setMinInterval(0x20);
    advertising->setMaxInterval(0x40);
    advertising->setAppearance(kPairingBleAppearance);
    advertising->start();

    g_status = "Advertising";
    Serial.printf("BLE: advertising as %s (open GATT + OOB confirm)\n", g_ble_name);
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
    Serial.printf("BLE name: %s\n", g_ble_name);
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
        g_status = status;
    }
}
