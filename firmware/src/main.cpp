/**
 * LMYC Boat Terminal — cabin status + pairing bootstrap
 *
 * Hardware: Waveshare ESP32-S3-Touch-LCD-7
 *  - Ready screen: club logo + QR for BLE pairing
 *  - Status: Wi-Fi as a *client* (STA) + BLE advertise
 *
 * Wi-Fi credentials live in secrets.h (gitignored). Copy secrets.h.example.
 *
 * Build:   pio run
 * Upload:  pio run -t upload  (BOOT+RESET into download, then RESET after flash)
 */

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <cstring>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#if __has_include("esp_coexist.h")
#include "esp_coexist.h"
#define LMYC_HAS_COEX 1
#endif
#include "lwip/ip4_addr.h"
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
#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "lmyc_logo.h"
#include "pairing.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy src/secrets.h.example to src/secrets.h and set WIFI_SSID / WIFI_PASSWORD"
#endif

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* TF slot on this Waveshare board is SPI; CS is CH422G EXIO4. */
static constexpr int kSdMosi = 11;
static constexpr int kSdSck = 12;
static constexpr int kSdMiso = 13;
static constexpr int kSdCsExpander = 4; /* CH422G EXIO4 */

static Board *g_board = nullptr;
static lv_obj_t *g_wifi_value = nullptr;
static lv_obj_t *g_ip_value = nullptr;
static lv_obj_t *g_ble_value = nullptr;
static lv_obj_t *g_mac_value = nullptr;
static lv_obj_t *g_peer_value = nullptr;
static lv_obj_t *g_mtu_value = nullptr;
static lv_obj_t *g_conn_value = nullptr;
static lv_obj_t *g_event_value = nullptr;
static lv_obj_t *g_rx_value = nullptr;
static lv_obj_t *g_adv_value = nullptr;
static lv_obj_t *g_heap_value = nullptr;
static lv_obj_t *g_sd_value = nullptr;
static lv_obj_t *g_hint_value = nullptr;
static lv_obj_t *g_log_value = nullptr;
static lv_timer_t *g_status_timer = nullptr;

static String g_sd_summary;
static bool g_sd_ready = false;

static volatile bool g_wifi_got_ip = false;
static volatile bool g_wifi_want_reconnect = false;
static char g_wifi_ip[16] = "-";
static char g_wifi_line[48] = "Starting...";
static uint32_t g_wifi_backoff_ms = 1000;
static uint32_t g_wifi_next_try_ms = 0;
static int8_t g_wifi_rssi = 0;

