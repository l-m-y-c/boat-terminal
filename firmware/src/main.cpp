/**
 * LMYC Boat Terminal — cabin status + pairing bootstrap
 *
 * Hardware: Waveshare ESP32-S3-Touch-LCD-7
 *  - Ready screen: club logo on the left half, pairing QR on the right.
 *    Status icons and Home stay on the top bar. The QR parks after pairing.
 *    Screen actions park on a right-hand rail behind a pull tab.
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

enum class Screen : uint8_t { Home, Instruments, Reports, Charts, Diagnostics };

/* The rail parks off the right edge with a 44 px ear left on the glass.
 * Content stays full width under that ear. Opening the rail overlays it. */
static constexpr lv_coord_t kTopH = 56;
static constexpr lv_coord_t kEarW = 44;
static constexpr lv_coord_t kRailInner = 160;
static constexpr lv_coord_t kRailW = kEarW + kRailInner;
static constexpr lv_coord_t kRailBtnX = kEarW + 6;
static constexpr lv_coord_t kRailBtnW = kRailInner - 12;
static constexpr lv_coord_t kRailClosedX = 800 - kEarW;
static constexpr lv_coord_t kRailOpenX = 800 - kRailW;
static constexpr lv_coord_t kContentX = 0;
static constexpr lv_coord_t kContentY = kTopH;
static constexpr lv_coord_t kContentW = 800 - kEarW;
static constexpr lv_coord_t kContentH = 480 - kTopH;
/* Snapshot is 800×480. Fit its height into the content page. */
static constexpr uint16_t kSnapFitZoom =
    static_cast<uint16_t>((static_cast<int>(kContentH) * 256) / 480);
/* 200×213 logo. 352 draws about 275×293 from the widget's top-left. */
static constexpr uint16_t kLogoZoom = 352;
static constexpr lv_coord_t kQrPlate = 348;
static constexpr lv_coord_t kQrSize = 316;
static constexpr lv_coord_t kStatusChip = 40;

static Screen g_screen = Screen::Home;
static lv_obj_t *g_page_home = nullptr;
static lv_obj_t *g_page_helm = nullptr;
static lv_obj_t *g_page_diag = nullptr;
static lv_obj_t *g_topbar = nullptr;
static lv_obj_t *g_rail = nullptr;
static lv_obj_t *g_rail_home = nullptr;
static lv_obj_t *g_rail_instr = nullptr;
static lv_obj_t *g_rail_reports = nullptr;
static lv_obj_t *g_rail_charts = nullptr;
static lv_obj_t *g_rail_diag = nullptr;
static lv_obj_t *g_rail_debug = nullptr;
static lv_obj_t *g_rail_items = nullptr;
static lv_obj_t *g_crumb_home = nullptr;
static lv_obj_t *g_crumb_reports = nullptr;
static lv_obj_t *g_crumb_details = nullptr;
static lv_obj_t *g_crumb_photo = nullptr;
static lv_obj_t *g_crumb_sep1 = nullptr;
static lv_obj_t *g_crumb_sep2 = nullptr;
static lv_obj_t *g_crumb_sep3 = nullptr;
static lv_obj_t *g_crumb_leaf = nullptr;

/* Home stays at x=408. The report path grows to its right and ends before the
 * screen edge. The rail sits under this bar, so the path stays visible. */
