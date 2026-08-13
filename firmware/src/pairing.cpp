/**
 * QR + BLE pairing bootstrap.
 *
 * ESP32-S3 is BLE-only (no Classic). Android Settings will list a named,
 * connectable GATT peripheral; iPhone Settings will not.
 *
 * This is still not the finished LE Secure Connections + booking-token
 * handshake. It is a discoverable, pairable bench peripheral.
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

static char g_oob_hex[(kPairingOobBytes * 2) + 1];
static char g_payload[kPairingPayloadMax];
static char g_ble_name[24];
static const char *g_status = "Idle";
static bool g_ble_ok = false;

static BLEServer *g_server = nullptr;
static BLECharacteristic *g_payload_chr = nullptr;

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

class PairingServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer * /*server*/) override
    {
        g_status = "Connected";
        Serial.println("BLE: phone connected");
    }

    void onDisconnect(BLEServer * /*server*/) override
    {
        g_status = "Advertising";
        Serial.println("BLE: disconnected, advertising again");
        delay(80);
        BLEDevice::startAdvertising();
    }
};

class PairingSecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override
    {
        return 0;
    }

    void onPassKeyNotify(uint32_t pass_key) override
    {
        Serial.printf("BLE: passkey notify %u\n", static_cast<unsigned>(pass_key));
    }

    bool onSecurityRequest() override
    {
        return true;
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override
    {
        if (cmpl.success) {
            g_status = "Paired";
            Serial.println("BLE: paired (Just Works bond)");
        } else {
            g_status = "Pair failed";
            Serial.printf("BLE: pair failed, reason=%u\n", cmpl.fail_reason);
        }
    }

    bool onConfirmPIN(uint32_t /*pin*/) override
    {
        return true;
    }
};

static PairingServerCallbacks g_server_cb;
static PairingSecurityCallbacks g_security_cb;

static bool start_ble_peripheral(void)
{
    BLEDevice::init(g_ble_name);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(&g_security_cb);

    BLESecurity *security = new BLESecurity();
    security->setAuthenticationMode(ESP_LE_AUTH_BOND);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

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
    fw->setValue("bench-qr");
    dis->start();

    BLEService *svc = g_server->createService(kLmycServiceUuid);
    g_payload_chr = svc->createCharacteristic(
        kLmycPayloadUuid,
        BLECharacteristic::PROPERTY_READ);
    g_payload_chr->setValue(g_payload);
    svc->start();

    /* Keep the 31-byte advertise packet to flags + complete name + DIS UUID.
     * That is what Android Settings uses to show a named available device. */
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
    Serial.printf("BLE: advertising as %s (GATT + bondable)\n", g_ble_name);
    return true;
}

void pairing_init(void)
{
    mint_oob();
    build_ble_name();
    build_payload();
    g_ble_ok = start_ble_peripheral();
    Serial.printf("Pairing payload: %s\n", g_payload);
    Serial.printf("BLE name: %s\n", g_ble_name);
}

const char *pairing_payload(void)
{
    return g_payload;
}

const char *pairing_oob_hex(void)
{
    return g_oob_hex;
}

const char *pairing_ble_name(void)
{
    return g_ble_name;
}

const char *pairing_status(void)
{
    return g_status;
}

bool pairing_ble_ok(void)
{
    return g_ble_ok;
}

void pairing_set_status(const char *status)
{
    if (status != nullptr) {
        g_status = status;
    }
}