static void wifi_event_handler(void * /*arg*/, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        snprintf(g_wifi_line, sizeof(g_wifi_line), "Connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        /* Do not hammer esp_wifi_connect() — that starves BLE advertising. */
        g_wifi_got_ip = false;
        strncpy(g_wifi_ip, "-", sizeof(g_wifi_ip));
        g_wifi_want_reconnect = true;
        g_wifi_next_try_ms = millis() + g_wifi_backoff_ms;
        snprintf(g_wifi_line, sizeof(g_wifi_line), "Retry %lus",
                 static_cast<unsigned long>((g_wifi_backoff_ms + 999) / 1000));
        if (g_wifi_backoff_ms < 16000) {
            g_wifi_backoff_ms *= 2;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_got_ip = true;
        g_wifi_want_reconnect = false;
        g_wifi_backoff_ms = 1000;
        snprintf(g_wifi_line, sizeof(g_wifi_line), "Connected (client)");
        Serial.printf("Wi-Fi got IP %s\n", g_wifi_ip);
    }
}

static void set_label(lv_obj_t *label, const char *text)
{
    if (label != nullptr) {
        lv_label_set_text(label, text);
    }
}

static void init_sd_card(void)
{
    g_sd_ready = false;

    auto *expander_dev = (g_board != nullptr) ? g_board->getIO_Expander() : nullptr;
    auto *expander = (expander_dev != nullptr) ? expander_dev->getBase() : nullptr;
    if (expander == nullptr) {
        g_sd_summary = "No IO expander";
        Serial.println(g_sd_summary);
        return;
    }

    expander->pinMode(kSdCsExpander, OUTPUT);
    expander->digitalWrite(kSdCsExpander, LOW);
    delay(20);

    SPI.begin(kSdSck, kSdMiso, kSdMosi, -1);
    if (!SD.begin(-1, SPI, 4000000)) {
        g_sd_summary = "Need FAT32 (not exFAT)";
        Serial.println("SD: mount failed (FAT32 card seated?)");
        expander->digitalWrite(kSdCsExpander, HIGH);
        return;
    }

    uint8_t type = SD.cardType();
    if (type == CARD_NONE) {
        g_sd_summary = "Slot empty";
        Serial.println("SD: CARD_NONE");
        return;
    }

    const char *type_name = "Unknown";
    if (type == CARD_MMC) {
        type_name = "MMC";
    } else if (type == CARD_SD) {
        type_name = "SD";
    } else if (type == CARD_SDHC) {
        type_name = "SDHC";
    }

    const uint64_t bytes = SD.cardSize();
    const unsigned long mb = static_cast<unsigned long>(bytes / (1024ULL * 1024ULL));
    g_sd_summary = String(type_name) + "  " + String(mb) + " MB";
    g_sd_ready = true;
    Serial.printf("SD: %s\n", g_sd_summary.c_str());
}

static void update_status_labels(void)
{
    char conn[24];
    char heap[24];
    char wifi[48];
    char mtu[24];

    snprintf(conn, sizeof(conn), "%lu  R%lu W%lu",
             static_cast<unsigned long>(pairing_connect_count()),
             static_cast<unsigned long>(pairing_read_count()),
             static_cast<unsigned long>(pairing_write_count()));
    snprintf(heap, sizeof(heap), "%lu KB",
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
    snprintf(mtu, sizeof(mtu), "%u  (%uB wr)",
             static_cast<unsigned>(pairing_mtu()),
             pairing_mtu() > 3 ? pairing_mtu() - 3 : 0);

    if (g_wifi_got_ip) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            g_wifi_rssi = ap.rssi;
            snprintf(wifi, sizeof(wifi), "Up  %d dBm", static_cast<int>(ap.rssi));
        } else {
            snprintf(wifi, sizeof(wifi), "%s", g_wifi_line);
        }
    } else {
        snprintf(wifi, sizeof(wifi), "%s", g_wifi_line);
    }

    set_label(g_wifi_value, wifi);
    set_label(g_ip_value, g_wifi_ip);
    set_label(g_ble_value, pairing_status());
    set_label(g_mac_value, pairing_mac());
    set_label(g_peer_value, pairing_peer());
    set_label(g_mtu_value, mtu);
    set_label(g_conn_value, conn);
    set_label(g_event_value, pairing_last_event());
    set_label(g_rx_value, pairing_last_rx());
    set_label(g_adv_value, pairing_adv_layout());
    set_label(g_heap_value, heap);
    set_label(g_sd_value, g_sd_summary.c_str());
    set_label(g_hint_value, pairing_hint());
    set_label(g_log_value, pairing_event_log());
}

static void status_timer_cb(lv_timer_t * /*timer*/)
{
    update_status_labels();
}

static void on_reset_ble(lv_event_t * /*e*/)
{
    pairing_restart_advertising();
    update_status_labels();
}