static constexpr lv_coord_t kCrumbHomeX = 408;
static constexpr lv_coord_t kCrumbHomeW = 88;
static constexpr lv_coord_t kCrumbReportsX = 516;
static constexpr lv_coord_t kCrumbReportsW = 86;
static constexpr lv_coord_t kCrumbDetailsX = 622;
static constexpr lv_coord_t kCrumbDetailsW = 82;
static constexpr lv_coord_t kCrumbPhotoX = 724;
static constexpr lv_coord_t kCrumbPhotoW = 68;
static lv_obj_t *g_home_status = nullptr;
static lv_obj_t *g_qr_plate = nullptr;
static lv_obj_t *g_qr_hint = nullptr;
static lv_obj_t *g_chart_empty = nullptr;
static lv_obj_t *g_chip_fix = nullptr;
static lv_obj_t *g_chip_link = nullptr;
static lv_obj_t *g_chip_qr = nullptr;
static lv_obj_t *g_status_pop = nullptr;
static lv_obj_t *g_status_pop_lbl = nullptr;
static char g_fix_msg[48] = "FIX: SIM";
static char g_link_msg[64] = "Advertising";
static int g_status_open = 0;
static bool g_qr_hold = false;
static bool g_pair_seen = false;
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
static lv_obj_t *g_helm_talk = nullptr;
static lv_obj_t *g_helm_link = nullptr;
static lv_obj_t *g_gnss_btn_lbl = nullptr;
static lv_obj_t *g_page_chart = nullptr;
static lv_obj_t *g_photo_pane = nullptr;
static lv_obj_t *g_note_pane = nullptr;
static lv_obj_t *g_note_title = nullptr;
static lv_obj_t *g_note_body = nullptr;
static lv_obj_t *g_note_caption = nullptr;
static lv_obj_t *g_photo_hint = nullptr;
static lv_obj_t *g_chart_img = nullptr;
static lv_timer_t *g_status_timer = nullptr;
static bool g_gateway_started = false;
static bool g_showing_helm = false;
static lv_obj_t *g_chart_next = nullptr;
static lv_obj_t *g_rail_ear = nullptr;
static lv_obj_t *g_rail_chevron = nullptr;
static lv_obj_t *g_rail_rule = nullptr;
static lv_obj_t *g_page_book = nullptr;
static bool g_chart_from_book = false;
static bool g_photo_full = false;
static bool g_rail_open = false;
static bool g_showing_note = false;
static bool g_pane_busy = false;
static uint8_t g_pane_anims = 0;
static lv_coord_t g_rail_x = kRailClosedX;
static lv_coord_t g_rail_y = kTopH;
static lv_coord_t g_rail_h = kContentH;
static lv_coord_t g_top_y = 0;
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
    /* 4 MHz made a photo write or read feel stuck. 16 MHz is inside the SD SPI limit. */
    if (!SD.begin(-1, SPI, 16000000)) {
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

static void refresh_chrome(void);
static void hide_status_pop(void);
static void apply_home_qr(void);
static void set_rail_open(bool open);
static void set_photo_full(bool full);
static void reset_to_photo_pane(void);
static void show_note_pane(void);
static void fill_note_pane(void);
static void apply_chart_zoom(void);
static void on_dismiss_rail(lv_event_t *e);

static void on_book_subtitle(const char * /*text*/)
{
    refresh_chrome();
}

static void hide_page(lv_obj_t *page)
{
    if (page != nullptr) {
        lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    }
}

static void show_page(lv_obj_t *page)
{
    if (page != nullptr) {
        lv_obj_clear_flag(page, LV_OBJ_FLAG_HIDDEN);
    }
}

static void apply_pages(void)
{
    // Home stays up after pairing. Instruments, reports, and charts open
    // only from their buttons, with or without a phone.
    if (g_page_home == nullptr) {
        return;
    }
    hide_page(g_page_home);
    hide_page(g_page_helm);
    hide_page(g_page_book);
    hide_page(g_page_chart);
    hide_page(g_page_diag);

    switch (g_screen) {
    case Screen::Home:
        show_page(g_page_home);
        break;
    case Screen::Instruments:
        show_page(g_page_helm);
        break;
    case Screen::Reports:
        show_page(g_page_book);
        break;
    case Screen::Charts:
        show_page(g_page_chart);
        if (g_chart_empty != nullptr) {
            if (snapshot_has_image()) {
                lv_obj_add_flag(g_chart_empty, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(g_chart_empty, LV_OBJ_FLAG_HIDDEN);
            }
        }
        break;
    case Screen::Diagnostics:
        show_page(g_page_diag);
        break;
    }
    g_showing_helm = g_screen == Screen::Instruments;
    refresh_chrome();
}

static void hide_status_pop(void)
{
    g_status_open = 0;
    if (g_status_pop != nullptr) {
        lv_obj_add_flag(g_status_pop, LV_OBJ_FLAG_HIDDEN);
    }
}

static void hide_helm_subs(void)
{
    hide_page(g_helm_talk);
    hide_page(g_helm_link);
}

static void go(Screen screen)
{
    if (screen != Screen::Charts) {
        g_chart_from_book = false;
    }
    g_screen = screen;
    hide_status_pop();
    hide_helm_subs();
    set_rail_open(false);
    set_photo_full(false);
    if (screen == Screen::Reports) {
        maint_ui_reload();
    }
    apply_pages();
}

static void on_reset_ble(lv_event_t * /*e*/)
{
    pairing_restart_advertising();
}

static void on_show_home(lv_event_t * /*e*/) { go(Screen::Home); }
static void on_show_helm(lv_event_t * /*e*/) { go(Screen::Instruments); }
static void on_show_book(lv_event_t * /*e*/) { go(Screen::Reports); }
static void on_show_diag(lv_event_t * /*e*/) { go(Screen::Diagnostics); }

static void present_snapshot_image(void)
{
    if (g_chart_img == nullptr || !snapshot_has_image()) {
        return;
    }
    const lv_img_dsc_t *dsc = snapshot_dsc();
    if (dsc == nullptr) {
        return;
    }
    if (snapshot_generation() == g_snap_seen && lv_img_get_src(g_chart_img) != nullptr) {
        return;
    }
    g_snap_seen = snapshot_generation();
    lv_img_cache_invalidate_src(dsc);
    apply_chart_zoom();
    lv_img_set_src(g_chart_img, dsc);
}

static void on_book_photo(void)
{
    g_chart_from_book = true;
    g_screen = Screen::Charts;
    set_rail_open(false);
    set_photo_full(true);
    if (maint_ui_open_on_note()) {
        show_note_pane();
    } else {
        reset_to_photo_pane();
    }
    fill_note_pane();
    apply_pages();
    present_snapshot_image();
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
    g_chart_from_book = false;
    g_screen = Screen::Charts;
    set_rail_open(false);
    set_photo_full(false);
    reset_to_photo_pane();
    apply_pages();
    present_snapshot_image();
}

static void on_hide_chart(lv_event_t * /*e*/)
{
    const bool back_to_report = g_chart_from_book;
    g_chart_from_book = false;
    g_screen = back_to_report ? Screen::Reports : Screen::Home;
    set_rail_open(false);
    set_photo_full(false);
    apply_pages();
}

static void on_show_detail_crumb(lv_event_t *e)
{
    if (g_screen == Screen::Charts && g_chart_from_book) {
        on_hide_chart(e);
    }
}

static void on_photo_crumb(lv_event_t * /*e*/) {}

static void on_book_debug(lv_event_t * /*e*/) { maint_ui_toggle_debug(); }
static void on_book_items(lv_event_t * /*e*/) { maint_ui_show_list(); }

static void show_obj(lv_obj_t *obj, bool on)
{
    if (obj == nullptr) {
        return;
    }
    if (on) {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

static void mark_crumb(lv_obj_t *btn, bool current)
{
    if (btn == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(current ? 0x0E5C55 : 0x1E3A5F), 0);
    if (current) {
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void refresh_chrome(void)
{
    const bool on_detail = g_screen == Screen::Reports && maint_ui_showing_detail();
    const bool on_photo = g_screen == Screen::Charts && g_chart_from_book;
    const bool in_reports = g_screen == Screen::Reports || on_photo;
    const char *leaf = "";
    if (!in_reports) {
        switch (g_screen) {
        case Screen::Instruments:
            leaf = "Instruments";
            break;
        case Screen::Diagnostics:
            leaf = "Diagnostics";
            break;
        case Screen::Charts:
            leaf = "Charts";
            break;
        default:
            break;
        }
    }

    if (g_crumb_home != nullptr) {
        lv_obj_set_style_bg_color(g_crumb_home,
                                  g_screen == Screen::Home ? lv_color_hex(0x0E5C55)
                                                           : lv_color_hex(0x1E3A5F),
                                  0);
    }
    const bool has_leaf = leaf[0] != '\0';
    show_obj(g_crumb_sep1, in_reports || has_leaf);
    show_obj(g_crumb_reports, in_reports);
    show_obj(g_crumb_sep2, on_detail || on_photo);
    show_obj(g_crumb_details, on_detail || on_photo);
    show_obj(g_crumb_sep3, on_photo);
    show_obj(g_crumb_photo, on_photo);
    mark_crumb(g_crumb_reports, in_reports && !on_detail && !on_photo);
    mark_crumb(g_crumb_details, on_detail);
    mark_crumb(g_crumb_photo, on_photo);
    if (g_crumb_leaf != nullptr) {
        set_label(g_crumb_leaf, leaf);
        show_obj(g_crumb_leaf, has_leaf);
        lv_obj_set_pos(g_crumb_leaf, kCrumbReportsX, 18);
        lv_obj_set_width(g_crumb_leaf, 260);
    }

    show_obj(g_rail_home, g_screen == Screen::Home);
    show_obj(g_rail_instr, g_screen == Screen::Instruments);
    show_obj(g_rail_reports, g_screen == Screen::Reports);
    show_obj(g_rail_charts, g_screen == Screen::Charts);
    show_obj(g_rail_diag, g_screen == Screen::Diagnostics);
    if (g_screen == Screen::Reports) {
        show_obj(g_rail_items, maint_ui_showing_detail());
        show_obj(g_rail_debug, !maint_ui_showing_detail());
    }
    show_obj(g_chart_next, g_screen == Screen::Charts && g_chart_from_book);
}

static void paint_chip(lv_obj_t *chip, uint32_t bg)
{
    if (chip != nullptr) {
        lv_obj_set_style_bg_color(chip, lv_color_hex(bg), 0);
    }
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

    char talkers[640] = "";
    const uint32_t now = millis();
    for (size_t i = 0; i < nmea_state_talker_count(); ++i) {
        const NmeaTalkerInfo *t = nmea_state_talker(i);
        if (t == nullptr) {
            continue;
        }
        const uint32_t age_s = (now - t->last_ms) / 1000UL;
        char row[80];
        snprintf(row, sizeof(row), "%s  %s  %lus  %lu\n",
                 t->id, t->formatters[0] ? t->formatters : "—",
                 static_cast<unsigned long>(age_s),
                 static_cast<unsigned long>(t->sentences));
        strncat(talkers, row, sizeof(talkers) - strlen(talkers) - 1);
    }
    if (talkers[0] == '\0') {
        strncpy(talkers, "Waiting for sentences...", sizeof(talkers) - 1);
    }
    set_label(g_talker_list, talkers);

    snprintf(buf, sizeof(buf), "%lu good  /  %lu bad",
             static_cast<unsigned long>(nmea_good_count()),
             static_cast<unsigned long>(nmea_bad_count()));
    set_label(g_helm_nmea, buf);
    snprintf(buf, sizeof(buf), "TCP 10110 - %lu client",
             static_cast<unsigned long>(gateway_client_count()));
    set_label(g_helm_tcp, buf);
    char ip_line[40];
    if (g_wifi_got_ip) {
        snprintf(ip_line, sizeof(ip_line), "STA %s", g_wifi_ip);
    } else {
        snprintf(ip_line, sizeof(ip_line), "STA --");
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

    present_snapshot_image();
}

static void update_home_status(void)
{
    char line[64];
    if (pairing_is_confirmed()) {
        snprintf(line, sizeof(line), "Paired\n%s", LMYC_BOAT_ID);
    } else {
        snprintf(line, sizeof(line), "%s", LMYC_BOAT_ID);
    }
    set_label(g_home_status, line);
}

/* The large code is the boot secret. It stays up while advertising, parks
 * once the phone is paired, and comes back only while g_qr_hold is set. */
static void apply_home_qr(void)
{
    const bool paired = pairing_is_confirmed();
    if (!paired || !g_pair_seen) {
        g_qr_hold = false;
    }
    g_pair_seen = paired;
    const bool show = !paired || g_qr_hold;
    show_obj(g_qr_plate, show);
    show_obj(g_qr_hint, show);
    show_obj(g_home_status, paired && !show);
    paint_chip(g_chip_qr, show ? 0x0E5C55 : 0x1E3A5F);
}

static void update_status_labels(void)
{
    apply_pages();

    if (inject_local_armed()) {
        snprintf(g_fix_msg, sizeof(g_fix_msg), "FIX: PHONE");
        paint_chip(g_chip_fix, 0x6B5420);
    } else if (nmea_sim_enabled()) {
        snprintf(g_fix_msg, sizeof(g_fix_msg), "FIX: SIM");
        paint_chip(g_chip_fix, 0x6B5420);
    } else {
        snprintf(g_fix_msg, sizeof(g_fix_msg), "FIX: BOAT");
        paint_chip(g_chip_fix, 0x1E4D3A);
    }
    const char *link = pairing_status();
    if (pairing_is_confirmed()) {
        snprintf(g_link_msg, sizeof(g_link_msg), "Paired");
        paint_chip(g_chip_link, 0x0E5C55);
    } else if (strcmp(link, "Advertising") == 0) {
        snprintf(g_link_msg, sizeof(g_link_msg), "Advertising - scan the code to pair");
        paint_chip(g_chip_link, 0x1E3A5F);
    } else {
        snprintf(g_link_msg, sizeof(g_link_msg), "%s", link);
        const bool bad = strcmp(link, "OOB mismatch") == 0 || strcmp(link, "ADV failed") == 0;
        paint_chip(g_chip_link, bad ? 0x6B5420 : 0x1E3A5F);
    }
    update_home_status();
    apply_home_qr();
    if (g_status_open == 1) {
        set_label(g_status_pop_lbl, g_fix_msg);
    } else if (g_status_open == 2) {
        set_label(g_status_pop_lbl, g_link_msg);
    }

    char http_last[80];
    char http_log[6 * 72];
    hub_http_copy_last(http_last, sizeof(http_last));
    hub_http_copy_log(http_log, sizeof(http_log));
    if (http_last[0] == '\0') {
        if (hub_http_listening()) {
            snprintf(http_last, sizeof(http_last),
                     "HTTP: listening %s, no request yet", g_ap_ip);
        } else {
            snprintf(http_last, sizeof(http_last), "HTTP: server not started");
        }
    } else {
        char shown[80];
        snprintf(shown, sizeof(shown), "HTTP: %s", http_last);
        snprintf(http_last, sizeof(http_last), "%s", shown);
    }
    if (http_log[0] == '\0') {
        snprintf(http_log, sizeof(http_log),
                 "No request has reached this boat.\n"
                 "Read Book is GET /maintenance\n"
                 "on Wi-Fi %s at %s.",
                 g_ap_ssid, g_ap_ip);
    }
    maint_ui_set_http(http_last, http_log);
    present_snapshot_image();

    if (g_showing_helm) {
        update_helm_labels();
        return;
    }
    if (g_screen != Screen::Diagnostics) {
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
    lv_obj_add_event_cb(card, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *heading = lv_label_create(card);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_color(heading, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_16, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

static lv_obj_t *add_kv_row(lv_obj_t *card, const char *key, lv_coord_t y, lv_obj_t **value_out,
                             lv_coord_t value_w = 220)
{
    lv_obj_t *k = lv_label_create(card);
    lv_label_set_text(k, key);
    lv_obj_set_style_text_color(k, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(k, &lv_font_montserrat_14, 0);
    lv_obj_align(k, LV_ALIGN_TOP_LEFT, 0, y);

    lv_obj_t *v = lv_label_create(card);
    lv_label_set_text(v, "—");
    lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, 96, y);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_width(v, value_w);
    *value_out = v;
    return v;
}

static lv_obj_t *make_icon_chip(lv_obj_t *parent, uint32_t bg, const char *icon, lv_event_cb_t cb)
{
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_set_size(chip, kStatusChip, kStatusChip);
    lv_obj_set_style_bg_color(chip, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 12, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_set_style_shadow_width(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(chip, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *lbl = lv_label_create(chip);
    lv_label_set_text(lbl, icon);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl);
    return chip;
}

static lv_obj_t *make_tile(lv_obj_t *parent, const char *caption, const char *unit,
                          lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                          lv_obj_t **value_out, lv_obj_t **sub_out)
{
    lv_obj_t *tile = lv_obj_create(parent);
    lv_obj_set_size(tile, w, h);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 16, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 14, 0);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(tile, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *cap = lv_label_create(tile);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_16, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *val = lv_label_create(tile);
    lv_label_set_text(val, "—");
    lv_obj_set_style_text_color(val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_48, 0);
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 6);
    *value_out = val;

    lv_obj_t *un = lv_label_create(tile);
    lv_label_set_text(un, unit);
    lv_obj_set_style_text_color(un, lv_color_hex(0x00B4A6), 0);
    lv_obj_set_style_text_font(un, &lv_font_montserrat_16, 0);
    lv_obj_align(un, LV_ALIGN_BOTTOM_MID, 0, 0);
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

static lv_obj_t *make_rail_nav(lv_obj_t *parent, const char *title, const char *sub,
                               uint32_t accent, lv_event_cb_t cb, lv_coord_t y)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, kRailBtnW, 96);
    lv_obj_set_pos(btn, kRailBtnX, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x12283F), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B4064), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 16, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *bar = lv_obj_create(btn);
    lv_obj_set_size(bar, 4, 40);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 2, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_label_create(btn);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_color(heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_16, 0);
    lv_obj_align(heading, LV_ALIGN_LEFT_MID, 22, -12);

    lv_obj_t *detail = lv_label_create(btn);
    lv_label_set_text(detail, sub);
    lv_obj_set_width(detail, kRailBtnW - 36);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(detail, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(detail, &lv_font_montserrat_12, 0);
    lv_obj_align(detail, LV_ALIGN_LEFT_MID, 22, 14);
    return btn;
}

static lv_obj_t *make_crumb_btn(lv_obj_t *parent, const char *text, lv_event_cb_t cb,
                                lv_coord_t x, lv_coord_t w, const lv_font_t *font)
{
    lv_obj_t *btn = make_ghost_btn(parent, text, cb, w);
    lv_obj_set_height(btn, 40);
    lv_obj_set_pos(btn, x, 8);
    lv_obj_set_style_pad_hor(btn, 2, 0);
    lv_obj_set_style_pad_ver(btn, 0, 0);
    lv_obj_t *lbl = lv_obj_get_child(btn, 0);
    if (lbl != nullptr) {
        lv_obj_set_style_text_font(lbl, font, 0);
        lv_obj_set_width(lbl, w - 8);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
    }
    return btn;
}

static void build_home(lv_obj_t *parent)
{
    /* VIRTUAL anchors the zoom at this corner. REAL mode shifts a zoomed
     * image toward the bottom-right of its box, which put the badge under
     * the QR. */
    lv_obj_t *logo = lv_img_create(parent);
    lv_img_set_src(logo, &lmyc_logo);
    lv_img_set_size_mode(logo, LV_IMG_SIZE_MODE_VIRTUAL);
    lv_img_set_pivot(logo, 0, 0);
    lv_img_set_zoom(logo, kLogoZoom);
    lv_obj_set_pos(logo, 12, 8);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_CLICKABLE);

    const lv_coord_t qr_x = kContentW - 16 - kQrPlate;
    g_qr_plate = lv_obj_create(parent);
    lv_obj_set_size(g_qr_plate, kQrPlate, kQrPlate);
    lv_obj_set_pos(g_qr_plate, qr_x, 23);
    lv_obj_set_style_bg_color(g_qr_plate, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(g_qr_plate, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_qr_plate, 18, 0);
    lv_obj_set_style_border_width(g_qr_plate, 0, 0);
    lv_obj_set_style_pad_all(g_qr_plate, 0, 0);
    lv_obj_set_style_shadow_width(g_qr_plate, 0, 0);
    lv_obj_clear_flag(g_qr_plate, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_qr_plate, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *qr = lv_qrcode_create(g_qr_plate, kQrSize, lv_color_hex(0x071422),
                                    lv_color_hex(0xFFFFFF));
    const char *payload = pairing_payload();
    lv_qrcode_update(qr, payload, strlen(payload));
    lv_obj_center(qr);
    lv_obj_clear_flag(qr, LV_OBJ_FLAG_CLICKABLE);

    g_qr_hint = lv_label_create(parent);
    lv_label_set_text(g_qr_hint, "Scan in the LMYC app");
    lv_obj_set_width(g_qr_hint, kQrPlate);
    lv_obj_set_style_text_align(g_qr_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_qr_hint, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_qr_hint, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(g_qr_hint, qr_x, 23 + kQrPlate + 8);
    lv_obj_clear_flag(g_qr_hint, LV_OBJ_FLAG_CLICKABLE);

    g_home_status = lv_label_create(parent);
    lv_label_set_text(g_home_status, LMYC_BOAT_ID);
    lv_obj_set_width(g_home_status, kQrPlate);
    lv_label_set_long_mode(g_home_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(g_home_status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_home_status, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_home_status, &lv_font_montserrat_22, 0);
    lv_obj_set_pos(g_home_status, qr_x, 168);
    lv_obj_add_flag(g_home_status, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_home_status, LV_OBJ_FLAG_CLICKABLE);
}

static void build_diag_page(lv_obj_t *parent)
{
    lv_obj_t *link = make_card(parent, "LINK", 366, kContentH);
    lv_obj_set_pos(link, 8, 0);
    lv_obj_t *boat = make_card(parent, "BOAT", 366, kContentH);
    lv_obj_set_pos(boat, 382, 0);

    add_kv_row(link, "BLE", 32, &g_ble_value);
    add_kv_row(link, "MAC", 60, &g_mac_value);
    add_kv_row(link, "Peer", 88, &g_peer_value);
    add_kv_row(link, "MTU", 116, &g_mtu_value);
    add_kv_row(link, "Conn", 144, &g_conn_value);
    add_kv_row(link, "Last", 172, &g_event_value);
    add_kv_row(link, "Rx", 200, &g_rx_value);
    add_kv_row(link, "ADV", 228, &g_adv_value);

    g_hint_value = lv_label_create(link);
    lv_label_set_text(g_hint_value, "—");
    lv_obj_set_style_text_color(g_hint_value, lv_color_hex(0xF0C674), 0);
    lv_obj_set_style_text_font(g_hint_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_hint_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_hint_value, 320);
    lv_obj_align(g_hint_value, LV_ALIGN_TOP_LEFT, 0, 270);

    lv_obj_t *boat_value = nullptr;
    lv_obj_t *tid_value = nullptr;
    lv_obj_t *name_value = nullptr;
    add_kv_row(boat, "Boat", 32, &boat_value);
    set_label(boat_value, LMYC_BOAT_ID);
    add_kv_row(boat, "Terminal", 60, &tid_value);
    set_label(tid_value, LMYC_TERMINAL_ID);
    add_kv_row(boat, "BLE name", 88, &name_value);
    set_label(name_value, pairing_ble_name());
    add_kv_row(boat, "Wi-Fi", 116, &g_wifi_value);
    add_kv_row(boat, "IP", 144, &g_ip_value);
    add_kv_row(boat, "Heap", 172, &g_heap_value);
    add_kv_row(boat, "SD", 200, &g_sd_value);
    add_kv_row(boat, "NMEA", 228, &g_nmea_value);

    g_log_value = lv_label_create(boat);
    lv_label_set_text(g_log_value, "—");
    lv_obj_set_style_text_color(g_log_value, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_log_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_log_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_log_value, 320);
    lv_obj_align(g_log_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void open_helm_sub(lv_obj_t *page)
{
    hide_helm_subs();
    show_page(page);
    if (page != nullptr) {
        lv_obj_move_foreground(page);
    }
    set_rail_open(false);
    hide_status_pop();
}

static void on_helm_talk(lv_event_t * /*e*/) { open_helm_sub(g_helm_talk); }
static void on_helm_link(lv_event_t * /*e*/) { open_helm_sub(g_helm_link); }
static void on_helm_sub_close(lv_event_t * /*e*/) { hide_helm_subs(); }

static lv_obj_t *make_helm_sub(lv_obj_t *parent, const char *title)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, kContentW, kContentH);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_set_style_shadow_width(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(page, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *heading = lv_label_create(page);
    lv_label_set_text(heading, title);
    lv_obj_set_style_text_color(heading, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(heading, 16, 14);
    lv_obj_clear_flag(heading, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *close = make_ghost_btn(page, "Close", on_helm_sub_close, 100);
    lv_obj_set_pos(close, kContentW - 16 - 100, 10);
    return page;
}

static lv_obj_t *helm_line(lv_obj_t *parent, const char *text, uint32_t color, lv_coord_t y,
                           const lv_font_t *font)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, kContentW - 32);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_pos(lbl, 16, y);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    return lbl;
}

static void build_helm_page(lv_obj_t *parent)
{
    /* Tiles fill the page. The icons sit in the empty corner of Speed and
     * each opens an overlay that covers the tiles. */
    constexpr lv_coord_t gap = 8;
    constexpr lv_coord_t margin = 8;
    const lv_coord_t tile_w = (kContentW - margin * 2 - gap) / 2;
    const lv_coord_t tile_h = (kContentH - margin * 2 - gap) / 2;

    make_tile(parent, "DEPTH", "metres", margin, margin, tile_w, tile_h, &g_tile_depth,
              nullptr);
    make_tile(parent, "SPEED", "knots SOG", margin + tile_w + gap, margin, tile_w, tile_h,
              &g_tile_sog, nullptr);
    make_tile(parent, "COURSE", "degrees COG", margin, margin + tile_h + gap, tile_w, tile_h,
              &g_tile_cog, nullptr);
    make_tile(parent, "WIND", "kn AWS", margin + tile_w + gap, margin + tile_h + gap, tile_w,
              tile_h, &g_tile_wind, &g_tile_wind_sub);

    const lv_coord_t icon_y = 16;
    const lv_coord_t link_x = kContentW - 16 - kStatusChip;
    const lv_coord_t talk_x = link_x - 8 - kStatusChip;
    lv_obj_t *talk_btn = make_icon_chip(parent, 0x1E3A5F, LV_SYMBOL_LIST, on_helm_talk);
    lv_obj_set_pos(talk_btn, talk_x, icon_y);
    lv_obj_set_style_bg_color(talk_btn, lv_color_hex(0x1B4064), LV_STATE_PRESSED);
    lv_obj_t *link_btn = make_icon_chip(parent, 0x1E3A5F, LV_SYMBOL_WIFI, on_helm_link);
    lv_obj_set_pos(link_btn, link_x, icon_y);
    lv_obj_set_style_bg_color(link_btn, lv_color_hex(0x1B4064), LV_STATE_PRESSED);

    g_helm_talk = make_helm_sub(parent, "NMEA talkers");
    helm_line(g_helm_talk, "ID      sentence      age      n", 0x8FA3B8, 64,
              &lv_font_montserrat_14);
    g_talker_list = helm_line(g_helm_talk, "Waiting for sentences...", 0xE8F0F7, 92,
                              &lv_font_montserrat_16);

    g_helm_link = make_helm_sub(parent, "Link");
    g_helm_nmea = helm_line(g_helm_link, "0 good  /  0 bad", 0xFFFFFF, 72, &lv_font_montserrat_20);
    g_helm_tcp = helm_line(g_helm_link, "TCP 10110 - 0 client", 0x8FA3B8, 128,
                           &lv_font_montserrat_20);
    g_helm_ip = helm_line(g_helm_link, "STA --", 0xFFFFFF, 184, &lv_font_montserrat_20);
    g_helm_ap = helm_line(g_helm_link, "AP 192.168.4.1", 0x00B4A6, 240, &lv_font_montserrat_20);
    g_helm_peer = helm_line(g_helm_link, "phone --", 0x8FA3B8, 296, &lv_font_montserrat_20);
}

static void place_content(lv_obj_t *page)
{
    lv_obj_set_size(page, kContentW, kContentH);
    lv_obj_set_pos(page, kContentX, kContentY);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *rail_group(lv_obj_t *rail)
{
    lv_obj_t *group = lv_obj_create(rail);
    lv_obj_set_size(group, kRailW, kContentH);
    lv_obj_set_pos(group, 0, 0);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_radius(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);
    lv_obj_clear_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    return group;
}

static lv_obj_t *rail_action(lv_obj_t *parent, const char *text, lv_event_cb_t cb, lv_coord_t y)
{
    lv_obj_t *btn = make_ghost_btn(parent, text, cb, kRailBtnW);
    lv_obj_set_height(btn, 44);
    lv_obj_set_pos(btn, kRailBtnX, y);
    return btn;
}

static lv_obj_t *crumb_sep(lv_obj_t *parent, lv_coord_t x)
{
    lv_obj_t *sep = lv_label_create(parent);
    lv_label_set_text(sep, ">");
    lv_obj_set_style_text_color(sep, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(sep, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(sep, x, 18);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_HIDDEN);
    return sep;
}

static void anim_x(void *obj, int32_t v)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
    if (obj == g_rail) {
        g_rail_x = static_cast<lv_coord_t>(v);
    }
}

static void anim_top_y(void *obj, int32_t v)
{
    g_top_y = static_cast<lv_coord_t>(v);
    lv_obj_set_y(static_cast<lv_obj_t *>(obj), g_top_y);
}

static void anim_pane_x(void *obj, int32_t v)
{
    lv_obj_set_x(static_cast<lv_obj_t *>(obj), static_cast<lv_coord_t>(v));
}

static void start_anim(lv_obj_t *obj, lv_coord_t from, lv_coord_t to, uint32_t ms,
                       lv_anim_exec_xcb_t exec, lv_anim_ready_cb_t ready)
{
    lv_anim_del(obj, exec);
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, obj);
    lv_anim_set_exec_cb(&anim, exec);
    lv_anim_set_values(&anim, from, to);
    lv_anim_set_time(&anim, ms);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    if (ready != nullptr) {
        lv_anim_set_ready_cb(&anim, ready);
    }
    lv_anim_start(&anim);
}

static void move_top_bar(lv_coord_t target, bool animate)
{
    if (g_topbar == nullptr) {
        g_top_y = target;
        return;
    }
    if (!animate || g_top_y == target) {
        lv_anim_del(g_topbar, anim_top_y);
        g_top_y = target;
        lv_obj_set_y(g_topbar, target);
        return;
    }
    start_anim(g_topbar, g_top_y, target, 200, anim_top_y, nullptr);
}

static void place_rail_column(lv_coord_t y, lv_coord_t h)
{
    g_rail_y = y;
    g_rail_h = h;
    if (g_rail == nullptr) {
        return;
    }
    lv_obj_set_size(g_rail, kRailW, h);
    lv_obj_set_pos(g_rail, g_rail_x, y);
    if (g_rail_ear != nullptr) {
        lv_obj_set_height(g_rail_ear, h);
    }
    if (g_rail_rule != nullptr) {
        lv_obj_set_height(g_rail_rule, h);
    }
    lv_obj_t *groups[] = {g_rail_home, g_rail_instr, g_rail_reports, g_rail_charts, g_rail_diag};
    for (lv_obj_t *group : groups) {
        if (group != nullptr) {
            lv_obj_set_height(group, h);
        }
    }
}

static void set_chevron(bool open)
{
    if (g_rail_chevron != nullptr) {
        lv_label_set_text(g_rail_chevron, open ? ">" : "<");
    }
}

static void set_rail_open(bool open)
{
    const lv_coord_t target = open ? kRailOpenX : kRailClosedX;
    if (g_rail == nullptr) {
        g_rail_open = open;
        g_rail_x = target;
        return;
    }
    if (open == g_rail_open && g_rail_x == target) {
        return;
    }
    g_rail_open = open;
    set_chevron(open);
    start_anim(g_rail, g_rail_x, target, 200, anim_x, nullptr);
}

static void layout_note_labels(lv_coord_t width)
{
    const lv_coord_t text_w = width > 96 ? width - 96 : width;
    if (g_note_title != nullptr) {
        lv_obj_set_width(g_note_title, text_w);
    }
    if (g_note_caption != nullptr) {
        lv_obj_set_width(g_note_caption, text_w);
    }
    if (g_note_body != nullptr) {
        lv_obj_set_width(g_note_body, text_w);
    }
}

static void apply_chart_zoom(void)
{
    if (g_chart_img == nullptr) {
        return;
    }
    const uint16_t zoom = g_photo_full ? 256 : kSnapFitZoom;
    lv_img_set_pivot(g_chart_img, kSnapshotWidth / 2, kSnapshotHeight / 2);
    lv_img_set_zoom(g_chart_img, zoom);
    lv_obj_center(g_chart_img);
}

static void show_photo_hint(bool on)
{
    show_obj(g_photo_hint, on);
}

static void set_photo_full(bool full)
{
    if (full == g_photo_full) {
        return;
    }
    g_photo_full = full;
    const lv_coord_t page_w = full ? 800 : kContentW;
    const lv_coord_t page_h = full ? 480 : kContentH;
    if (g_page_chart != nullptr) {
        if (full) {
            lv_obj_set_size(g_page_chart, page_w, page_h);
            lv_obj_set_pos(g_page_chart, 0, 0);
        } else {
            place_content(g_page_chart);
        }
    }
    if (g_photo_pane != nullptr) {
        lv_obj_set_size(g_photo_pane, page_w, page_h);
    }
    if (g_note_pane != nullptr) {
        lv_obj_set_size(g_note_pane, page_w, page_h);
    }
    layout_note_labels(page_w);
    /* Keep the bar and the rail in their normal places so Reports > Details >
     * Photo stays readable above the picture. */
    place_rail_column(kTopH, kContentH);
    move_top_bar(0, false);
    show_photo_hint(full);
    apply_chart_zoom();
}

static void fill_note_pane(void)
{
    const char *title = maint_ui_item_title();
    const char *caption = maint_ui_caption();
    const char *note = maint_ui_photo_note();
    if (g_note_title != nullptr) {
        lv_label_set_text(g_note_title, title != nullptr ? title : "");
    }
    if (g_note_caption != nullptr) {
        lv_label_set_text(g_note_caption,
                          caption != nullptr && caption[0] != '\0' ? caption : "Photo");
        if (g_note_title != nullptr) {
            lv_obj_update_layout(g_note_title);
            lv_obj_align_to(g_note_caption, g_note_title, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);
        }
    }
    if (g_note_body != nullptr) {
        const bool has_note = note != nullptr && note[0] != '\0';
        lv_label_set_text(g_note_body, has_note ? note : "No note for this photo");
        lv_obj_set_style_text_color(g_note_body,
                                    lv_color_hex(has_note ? 0xFFFFFF : 0x8FA3B8), 0);
    }
}

static void pane_anim_ready(lv_anim_t * /*a*/)
{
    if (g_pane_anims > 0) {
        --g_pane_anims;
    }
    if (g_pane_anims > 0) {
        return;
    }
    g_pane_busy = false;
    lv_obj_t *shown = g_showing_note ? g_note_pane : g_photo_pane;
    lv_obj_t *hidden = g_showing_note ? g_photo_pane : g_note_pane;
    if (shown != nullptr) {
        lv_obj_clear_flag(shown, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(shown, 0, 0);
    }
    if (hidden != nullptr) {
        lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(hidden, 800, 0);
    }
}

static void show_note_pane(void)
{
    if (g_photo_pane != nullptr) {
        lv_anim_del(g_photo_pane, anim_pane_x);
    }
    if (g_note_pane != nullptr) {
        lv_anim_del(g_note_pane, anim_pane_x);
    }
    g_pane_anims = 0;
    g_pane_busy = false;
    g_showing_note = true;
    if (g_note_pane != nullptr) {
        lv_obj_clear_flag(g_note_pane, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_note_pane, 0, 0);
        lv_obj_move_foreground(g_note_pane);
    }
    if (g_photo_pane != nullptr) {
        lv_obj_add_flag(g_photo_pane, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_photo_pane, 800, 0);
    }
}

static void reset_to_photo_pane(void)
{
    if (g_photo_pane != nullptr) {
        lv_anim_del(g_photo_pane, anim_pane_x);
    }
    if (g_note_pane != nullptr) {
        lv_anim_del(g_note_pane, anim_pane_x);
    }
    g_pane_anims = 0;
    g_pane_busy = false;
    g_showing_note = false;
    if (g_photo_pane != nullptr) {
        lv_obj_clear_flag(g_photo_pane, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_photo_pane, 0, 0);
    }
    if (g_note_pane != nullptr) {
        lv_obj_add_flag(g_note_pane, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(g_note_pane, 800, 0);
    }
}

static void slide_panes(lv_dir_t dir)
{
    if (g_pane_busy || g_photo_pane == nullptr || g_note_pane == nullptr) {
        return;
    }
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) {
        return;
    }
    lv_obj_t *incoming = g_showing_note ? g_photo_pane : g_note_pane;
    lv_obj_t *outgoing = g_showing_note ? g_note_pane : g_photo_pane;
    const lv_coord_t travel = 800;
    const lv_coord_t from = dir == LV_DIR_LEFT ? travel : -travel;
    const lv_coord_t out_to = dir == LV_DIR_LEFT ? -travel : travel;
    g_showing_note = !g_showing_note;
    fill_note_pane();
    set_rail_open(false);
    lv_obj_move_foreground(incoming);
    lv_obj_clear_flag(incoming, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(incoming, from, 0);
    lv_obj_set_pos(outgoing, 0, 0);
    g_pane_busy = true;
    g_pane_anims = 2;
    start_anim(incoming, from, 0, 220, anim_pane_x, pane_anim_ready);
    start_anim(outgoing, 0, out_to, 220, anim_pane_x, pane_anim_ready);
}

static void on_screen_gesture(lv_event_t * /*e*/)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == nullptr) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        set_rail_open(true);
    } else if (dir == LV_DIR_RIGHT) {
        if (g_rail_open) {
            set_rail_open(false);
        } else if (g_screen == Screen::Reports) {
            maint_ui_next_report();
        }
    }
    lv_indev_wait_release(indev);
}

static void on_chart_gesture(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == nullptr) {
        return;
    }
    const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        set_rail_open(true);
    } else if (dir == LV_DIR_RIGHT) {
        if (g_rail_open) {
            set_rail_open(false);
        } else if (g_chart_from_book && g_photo_full) {
            slide_panes(LV_DIR_RIGHT);
        }
    }
    lv_indev_wait_release(indev);
    (void)e;
}

static void on_dismiss_rail(lv_event_t * /*e*/)
{
    set_rail_open(false);
    hide_status_pop();
}

static void dismiss_rail(void)
{
    set_rail_open(false);
    hide_status_pop();
}

static void show_status_pop(int which, const char *text)
{
    if (g_status_pop == nullptr || g_status_pop_lbl == nullptr) {
        return;
    }
    if (g_status_open == which) {
        hide_status_pop();
        return;
    }
    g_status_open = which;
    lv_label_set_text(g_status_pop_lbl, text);
    lv_obj_set_pos(g_status_pop, 8, kTopH + 8);
    lv_obj_clear_flag(g_status_pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_status_pop);
}

static void on_status_pop(lv_event_t * /*e*/)
{
    hide_status_pop();
}

static void on_fix_chip(lv_event_t * /*e*/)
{
    show_status_pop(1, g_fix_msg);
}

static void on_link_chip(lv_event_t * /*e*/)
{
    show_status_pop(2, g_link_msg);
}

static void on_qr_chip(lv_event_t * /*e*/)
{
    if (!pairing_is_confirmed()) {
        if (g_screen != Screen::Home) {
            go(Screen::Home);
        }
        show_status_pop(3, "Scan this code in the LMYC app");
        return;
    }
    hide_status_pop();
    if (g_screen != Screen::Home) {
        g_qr_hold = true;
        go(Screen::Home);
    } else {
        g_qr_hold = !g_qr_hold;
    }
    apply_home_qr();
}

static void on_ear(lv_event_t * /*e*/)
{
    set_rail_open(!g_rail_open);
}

static lv_obj_t *make_full_pane(lv_obj_t *parent, uint32_t bg)
{
    lv_obj_t *pane = lv_obj_create(parent);
    lv_obj_set_size(pane, kContentW, kContentH);
    lv_obj_set_pos(pane, 0, 0);
    lv_obj_set_style_bg_color(pane, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(pane, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pane, 0, 0);
    lv_obj_set_style_radius(pane, 0, 0);
    lv_obj_set_style_pad_all(pane, 0, 0);
    lv_obj_clear_flag(pane, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(pane, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    return pane;
}

static void create_ui(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    g_page_helm = lv_obj_create(scr);
    place_content(g_page_helm);
    lv_obj_set_style_bg_opa(g_page_helm, LV_OPA_TRANSP, 0);
    build_helm_page(g_page_helm);
    lv_obj_add_flag(g_page_helm, LV_OBJ_FLAG_HIDDEN);

    g_page_chart = lv_obj_create(scr);
    place_content(g_page_chart);
    lv_obj_set_style_bg_color(g_page_chart, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_page_chart, LV_OPA_COVER, 0);
    /* Child objects bubble gestures. Clearing it here keeps photo swipes on this page. */
    lv_obj_clear_flag(g_page_chart, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(g_page_chart, on_chart_gesture, LV_EVENT_GESTURE, nullptr);

    g_photo_pane = make_full_pane(g_page_chart, 0x000000);
    g_chart_img = lv_img_create(g_photo_pane);
    lv_img_set_pivot(g_chart_img, kSnapshotWidth / 2, kSnapshotHeight / 2);
    lv_img_set_zoom(g_chart_img, kSnapFitZoom);
    lv_obj_center(g_chart_img);
    lv_obj_clear_flag(g_chart_img, LV_OBJ_FLAG_CLICKABLE);
    g_chart_empty = lv_label_create(g_photo_pane);
    lv_label_set_text(g_chart_empty, "No snapshot yet");
    lv_obj_set_style_text_color(g_chart_empty, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_chart_empty, &lv_font_montserrat_20, 0);
    lv_obj_center(g_chart_empty);
    g_photo_hint = lv_label_create(g_photo_pane);
    lv_label_set_text(g_photo_hint, "Swipe right for the note");
    lv_obj_set_style_text_color(g_photo_hint, lv_color_hex(0xE8F0F7), 0);
    lv_obj_set_style_text_font(g_photo_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(g_photo_hint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(g_photo_hint, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(g_photo_hint, 14, 0);
    lv_obj_set_style_pad_ver(g_photo_hint, 6, 0);
    lv_obj_set_style_radius(g_photo_hint, 12, 0);
    lv_obj_align(g_photo_hint, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_add_flag(g_photo_hint, LV_OBJ_FLAG_HIDDEN);

    g_note_pane = make_full_pane(g_page_chart, 0x071422);
    g_note_title = lv_label_create(g_note_pane);
    lv_label_set_long_mode(g_note_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_note_title, kContentW - 96);
    lv_obj_set_style_text_align(g_note_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_note_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_note_title, &lv_font_montserrat_20, 0);
    lv_label_set_text(g_note_title, "");
    lv_obj_align(g_note_title, LV_ALIGN_TOP_MID, 0, 72);
    g_note_caption = lv_label_create(g_note_pane);
    lv_label_set_long_mode(g_note_caption, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_note_caption, kContentW - 96);
    lv_obj_set_style_text_align(g_note_caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_note_caption, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_note_caption, &lv_font_montserrat_16, 0);
    lv_label_set_text(g_note_caption, "");
    lv_obj_align(g_note_caption, LV_ALIGN_TOP_MID, 0, 108);
    g_note_body = lv_label_create(g_note_pane);
    lv_label_set_long_mode(g_note_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_note_body, kContentW - 96);
    lv_obj_set_style_text_align(g_note_body, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_note_body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_note_body, &lv_font_montserrat_22, 0);
    lv_label_set_text(g_note_body, "No note for this photo");
    lv_obj_align(g_note_body, LV_ALIGN_CENTER, 0, 12);
    lv_obj_t *note_hint = lv_label_create(g_note_pane);
    lv_label_set_text(note_hint, "Swipe right for the photo");
    lv_obj_set_style_text_color(note_hint, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(note_hint, &lv_font_montserrat_14, 0);
    lv_obj_align(note_hint, LV_ALIGN_BOTTOM_MID, 0, -28);
    lv_obj_add_flag(g_note_pane, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_page_chart, LV_OBJ_FLAG_HIDDEN);

    g_page_book = lv_obj_create(scr);
    place_content(g_page_book);
    lv_obj_set_style_bg_opa(g_page_book, LV_OPA_TRANSP, 0);
    maint_ui_build(g_page_book, kContentW, kContentH, on_book_photo, on_book_subtitle,
                   dismiss_rail);
    lv_obj_add_event_cb(g_page_book, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(g_page_book, LV_OBJ_FLAG_HIDDEN);

    g_page_diag = lv_obj_create(scr);
    place_content(g_page_diag);
    lv_obj_set_style_bg_opa(g_page_diag, LV_OPA_TRANSP, 0);
    build_diag_page(g_page_diag);
    lv_obj_add_flag(g_page_diag, LV_OBJ_FLAG_HIDDEN);

    g_page_home = lv_obj_create(scr);
    place_content(g_page_home);
    lv_obj_set_style_bg_color(g_page_home, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(g_page_home, LV_OPA_COVER, 0);
    build_home(g_page_home);
    lv_obj_add_event_cb(g_page_home, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_page_helm, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_page_diag, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(scr, on_screen_gesture, LV_EVENT_GESTURE, nullptr);

    g_topbar = lv_obj_create(scr);
    lv_obj_set_size(g_topbar, 800, kTopH);
    lv_obj_set_pos(g_topbar, 0, 0);
    lv_obj_set_style_bg_color(g_topbar, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(g_topbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_topbar, 0, 0);
    lv_obj_set_style_radius(g_topbar, 0, 0);
    lv_obj_set_style_pad_all(g_topbar, 0, 0);
    lv_obj_clear_flag(g_topbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *rule = lv_obj_create(g_topbar);
    lv_obj_set_size(rule, 800, 1);
    lv_obj_align(rule, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rule, 0, 0);
    lv_obj_set_style_radius(rule, 0, 0);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

    g_chip_fix = make_icon_chip(g_topbar, 0x6B5420, LV_SYMBOL_GPS, on_fix_chip);
    lv_obj_set_pos(g_chip_fix, 8, 8);
    g_chip_link = make_icon_chip(g_topbar, 0x1E3A5F, LV_SYMBOL_BLUETOOTH, on_link_chip);
    lv_obj_set_pos(g_chip_link, 56, 8);
    g_chip_qr = make_icon_chip(g_topbar, 0x0E5C55, "QR", on_qr_chip);
    lv_obj_set_pos(g_chip_qr, 104, 8);

    g_crumb_home = make_crumb_btn(g_topbar, "Home", on_show_home, kCrumbHomeX, kCrumbHomeW,
                                  &lv_font_montserrat_16);
    g_crumb_sep1 = crumb_sep(g_topbar, 500);
    g_crumb_reports = make_crumb_btn(g_topbar, "Reports", on_show_book, kCrumbReportsX,
                                     kCrumbReportsW, &lv_font_montserrat_14);
    lv_obj_add_flag(g_crumb_reports, LV_OBJ_FLAG_HIDDEN);
    g_crumb_sep2 = crumb_sep(g_topbar, 606);
    g_crumb_details = make_crumb_btn(g_topbar, "Details", on_show_detail_crumb, kCrumbDetailsX,
                                     kCrumbDetailsW, &lv_font_montserrat_14);
    lv_obj_add_flag(g_crumb_details, LV_OBJ_FLAG_HIDDEN);
    g_crumb_sep3 = crumb_sep(g_topbar, 708);
    g_crumb_photo = make_crumb_btn(g_topbar, "Photo", on_photo_crumb, kCrumbPhotoX,
                                   kCrumbPhotoW, &lv_font_montserrat_14);
    lv_obj_add_flag(g_crumb_photo, LV_OBJ_FLAG_HIDDEN);
    g_crumb_leaf = lv_label_create(g_topbar);
    lv_label_set_text(g_crumb_leaf, "");
    lv_obj_set_pos(g_crumb_leaf, kCrumbReportsX, 18);
    lv_obj_set_width(g_crumb_leaf, 260);
    lv_label_set_long_mode(g_crumb_leaf, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(g_crumb_leaf, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_crumb_leaf, &lv_font_montserrat_16, 0);
    lv_obj_clear_flag(g_crumb_leaf, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_crumb_leaf, LV_OBJ_FLAG_HIDDEN);

    g_rail = lv_obj_create(scr);
    lv_obj_set_size(g_rail, kRailW, kContentH);
    lv_obj_set_pos(g_rail, kRailClosedX, kTopH);
    lv_obj_set_style_bg_color(g_rail, lv_color_hex(0x0B1C30), 0);
    lv_obj_set_style_bg_opa(g_rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_rail, 0, 0);
    lv_obj_set_style_radius(g_rail, 0, 0);
    lv_obj_set_style_pad_all(g_rail, 0, 0);
    lv_obj_clear_flag(g_rail, LV_OBJ_FLAG_SCROLLABLE);

    g_rail_home = rail_group(g_rail);
    make_rail_nav(g_rail_home, "Charts", "Last picture", 0x00B4A6, on_show_chart, 8);
    make_rail_nav(g_rail_home, "Reports", "Open items", 0x00B4A6, on_show_book, 112);
    make_rail_nav(g_rail_home, "Instruments", "Depth and wind", 0x00B4A6, on_show_helm, 216);
    make_rail_nav(g_rail_home, "Diagnostics", "Radio and card", 0x5C7080, on_show_diag, 320);

    g_rail_instr = rail_group(g_rail);
    lv_obj_t *gnss_btn = rail_action(g_rail_instr, "GNSS off", on_toggle_gnss, 8);
    g_gnss_btn_lbl = lv_obj_get_child(gnss_btn, 0);
    lv_obj_add_flag(g_rail_instr, LV_OBJ_FLAG_HIDDEN);

    g_rail_reports = rail_group(g_rail);
    g_rail_debug = rail_action(g_rail_reports, "Debug", on_book_debug, 8);
    g_rail_items = rail_action(g_rail_reports, "Items", on_book_items, 8);
    lv_obj_add_flag(g_rail_items, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_rail_reports, LV_OBJ_FLAG_HIDDEN);

    g_rail_charts = rail_group(g_rail);
    rail_action(g_rail_charts, "Back", on_hide_chart, 8);
    g_chart_next = rail_action(g_rail_charts, "Next photo", on_next_book_photo, 60);
    lv_obj_add_flag(g_chart_next, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(g_rail_charts, LV_OBJ_FLAG_HIDDEN);

    g_rail_diag = rail_group(g_rail);
    rail_action(g_rail_diag, "Reset BLE", on_reset_ble, 8);
    lv_obj_add_flag(g_rail_diag, LV_OBJ_FLAG_HIDDEN);

    g_rail_ear = lv_obj_create(g_rail);
    lv_obj_set_size(g_rail_ear, kEarW, kContentH);
    lv_obj_set_pos(g_rail_ear, 0, 0);
    lv_obj_set_style_bg_color(g_rail_ear, lv_color_hex(0x12283F), 0);
    lv_obj_set_style_bg_color(g_rail_ear, lv_color_hex(0x1B4064), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(g_rail_ear, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_rail_ear, 0, 0);
    lv_obj_set_style_radius(g_rail_ear, 0, 0);
    lv_obj_set_style_pad_all(g_rail_ear, 0, 0);
    lv_obj_clear_flag(g_rail_ear, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_rail_ear, on_ear, LV_EVENT_CLICKED, nullptr);
    g_rail_rule = lv_obj_create(g_rail_ear);
    lv_obj_set_size(g_rail_rule, 1, kContentH);
    lv_obj_set_pos(g_rail_rule, 0, 0);
    lv_obj_set_style_bg_color(g_rail_rule, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_opa(g_rail_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_rail_rule, 0, 0);
    lv_obj_set_style_radius(g_rail_rule, 0, 0);
    lv_obj_clear_flag(g_rail_rule, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_rail_rule, LV_OBJ_FLAG_SCROLLABLE);
    g_rail_chevron = lv_label_create(g_rail_ear);
    lv_label_set_text(g_rail_chevron, "<");
    lv_obj_set_style_text_color(g_rail_chevron, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_rail_chevron, &lv_font_montserrat_28, 0);
    lv_obj_center(g_rail_chevron);
    lv_obj_move_foreground(g_rail_ear);

    g_status_pop = lv_obj_create(scr);
    lv_obj_set_size(g_status_pop, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_pos(g_status_pop, 8, kTopH + 8);
    lv_obj_set_style_bg_color(g_status_pop, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(g_status_pop, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_status_pop, 12, 0);
    lv_obj_set_style_border_width(g_status_pop, 0, 0);
    lv_obj_set_style_pad_all(g_status_pop, 12, 0);
    lv_obj_set_style_shadow_width(g_status_pop, 0, 0);
    lv_obj_clear_flag(g_status_pop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_status_pop, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_status_pop, on_status_pop, LV_EVENT_CLICKED, nullptr);
    g_status_pop_lbl = lv_label_create(g_status_pop);
    lv_label_set_text(g_status_pop_lbl, "");
    lv_obj_set_style_text_color(g_status_pop_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_status_pop_lbl, &lv_font_montserrat_16, 0);

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
    pairing_set_wifi_bootstrap(g_ap_ssid, AP_PASSWORD, g_ap_ip);
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

    // The QR is painted once from the lmyc:// link, so the link has to exist
    // first. The radio itself still starts after the page is drawn.
    pairing_prepare();
    lvgl_port_lock(-1);
    create_ui();
    lvgl_port_unlock();

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

    Serial.println("UI ready. BLE name is in the primary ADV packet; scan in the LMYC app.");
    Serial.println("NMEA sim on (FIX: SIM) — RMC/GGA/DPT/MWV @ 1 Hz until a real talker appears.");
}

void loop()
{
    lvgl_port_handle();
    if (maint_ui_photo_pending()) {
        maint_ui_decode_queued_photo();
        if (lvgl_port_lock(-1)) {
            maint_ui_publish_queued_photo();
            lvgl_port_unlock();
        }
    } else if (maint_ui_thumb_pending()) {
        maint_ui_decode_queued_thumb();
        if (lvgl_port_lock(-1)) {
            maint_ui_publish_queued_thumb();
            lvgl_port_unlock();
        }
    }

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

    delay(5);
}
