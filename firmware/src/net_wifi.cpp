#include "net_wifi.h"

#include "boat_config.h"
#include "gateway.h"
#include "hub_http.h"
#include "pairing.h"
#include "protocol.h"

#include <Arduino.h>
#include <cstring>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#if __has_include("esp_coexist.h")
#include "esp_coexist.h"
#define LMYC_HAS_COEX 1
#endif
#include "lwip/pbuf.h"
#include "lwip/netif.h"

/* Arduino-ESP32 3.x liblwip expects this hook even when we use the IDF Wi-Fi API. */
extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp)
{
    (void)inp;
    if (p != nullptr) {
        pbuf_free(p);
    }
    return 1;
}

static char g_ap_ssid[24] = "LMYC-BOAT";

/* Written on the Wi-Fi event task, read by loop() and the LVGL task. */
static volatile bool g_got_ip = false;
static volatile bool g_ap_up = false;
static volatile bool g_want_reconnect = false;
static volatile bool g_services_wanted = false;
static char g_sta_ip[16] = "-";
static char g_line[48] = "Starting...";
static uint32_t g_backoff_ms = 1000;
static volatile uint32_t g_next_try_ms = 0;
static bool g_services_started = false;

static void wifi_event_handler(void * /*arg*/, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        snprintf(g_line, sizeof(g_line), "Connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Do not hammer esp_wifi_connect() — that starves BLE advertising. */
        g_got_ip = false;
        strncpy(g_sta_ip, "-", sizeof(g_sta_ip));
        g_next_try_ms = millis() + g_backoff_ms;
        g_want_reconnect = true;
        snprintf(g_line, sizeof(g_line), "Retry %lus",
                 static_cast<unsigned long>((g_backoff_ms + 999) / 1000));
        if (g_backoff_ms < 16000) {
            g_backoff_ms *= 2;
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        snprintf(g_line, sizeof(g_line), "AP %s", g_ap_ssid);
        Serial.printf("Wi-Fi AP %s  %s\n", g_ap_ssid, kSoftApIp);
        g_ap_up = true;
        g_services_wanted = true;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        snprintf(g_sta_ip, sizeof(g_sta_ip), IPSTR, IP2STR(&event->ip_info.ip));
        g_got_ip = true;
        g_want_reconnect = false;
        g_backoff_ms = 1000;
        snprintf(g_line, sizeof(g_line), "STA+AP");
        Serial.printf("Wi-Fi STA IP %s  AP %s\n", g_sta_ip, kSoftApIp);
        g_services_wanted = true;
    }
}

void net_wifi_start(void)
{
    snprintf(g_ap_ssid, sizeof(g_ap_ssid), "LMYC-%s", LMYC_BOAT_ID);
    Serial.printf("Wi-Fi: AP '%s' + STA '%s'\n", g_ap_ssid, WIFI_SSID);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    /* The config is set from secrets.h on every boot. Keeping it in RAM means
     * Wi-Fi never writes flash, and a flash write pauses the CAN interrupt. */
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    wifi_config_t ap = {};
    strncpy(reinterpret_cast<char *>(ap.ap.ssid), g_ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = static_cast<uint8_t>(strlen(g_ap_ssid));
    strncpy(reinterpret_cast<char *>(ap.ap.password), AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.max_connection = 4;
    ap.ap.channel = 6;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    wifi_config_t sta = {};
    strncpy(reinterpret_cast<char *>(sta.sta.ssid), WIFI_SSID, sizeof(sta.sta.ssid));
    strncpy(reinterpret_cast<char *>(sta.sta.password), WIFI_PASSWORD,
            sizeof(sta.sta.password));
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    pairing_set_wifi_bootstrap(g_ap_ssid, AP_PASSWORD, kSoftApIp);
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
#ifdef LMYC_HAS_COEX
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
#endif
    ESP_ERROR_CHECK(esp_wifi_start());
}

void net_wifi_poll(void)
{
    if (g_services_wanted && !g_services_started) {
        gateway_begin();
        hub_http_start();
        g_services_started = true;
    }
    if (g_want_reconnect && static_cast<int32_t>(millis() - g_next_try_ms) >= 0) {
        g_want_reconnect = false;
        snprintf(g_line, sizeof(g_line), "Retrying...");
        Serial.println("Wi-Fi: reconnect (backoff)");
        esp_wifi_connect();
    }
}

bool net_wifi_sta_up(void) { return g_got_ip; }
const char *net_wifi_sta_ip(void) { return g_sta_ip; }
const char *net_wifi_ap_ssid(void) { return g_ap_ssid; }
const char *net_wifi_ap_ip(void) { return kSoftApIp; }
bool net_wifi_ap_up(void) { return g_ap_up; }

int net_wifi_ap_stations(void)
{
    if (!g_ap_up) {
        return 0;
    }
    wifi_sta_list_t list = {};
    if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) {
        return 0;
    }
    return static_cast<int>(list.num);
}

void net_wifi_status(char *out, size_t cap)
{
    if (g_got_ip) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            snprintf(out, cap, "Up  %d dBm", static_cast<int>(ap.rssi));
            return;
        }
    }
    snprintf(out, cap, "%s", g_line);
}