static lv_obj_t *make_card(lv_obj_t *parent, const char *title, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_label_create(card);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_color(heading, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_16, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

static lv_obj_t *add_kv_row(lv_obj_t *card, const char *key, lv_coord_t y, lv_obj_t **value_out)
{
    lv_obj_t *k = lv_label_create(card);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_color(k, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_12, 0);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, y);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "-");
    lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, 88, y);
    lv_label_set_long_mode(v, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(v, 260);
    *value_out = v;
    return v;
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A1628), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &lmyc_logo);
    lv_img_set_zoom(logo, 128);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 20, 12);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LMYC Boat Terminal");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 140, 20);

    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Use the LMYC app -> Find terminals  (not phone Settings)");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(subtitle, &lv_font_montserrat_16, 0);
    lv_obj_align(subtitle, LV_ALIGN_TOP_LEFT, 140, 58);

    lv_obj_t *qr_card = make_card(scr, "PAIR WITH PHONE", 340, 388);
    lv_obj_align(qr_card, LV_ALIGN_BOTTOM_LEFT, 16, -8);

    lv_obj_t *qr = lv_qrcode_create(qr_card, 220,
                                    lv_color_hex(0x0A1628),
                                    lv_color_hex(0xFFFFFF));
    const char *payload = pairing_payload();
    lv_qrcode_update(qr, payload, strlen(payload));
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *hint = lv_label_create(qr_card);
    lv_label_set_text(hint, "Scan QR in the LMYC app (or paste the URI)");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6B7C90), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *status_card = make_card(scr, "DIAGNOSTICS", 420, 388);
    lv_obj_align(status_card, LV_ALIGN_BOTTOM_RIGHT, -16, -8);

    lv_obj_t *boat_value = nullptr;
    lv_obj_t *tid_value = nullptr;
    lv_obj_t *name_value = nullptr;
    add_kv_row(status_card, "Boat", 22, &boat_value);
    set_label(boat_value, LMYC_BOAT_ID);
    add_kv_row(status_card, "Terminal", 40, &tid_value);
    set_label(tid_value, LMYC_TERMINAL_ID);
    add_kv_row(status_card, "BLE name", 58, &name_value);
    set_label(name_value, pairing_ble_name());
    add_kv_row(status_card, "BLE", 76, &g_ble_value);
    add_kv_row(status_card, "MAC", 94, &g_mac_value);
    add_kv_row(status_card, "Peer", 112, &g_peer_value);
    add_kv_row(status_card, "MTU", 130, &g_mtu_value);
    add_kv_row(status_card, "Conn", 148, &g_conn_value);
    add_kv_row(status_card, "Last", 166, &g_event_value);
    add_kv_row(status_card, "Rx", 184, &g_rx_value);
    add_kv_row(status_card, "ADV", 202, &g_adv_value);
    add_kv_row(status_card, "Wi-Fi", 220, &g_wifi_value);
    add_kv_row(status_card, "IP", 238, &g_ip_value);
    add_kv_row(status_card, "Heap", 256, &g_heap_value);
    add_kv_row(status_card, "SD", 274, &g_sd_value);

    g_hint_value = lv_label_create(status_card);
    lv_label_set_text(g_hint_value, "-");
    lv_obj_set_style_text_color(g_hint_value, lv_color_hex(0xF0C674), 0);
    lv_obj_set_style_text_font(g_hint_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_hint_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_hint_value, 388);
    lv_obj_align(g_hint_value, LV_ALIGN_TOP_LEFT, 0, 294);

    g_log_value = lv_label_create(status_card);
    lv_label_set_text(g_log_value, "-");
    lv_obj_set_style_text_color(g_log_value, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_log_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_log_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_log_value, 250);
    lv_obj_align(g_log_value, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    lv_obj_t *reset_btn = lv_btn_create(status_card);
    lv_obj_set_size(reset_btn, 110, 28);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(reset_btn, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_shadow_width(reset_btn, 0, 0);
    lv_obj_add_event_cb(reset_btn, on_reset_ble, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *reset_lbl = lv_label_create(reset_btn);
    lv_label_set_text(reset_lbl, "Reset BLE");
    lv_obj_set_style_text_font(reset_lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(reset_lbl);

    update_status_labels();
    g_status_timer = lv_timer_create(status_timer_cb, 250, nullptr);
}

static void start_wifi_client(void)
{
    Serial.printf("Wi-Fi: joining SSID '%s' as a client...\n", WIFI_SSID);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char *>(wifi_config.sta.ssid), WIFI_SSID,
            sizeof(wifi_config.sta.ssid));
    strncpy(reinterpret_cast<char *>(wifi_config.sta.password), WIFI_PASSWORD,
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
#ifdef LMYC_HAS_COEX
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
#endif
    ESP_ERROR_CHECK(esp_wifi_start());
}

void setup()
{
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("=== LMYC Boat Terminal ===");

    g_board = new Board();
    g_board->init();

#if LVGL_PORT_AVOID_TEARING_MODE
    auto lcd = g_board->getLCD();
    lcd->configFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
#if ESP_PANEL_DRIVERS_BUS_ENABLE_RGB && CONFIG_IDF_TARGET_ESP32S3
    auto lcd_bus = lcd->getBus();
    if (lcd_bus->getBasicAttributes().type == ESP_PANEL_BUS_TYPE_RGB) {
        static_cast<BusRGB *>(lcd_bus)->configRGB_BounceBufferSize(lcd->getFrameWidth() * 10);
    }
#endif
#endif

    if (!g_board->begin()) {
        Serial.println("ERROR: board->begin() failed");
        while (true) {
            delay(1000);
        }
    }

    lvgl_port_init(g_board->getLCD(), g_board->getTouch());

    init_sd_card();
    pairing_init();
    start_wifi_client();

    lvgl_port_lock(-1);
    create_ui();
    lvgl_port_unlock();

    Serial.println("UI ready. BLE name is in the primary ADV packet; scan in the LMYC app.");
}

void loop()
{
    if (g_wifi_want_reconnect && millis() >= g_wifi_next_try_ms) {
        g_wifi_want_reconnect = false;
        snprintf(g_wifi_line, sizeof(g_wifi_line), "Retrying...");
        Serial.println("Wi-Fi: reconnect (backoff)");
        esp_wifi_connect();
    }
    delay(200);
}
