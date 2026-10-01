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
#include "nmea.h"
#include "nmea_io.h"
#include "nmea_sim.h"
#include "nmea_state.h"
#include "gateway.h"
#include "hub_http.h"
#include "snapshot.h"
#include "inject.h"
#include "maint_store.h"
#include "maint_ui.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy src/secrets.h.example to src/secrets.h and set WIFI_SSID / WIFI_PASSWORD"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD "lmycboat"
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
static lv_obj_t *g_nmea_value = nullptr;
static lv_obj_t *g_hint_value = nullptr;
static lv_obj_t *g_log_value = nullptr;

static lv_obj_t *g_page_pair = nullptr;
static lv_obj_t *g_page_helm = nullptr;
static lv_obj_t *g_hdr_subtitle = nullptr;
static lv_obj_t *g_chip_fix = nullptr;
static lv_obj_t *g_chip_fix_lbl = nullptr;
static lv_obj_t *g_chip_link = nullptr;
static lv_obj_t *g_chip_link_lbl = nullptr;
static lv_obj_t *g_tile_depth = nullptr;
static lv_obj_t *g_tile_sog = nullptr;
static lv_obj_t *g_tile_cog = nullptr;
static lv_obj_t *g_tile_wind = nullptr;
static lv_obj_t *g_tile_wind_sub = nullptr;
static lv_obj_t *g_talker_list = nullptr;
static lv_obj_t *g_helm_nmea = nullptr;
static lv_obj_t *g_helm_tcp = nullptr;
static lv_obj_t *g_helm_ip = nullptr;
static lv_obj_t *g_helm_peer = nullptr;
static lv_obj_t *g_helm_ap = nullptr;
static lv_obj_t *g_gnss_btn_lbl = nullptr;
static lv_obj_t *g_page_chart = nullptr;
static lv_obj_t *g_chart_img = nullptr;
static lv_timer_t *g_status_timer = nullptr;
static bool g_gateway_started = false;
static bool g_showing_helm = false;
static bool g_force_qr = false;
static bool g_showing_chart = false;
static lv_obj_t *g_chart_next = nullptr;
static lv_obj_t *g_page_book = nullptr;
static bool g_showing_book = false;
static bool g_chart_from_book = false;
static uint32_t g_snap_seen = 0;
static char g_ap_ssid[24] = "LMYC-BOAT";
static char g_ap_ip[16] = "192.168.4.1";

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
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START) {
        snprintf(g_wifi_line, sizeof(g_wifi_line), "AP %s", g_ap_ssid);
        Serial.printf("Wi-Fi AP %s  %s\n", g_ap_ssid, g_ap_ip);
        if (!g_gateway_started) {
            gateway_begin();
            hub_http_start();
            g_gateway_started = true;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        auto *event = static_cast<ip_event_got_ip_t *>(event_data);
        snprintf(g_wifi_ip, sizeof(g_wifi_ip), IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_got_ip = true;
        g_wifi_want_reconnect = false;
        g_wifi_backoff_ms = 1000;
        snprintf(g_wifi_line, sizeof(g_wifi_line), "STA+AP");
        Serial.printf("Wi-Fi STA IP %s  AP %s\n", g_wifi_ip, g_ap_ip);
        if (!g_gateway_started) {
            gateway_begin();
            hub_http_start();
            g_gateway_started = true;
        }
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
    maint_store_begin();
}

static void apply_pages(void)
{
    const bool helm = pairing_is_confirmed() && !g_force_qr;
    if (g_page_pair == nullptr || g_page_helm == nullptr) {
        return;
    }
    if (g_page_chart != nullptr) {
        if (g_showing_chart && snapshot_has_image()) {
            lv_obj_add_flag(g_page_pair, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(g_page_helm, LV_OBJ_FLAG_HIDDEN);
            if (g_page_book != nullptr) {
                lv_obj_add_flag(g_page_book, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_clear_flag(g_page_chart, LV_OBJ_FLAG_HIDDEN);
            if (g_chart_from_book) {
                set_label(g_hdr_subtitle, maint_ui_caption());
                if (g_chart_next != nullptr) {
                    lv_obj_clear_flag(g_chart_next, LV_OBJ_FLAG_HIDDEN);
                }
            } else {
                set_label(g_hdr_subtitle, "Chart snapshot  ·  tap Back to return");
                if (g_chart_next != nullptr) {
                    lv_obj_add_flag(g_chart_next, LV_OBJ_FLAG_HIDDEN);
                }
            }
            g_showing_helm = false;
            return;
        }
        lv_obj_add_flag(g_page_chart, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_chart_next != nullptr) {
        lv_obj_add_flag(g_chart_next, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_showing_book && g_page_book != nullptr) {
        lv_obj_add_flag(g_page_pair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_page_helm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_page_book, LV_OBJ_FLAG_HIDDEN);
        set_label(g_hdr_subtitle, "Open maintenance  ·  tap a row to see the photo");
        g_showing_helm = false;
        return;
    }
    if (g_page_book != nullptr) {
        lv_obj_add_flag(g_page_book, LV_OBJ_FLAG_HIDDEN);
    }
    if (helm) {
        lv_obj_add_flag(g_page_pair, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_page_helm, LV_OBJ_FLAG_HIDDEN);
        set_label(g_hdr_subtitle, LMYC_BOAT_ID "  ·  live instruments");
    } else {
        lv_obj_add_flag(g_page_helm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_page_pair, LV_OBJ_FLAG_HIDDEN);
        set_label(g_hdr_subtitle, "Scan QR in the LMYC app  ·  not phone Settings");
    }
    g_showing_helm = helm;
}

static void on_reset_ble(lv_event_t * /*e*/)
{
    g_force_qr = false;
    pairing_restart_advertising();
    apply_pages();
}

static void on_show_qr(lv_event_t * /*e*/)
{
    g_force_qr = true;
    apply_pages();
}

static void on_show_helm(lv_event_t * /*e*/)
{
    g_force_qr = false;
    g_showing_chart = false;
    g_showing_book = false;
    g_chart_from_book = false;
    apply_pages();
}

static void on_show_book(lv_event_t * /*e*/)
{
    g_showing_chart = false;
    g_chart_from_book = false;
    g_showing_book = true;
    maint_ui_reload();
    apply_pages();
}

static void on_book_photo(void)
{
    g_showing_book = false;
    g_chart_from_book = true;
    g_showing_chart = true;
    apply_pages();
}

static void on_next_book_photo(lv_event_t * /*e*/)
{
    maint_ui_next_photo();
}

static void on_toggle_gnss(lv_event_t * /*e*/)
{
    const bool next = !inject_local_armed();
    inject_set_local(next);
    nmea_sim_set_emit_gnss(!next);
}

static void on_show_chart(lv_event_t * /*e*/)
{
    if (!snapshot_has_image()) {
        return;
    }
    g_chart_from_book = false;
    g_showing_book = false;
    g_showing_chart = true;
    apply_pages();
}

static void on_hide_chart(lv_event_t * /*e*/)
{
    g_showing_chart = false;
    if (g_chart_from_book) {
        g_chart_from_book = false;
        g_showing_book = true;
    }
    apply_pages();
}

static void style_chip(lv_obj_t *chip, lv_obj_t *lbl, uint32_t bg, const char *text)
{
    lv_obj_set_style_bg_color(chip, lv_color_hex(bg), 0);
    lv_label_set_text(lbl, text);
}

static void update_wifi_line(char *wifi, size_t cap)
{
    if (g_wifi_got_ip) {
        wifi_ap_record_t ap = {};
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            g_wifi_rssi = ap.rssi;
            snprintf(wifi, cap, "Up  %d dBm", static_cast<int>(ap.rssi));
            return;
        }
    }
    snprintf(wifi, cap, "%s", g_wifi_line);
}

static void update_helm_labels(void)
{
    const NmeaInstruments *inst = nmea_state_instruments();
    char buf[48];

    if (inst->have_depth) {
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(inst->depth_m));
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_label(g_tile_depth, buf);

    if (inst->have_sog) {
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(inst->sog_kn));
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_label(g_tile_sog, buf);

    if (inst->have_cog) {
        snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(inst->cog_deg));
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_label(g_tile_cog, buf);

    if (inst->have_wind) {
        snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(inst->aws_kn));
        set_label(g_tile_wind, buf);
        snprintf(buf, sizeof(buf), "%+.0f° AWA", static_cast<double>(inst->awa_deg));
        set_label(g_tile_wind_sub, buf);
    } else {
        set_label(g_tile_wind, "—");
        set_label(g_tile_wind_sub, "apparent");
    }

    char talkers[192] = "";
    const uint32_t now = millis();
    for (size_t i = 0; i < nmea_state_talker_count(); ++i) {
        const NmeaTalkerInfo *t = nmea_state_talker(i);
        if (t == nullptr) {
            continue;
        }
        const uint32_t age_s = (now - t->last_ms) / 1000UL;
        char row[64];
        snprintf(row, sizeof(row), "%s  %s  %lus  %lu\n",
                 t->id, t->formatters[0] ? t->formatters : "—",
                 static_cast<unsigned long>(age_s),
                 static_cast<unsigned long>(t->sentences));
        strncat(talkers, row, sizeof(talkers) - strlen(talkers) - 1);
    }
    if (talkers[0] == '\0') {
        strncpy(talkers, "Waiting for sentences…", sizeof(talkers) - 1);
    }
    set_label(g_talker_list, talkers);

    snprintf(buf, sizeof(buf), "%lu good  /  %lu bad",
             static_cast<unsigned long>(nmea_good_count()),
             static_cast<unsigned long>(nmea_bad_count()));
    set_label(g_helm_nmea, buf);
    snprintf(buf, sizeof(buf), "TCP 10110  ·  %lu client",
             static_cast<unsigned long>(gateway_client_count()));
    set_label(g_helm_tcp, buf);
    char ip_line[40];
    if (g_wifi_got_ip) {
        snprintf(ip_line, sizeof(ip_line), "STA %s", g_wifi_ip);
    } else {
        snprintf(ip_line, sizeof(ip_line), "STA —");
    }
    set_label(g_helm_ip, ip_line);
    char ap_line[40];
    snprintf(ap_line, sizeof(ap_line), "AP %s", g_ap_ip);
    set_label(g_helm_ap, ap_line);
    set_label(g_helm_peer, pairing_peer());
    if (g_gnss_btn_lbl != nullptr) {
        lv_label_set_text(g_gnss_btn_lbl,
                          inject_local_armed() ? "GNSS LOCAL" : "GNSS off");
    }

    if (snapshot_has_image() && snapshot_generation() != g_snap_seen &&
        g_chart_img != nullptr && snapshot_dsc() != nullptr) {
        g_snap_seen = snapshot_generation();
        lv_img_set_src(g_chart_img, snapshot_dsc());
    }
}

static void update_status_labels(void)
{
    if (!pairing_is_confirmed()) {
        g_force_qr = false;
    }
    apply_pages();

    if (inject_local_armed()) {
        style_chip(g_chip_fix, g_chip_fix_lbl, 0x6B5420, "FIX: PHONE");
    } else if (nmea_sim_enabled()) {
        style_chip(g_chip_fix, g_chip_fix_lbl, 0x6B5420, "FIX: SIM");
    } else {
        style_chip(g_chip_fix, g_chip_fix_lbl, 0x1E4D3A, "FIX: BOAT");
    }
    if (pairing_is_confirmed()) {
        style_chip(g_chip_link, g_chip_link_lbl, 0x0E5C55, "PAIRED");
    } else {
        style_chip(g_chip_link, g_chip_link_lbl, 0x1E3A5F, pairing_status());
    }

    if (g_showing_helm) {
        update_helm_labels();
        return;
    }

    char conn[24];
    char heap[24];
    char wifi[48];
    char mtu[24];
    char nmea[48];

    snprintf(conn, sizeof(conn), "%lu  R%lu W%lu",
             static_cast<unsigned long>(pairing_connect_count()),
             static_cast<unsigned long>(pairing_read_count()),
             static_cast<unsigned long>(pairing_write_count()));
    snprintf(heap, sizeof(heap), "%lu KB",
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
    snprintf(mtu, sizeof(mtu), "%u  (%uB wr)",
             static_cast<unsigned>(pairing_mtu()),
             pairing_mtu() > 3 ? pairing_mtu() - 3 : 0);
    snprintf(nmea, sizeof(nmea), "%lug/%lub %s TCP:%lu",
             static_cast<unsigned long>(nmea_good_count()),
             static_cast<unsigned long>(nmea_bad_count()),
             nmea_sim_enabled() ? "FIX:SIM" : "live",
             static_cast<unsigned long>(gateway_client_count()));
    update_wifi_line(wifi, sizeof(wifi));

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
    set_label(g_nmea_value, nmea);
    set_label(g_hint_value, pairing_hint());
    set_label(g_log_value, pairing_event_log());
}

static void status_timer_cb(lv_timer_t * /*timer*/)
{
    update_status_labels();
}

static lv_obj_t *make_card(lv_obj_t *parent, const char *title, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 14, 0);
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

static lv_obj_t *make_chip(lv_obj_t *parent, lv_obj_t **lbl_out, uint32_t bg)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, 32);
    lv_obj_set_style_bg_color(chip, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 16, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_pad_hor(chip, 14, 0);
    lv_obj_set_style_pad_ver(chip, 6, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(chip);
    lv_label_set_text(lbl, "—");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_center(lbl);
    *lbl_out = lbl;
    return chip;
}

static lv_obj_t *make_tile(lv_obj_t *parent, const char *caption, const char *unit,
                          lv_coord_t x, lv_obj_t **value_out, lv_obj_t **sub_out)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, 184, 168);
    lv_obj_set_pos(tile, x, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 16, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 14, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *cap = lv_label_create(tile);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_14, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *val = lv_label_create(tile);
    lv_label_set_text(val, "—");
    lv_obj_set_style_text_color(val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_48, 0);
    lv_obj_align(val, LV_ALIGN_LEFT_MID, 0, 4);
    *value_out = val;

    lv_obj_t *un = lv_label_create(tile);
    lv_label_set_text(un, unit);
    lv_obj_set_style_text_color(un, lv_color_hex(0x00B4A6), 0);
    lv_obj_set_style_text_font(un, &lv_font_montserrat_16, 0);
    lv_obj_align(un, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    if (sub_out != nullptr) {
        *sub_out = un;
    }
    return tile;
}

static lv_obj_t *make_ghost_btn(lv_obj_t *parent, const char *text,
                               lv_event_cb_t cb, lv_coord_t w)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(lbl);
    return btn;
}

static void build_pair_page(lv_obj_t *parent)
{
    lv_obj_t *qr_card = make_card(parent, "PAIR WITH PHONE", 340, 380);
    lv_obj_align(qr_card, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *qr = lv_qrcode_create(qr_card, 220,
                                    lv_color_hex(0x0A1628),
                                    lv_color_hex(0xFFFFFF));
    const char *payload = pairing_payload();
    lv_qrcode_update(qr, payload, strlen(payload));
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 28);

    lv_obj_t *hint = lv_label_create(qr_card);
    lv_label_set_text(hint, "Scan in the LMYC app (or paste the URI)");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x6B7C90), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -36);

    lv_obj_t *helm_btn = make_ghost_btn(qr_card, "Instruments", on_show_helm, 160);
    lv_obj_align(helm_btn, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *status_card = make_card(parent, "DIAGNOSTICS", 420, 380);
    lv_obj_align(status_card, LV_ALIGN_RIGHT_MID, 0, 0);

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
    add_kv_row(status_card, "NMEA", 292, &g_nmea_value);

    g_hint_value = lv_label_create(status_card);
    lv_label_set_text(g_hint_value, "-");
    lv_obj_set_style_text_color(g_hint_value, lv_color_hex(0xF0C674), 0);
    lv_obj_set_style_text_font(g_hint_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_hint_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_hint_value, 388);
    lv_obj_align(g_hint_value, LV_ALIGN_TOP_LEFT, 0, 312);

    g_log_value = lv_label_create(status_card);
    lv_label_set_text(g_log_value, "-");
    lv_obj_set_style_text_color(g_log_value, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_log_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_log_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_log_value, 250);
    lv_obj_align(g_log_value, LV_ALIGN_BOTTOM_LEFT, 0, -2);

    lv_obj_t *reset_btn = make_ghost_btn(status_card, "Reset BLE", on_reset_ble, 110);
    lv_obj_align(reset_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void build_helm_page(lv_obj_t *parent)
{
    make_tile(parent, "DEPTH", "metres", 0, &g_tile_depth, nullptr);
    make_tile(parent, "SPEED", "knots SOG", 196, &g_tile_sog, nullptr);
    make_tile(parent, "COURSE", "degrees COG", 392, &g_tile_cog, nullptr);
    make_tile(parent, "WIND", "kn AWS", 588, &g_tile_wind, &g_tile_wind_sub);

    lv_obj_t *talk = make_card(parent, "NMEA TALKERS", 480, 196);
    lv_obj_align(talk, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *hdr = lv_label_create(talk);
    lv_label_set_text(hdr, "ID    sentences              age    count");
    lv_obj_set_style_text_color(hdr, lv_color_hex(0x5C7080), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_12, 0);
    lv_obj_align(hdr, LV_ALIGN_TOP_LEFT, 0, 26);

    g_talker_list = lv_label_create(talk);
    lv_label_set_text(g_talker_list, "Waiting for sentences…");
    lv_obj_set_style_text_color(g_talker_list, lv_color_hex(0xE8F0F7), 0);
    lv_obj_set_style_text_font(g_talker_list, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(g_talker_list, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_talker_list, 448);
    lv_obj_align(g_talker_list, LV_ALIGN_TOP_LEFT, 0, 48);

    lv_obj_t *link = make_card(parent, "LINK", 284, 196);
    lv_obj_align(link, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    g_helm_nmea = lv_label_create(link);
    lv_label_set_text(g_helm_nmea, "0 good  /  0 bad");
    lv_obj_set_style_text_color(g_helm_nmea, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_helm_nmea, &lv_font_montserrat_16, 0);
    lv_obj_align(g_helm_nmea, LV_ALIGN_TOP_LEFT, 0, 28);

    g_helm_tcp = lv_label_create(link);
    lv_label_set_text(g_helm_tcp, "TCP 10110  ·  0 client");
    lv_obj_set_style_text_color(g_helm_tcp, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_helm_tcp, &lv_font_montserrat_14, 0);
    lv_obj_align(g_helm_tcp, LV_ALIGN_TOP_LEFT, 0, 54);

    g_helm_ip = lv_label_create(link);
    lv_label_set_text(g_helm_ip, "STA —");
    lv_obj_set_style_text_color(g_helm_ip, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_helm_ip, &lv_font_montserrat_14, 0);
    lv_obj_align(g_helm_ip, LV_ALIGN_TOP_LEFT, 0, 78);

    g_helm_ap = lv_label_create(link);
    lv_label_set_text(g_helm_ap, "AP 192.168.4.1");
    lv_obj_set_style_text_color(g_helm_ap, lv_color_hex(0x00B4A6), 0);
    lv_obj_set_style_text_font(g_helm_ap, &lv_font_montserrat_14, 0);
    lv_obj_align(g_helm_ap, LV_ALIGN_TOP_LEFT, 0, 98);

    g_helm_peer = lv_label_create(link);
    lv_label_set_text(g_helm_peer, "phone —");
    lv_obj_set_style_text_color(g_helm_peer, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_helm_peer, &lv_font_montserrat_12, 0);
    lv_obj_align(g_helm_peer, LV_ALIGN_TOP_LEFT, 0, 118);

    lv_obj_t *gnss_btn = make_ghost_btn(link, "GNSS off", on_toggle_gnss, 128);
    lv_obj_align(gnss_btn, LV_ALIGN_BOTTOM_LEFT, 0, -40);
    g_gnss_btn_lbl = lv_obj_get_child(gnss_btn, 0);

    lv_obj_t *chart_btn = make_ghost_btn(link, "Chart", on_show_chart, 120);
    lv_obj_align(chart_btn, LV_ALIGN_BOTTOM_RIGHT, 0, -40);

    lv_obj_t *qr_btn = make_ghost_btn(link, "Show QR", on_show_qr, 120);
    lv_obj_align(qr_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *book_btn = make_ghost_btn(link, "Book", on_show_book, 120);
    lv_obj_align(book_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &lmyc_logo);
    lv_img_set_zoom(logo, 128);
    lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 16, 8);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "LMYC Boat Terminal");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 128, 10);

    g_hdr_subtitle = lv_label_create(scr);
    lv_label_set_text(g_hdr_subtitle, "Scan QR in the LMYC app  ·  not phone Settings");
    lv_obj_set_style_text_color(g_hdr_subtitle, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_hdr_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_align(g_hdr_subtitle, LV_ALIGN_TOP_LEFT, 128, 46);

    g_chip_fix = make_chip(scr, &g_chip_fix_lbl, 0x6B5420);
    lv_obj_align(g_chip_fix, LV_ALIGN_TOP_RIGHT, -16, 14);
    g_chip_link = make_chip(scr, &g_chip_link_lbl, 0x1E3A5F);
    lv_obj_align(g_chip_link, LV_ALIGN_TOP_RIGHT, -130, 14);

    g_page_pair = lv_obj_create(scr);
    lv_obj_set_size(g_page_pair, 768, 392);
    lv_obj_align(g_page_pair, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(g_page_pair, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_page_pair, 0, 0);
    lv_obj_set_style_pad_all(g_page_pair, 0, 0);
    lv_obj_clear_flag(g_page_pair, LV_OBJ_FLAG_SCROLLABLE);
    build_pair_page(g_page_pair);

    g_page_helm = lv_obj_create(scr);
    lv_obj_set_size(g_page_helm, 768, 392);
    lv_obj_align(g_page_helm, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(g_page_helm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_page_helm, 0, 0);
    lv_obj_set_style_pad_all(g_page_helm, 0, 0);
    lv_obj_clear_flag(g_page_helm, LV_OBJ_FLAG_SCROLLABLE);
    build_helm_page(g_page_helm);
    lv_obj_add_flag(g_page_helm, LV_OBJ_FLAG_HIDDEN);

    g_page_chart = lv_obj_create(scr);
    lv_obj_set_size(g_page_chart, 800, 400);
    lv_obj_align(g_page_chart, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g_page_chart, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_page_chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_page_chart, 0, 0);
    lv_obj_set_style_pad_all(g_page_chart, 0, 0);
    lv_obj_clear_flag(g_page_chart, LV_OBJ_FLAG_SCROLLABLE);
    g_chart_img = lv_img_create(g_page_chart);
    lv_obj_center(g_chart_img);
    lv_obj_t *back = make_ghost_btn(g_page_chart, "Back", on_hide_chart, 120);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -16, 8);
    g_chart_next = make_ghost_btn(g_page_chart, "Next photo", on_next_book_photo, 140);
    lv_obj_align(g_chart_next, LV_ALIGN_TOP_RIGHT, -148, 8);
    lv_obj_add_flag(g_chart_next, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_page_chart, LV_OBJ_FLAG_HIDDEN);

    g_page_book = lv_obj_create(scr);
    lv_obj_set_size(g_page_book, 768, 392);
    lv_obj_align(g_page_book, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_opa(g_page_book, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_page_book, 0, 0);
    lv_obj_set_style_pad_all(g_page_book, 0, 0);
    lv_obj_clear_flag(g_page_book, LV_OBJ_FLAG_SCROLLABLE);
    maint_ui_build(g_page_book, on_show_helm, on_book_photo);
    lv_obj_add_flag(g_page_book, LV_OBJ_FLAG_HIDDEN);

    update_status_labels();
    g_status_timer = lv_timer_create(status_timer_cb, 250, nullptr);
}

static void on_nmea_sentence(const char *sentence, size_t /*len*/, void * /*ctx*/)
{
    nmea_state_note(sentence);
    gateway_send_sentence(sentence);
    pairing_nmea_notify(sentence);
}

static void start_wifi_client(void)
{
    snprintf(g_ap_ssid, sizeof(g_ap_ssid), "LMYC-%s", LMYC_BOAT_ID);
    Serial.printf("Wi-Fi: AP '%s' + STA '%s'\n", g_ap_ssid, WIFI_SSID);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
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
    nmea_init();
    nmea_state_init();
    nmea_io_init();
    nmea_sim_init();
    inject_init();
    snapshot_init();
    gateway_init();
    hub_http_init();
    nmea_set_sink(on_nmea_sentence, nullptr);
    pairing_init();
    start_wifi_client();

    lvgl_port_lock(-1);
    create_ui();
    lvgl_port_unlock();

    Serial.println("UI ready. BLE name is in the primary ADV packet; scan in the LMYC app.");
    Serial.println("NMEA sim on (FIX: SIM) — RMC/GGA/DPT/MWV @ 1 Hz until a real talker appears.");
}

void loop()
{
    if (g_wifi_want_reconnect && millis() >= g_wifi_next_try_ms) {
        g_wifi_want_reconnect = false;
        snprintf(g_wifi_line, sizeof(g_wifi_line), "Retrying...");
        Serial.println("Wi-Fi: reconnect (backoff)");
        esp_wifi_connect();
    }

    nmea_io_poll();
    if (nmea_sim_enabled() && nmea_io_quiet()) {
        nmea_sim_poll();
    }
    gateway_poll();

    delay(20);
}
