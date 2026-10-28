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
#include <cstring>

#include <esp_display_panel.hpp>
#include <lvgl.h>
#include "lvgl_v8_port.h"
#include "lmyc_logo.h"
#include "log_tee.h"
#include "montserrat_96.h"
#include "pairing.h"
#include "nmea.h"
#include "nmea_io.h"
#include "n2k.h"
#include "n2k_diag.h"
#include "n2k_network.h"
#include "n2k_to_0183.h"
#include "nmea_state.h"
#include "gateway.h"
#include "hub_http.h"
#include "snapshot.h"
#include "inject.h"
#include "maint_store.h"
#include "maint_ui.h"
#include "net_wifi.h"
#include "sd_card.h"
#include "terminal_stats.h"

#include "boat_config.h"
#if !LMYC_HAVE_SECRETS
#error "Copy src/secrets.h.example to src/secrets.h and set WIFI_SSID / WIFI_PASSWORD"
#endif

using namespace esp_panel::drivers;
using namespace esp_panel::board;

/* TF slot on this Waveshare board is SPI; CS is CH422G EXIO4. */
static constexpr int kCanMuxExpander = 5; /* CH422G EXIO5: high = CAN, low = USB */

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
static lv_obj_t *g_pgn_value = nullptr;
static lv_obj_t *g_hint_value = nullptr;
static lv_obj_t *g_log_value = nullptr;

enum class Screen : uint8_t { Home, Instruments, Reports, Charts, Diagnostics, Data, Network };

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
/* The journey bar takes the top of the home page; the code fits below it. */
static constexpr lv_coord_t kQrPlate = 300;
static constexpr lv_coord_t kQrSize = 276;
static constexpr lv_coord_t kStatusChip = 40;

static Screen g_screen = Screen::Home;
static lv_obj_t *g_page_home = nullptr;
static lv_obj_t *g_page_helm = nullptr;
static lv_obj_t *g_page_diag = nullptr;
static lv_obj_t *g_page_data = nullptr;
static lv_obj_t *g_page_net = nullptr;
static lv_obj_t *g_rail_net = nullptr;
static lv_obj_t *g_topbar = nullptr;
static lv_obj_t *g_top_rule = nullptr;
static lv_obj_t *g_mode_badge = nullptr;
static lv_obj_t *g_mode_dot = nullptr;
static lv_obj_t *g_mode_lbl = nullptr;
static lv_obj_t *g_rail = nullptr;
static lv_obj_t *g_rail_home = nullptr;
static lv_obj_t *g_rail_instr = nullptr;
static lv_obj_t *g_rail_reports = nullptr;
static lv_obj_t *g_rail_charts = nullptr;
static lv_obj_t *g_rail_diag = nullptr;
static lv_obj_t *g_rail_data = nullptr;
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
static lv_obj_t *g_qr_plate = nullptr;
static lv_obj_t *g_home_logo = nullptr;
static lv_obj_t *g_home_title = nullptr;
static lv_obj_t *g_home_body = nullptr;
static lv_obj_t *g_home_hint = nullptr;
static constexpr int kStops = 4;
static lv_obj_t *g_stop_dot[kStops] = {};
static lv_obj_t *g_stop_mark[kStops] = {};
static lv_obj_t *g_stop_label[kStops] = {};
static lv_obj_t *g_stop_line[kStops - 1] = {};
static lv_obj_t *g_chart_empty = nullptr;
static lv_obj_t *g_chip_fix = nullptr;
static lv_obj_t *g_chip_link = nullptr;
static lv_obj_t *g_chip_qr = nullptr;
static lv_obj_t *g_status_pop = nullptr;
static lv_obj_t *g_status_pop_lbl = nullptr;
static char g_fix_msg[512] = "FIX: SIM";
static char g_link_msg[160] = "Advertising";
static int g_status_open = 0;
static bool g_qr_hold = false;
static lv_obj_t *g_tile_depth = nullptr;
static lv_obj_t *g_tile_sog = nullptr;
static lv_obj_t *g_tile_cog = nullptr;
static lv_obj_t *g_tile_wind = nullptr;
static lv_obj_t *g_tile_wind_sub = nullptr;
static lv_obj_t *g_tile_depth_sub = nullptr;
static lv_obj_t *g_talker_list = nullptr;
static lv_obj_t *g_helm_n2k_status = nullptr;
static lv_obj_t *g_helm_n2k = nullptr;
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
static lv_obj_t *g_link_chip = nullptr;
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

static void set_label(lv_obj_t *label, const char *text)
{
    if (label != nullptr) {
        lv_label_set_text(label, text);
    }
}

/* GPIO19/20 are shared. EXIO5 high wires the TJA1051; low wires USB. */
static void select_can(bool can_mode)
{
    auto *expander_dev = (g_board != nullptr) ? g_board->getIO_Expander() : nullptr;
    auto *expander = (expander_dev != nullptr) ? expander_dev->getBase() : nullptr;
    if (expander == nullptr) {
        g_log.println("N2K: no IO expander, CAN mux unchanged");
        return;
    }
    expander->pinMode(kCanMuxExpander, OUTPUT);
    expander->digitalWrite(kCanMuxExpander, can_mode ? HIGH : LOW);
}

static void refresh_chrome(void);
static void update_data_views(void);
static void update_network_view(void);
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
    hide_page(g_page_data);
    hide_page(g_page_net);

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
#if LMYC_WITH_REPORTS_CHARTS
        if (g_chart_empty != nullptr) {
            if (snapshot_has_image()) {
                lv_obj_add_flag(g_chart_empty, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(g_chart_empty, LV_OBJ_FLAG_HIDDEN);
            }
        }
#endif
        break;
    case Screen::Diagnostics:
        show_page(g_page_diag);
        break;
    case Screen::Data:
        show_page(g_page_data);
        break;
    case Screen::Network:
        show_page(g_page_net);
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
#if LMYC_WITH_REPORTS_CHARTS
    if (screen == Screen::Reports) {
        maint_ui_reload();
    }
#endif
    apply_pages();
}

static void on_reset_ble(lv_event_t * /*e*/)
{
    pairing_restart_advertising();
}

static void on_show_home(lv_event_t * /*e*/) { go(Screen::Home); }
static void on_show_helm(lv_event_t * /*e*/) { go(Screen::Instruments); }
#if LMYC_WITH_REPORTS_CHARTS
static void on_show_book(lv_event_t * /*e*/) { go(Screen::Reports); }
#endif
static void on_show_diag(lv_event_t * /*e*/) { go(Screen::Diagnostics); }
static void on_show_data(lv_event_t * /*e*/) { go(Screen::Data); }
static void on_show_net(lv_event_t * /*e*/) { go(Screen::Network); }

static void present_snapshot_image(void)
{
#if LMYC_WITH_REPORTS_CHARTS
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
#endif
}

#if LMYC_WITH_REPORTS_CHARTS
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
#endif

static void on_toggle_gnss(lv_event_t * /*e*/)
{
    const bool next = !inject_local_armed();
    inject_set_local(next);
    n2k_set_sim_gnss(!next);
}

#if LMYC_WITH_REPORTS_CHARTS
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
#endif

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

static char g_leaf[32] = "";

static void refresh_chrome(void)
{
#if LMYC_WITH_REPORTS_CHARTS
    const bool on_detail = g_screen == Screen::Reports && maint_ui_showing_detail();
#else
    const bool on_detail = false;
#endif
    const bool on_photo = g_screen == Screen::Charts && g_chart_from_book;
    const bool in_reports = g_screen == Screen::Reports || on_photo;
    const char *leaf = "";
    if (!in_reports) {
        switch (g_screen) {
        case Screen::Instruments:
            snprintf(g_leaf, sizeof(g_leaf), "Instruments %s",
                     inject_local_armed() ? "PHONE" : n2k_fix_tag());
            leaf = g_leaf;
            break;
        case Screen::Diagnostics:
            leaf = "Diagnostics";
            break;
        case Screen::Data:
            leaf = "Data source";
            break;
        case Screen::Network:
            leaf = "Network";
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
    show_obj(g_rail_data, g_screen == Screen::Data);
    show_obj(g_rail_net, g_screen == Screen::Network);
#if LMYC_WITH_REPORTS_CHARTS
    if (g_screen == Screen::Reports) {
        show_obj(g_rail_items, maint_ui_showing_detail());
        show_obj(g_rail_debug, !maint_ui_showing_detail());
    }
#endif
    show_obj(g_chart_next, g_screen == Screen::Charts && g_chart_from_book);
}

static void paint_chip(lv_obj_t *chip, uint32_t bg)
{
    if (chip != nullptr) {
        lv_obj_set_style_bg_color(chip, lv_color_hex(bg), 0);
    }
}

static int hotspot_phones(void) { return net_wifi_ap_stations(); }

static void note_link_uart(int phones)
{
    char line[140];
    snprintf(line, sizeof(line), "LINK %s phones %d  nmea %lu  ble %s  uplink %s",
             net_wifi_ap_ssid(), phones,
             static_cast<unsigned long>(gateway_client_count()),
             pairing_status(),
             net_wifi_sta_up() ? net_wifi_sta_ip() : "none");
    static char last[140] = "";
    if (strcmp(line, last) == 0) {
        return;
    }
    snprintf(last, sizeof(last), "%s", line);
    /* Both sockets (log_tee.h). */
    g_log.println(line);
}

static void paint_hotspot_chip(int phones)
{
    if (g_link_chip == nullptr) {
        return;
    }
    lv_obj_set_style_bg_color(g_link_chip, lv_color_hex(phones > 0 ? 0x1E4D3A : 0x6B5420), 0);
}

static void update_helm_labels(void)
{
    const NmeaInstruments *inst = nmea_state_instruments();
    char buf[48];

    if (inst->have_depth && nmea_state_fresh(inst->depth_ms)) {
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(inst->depth_m));
        set_label(g_tile_depth, buf);
        snprintf(buf, sizeof(buf), "metres %s", nmea_depth_ref_name(inst->depth_ref));
        set_label(g_tile_depth_sub, buf);
    } else {
        set_label(g_tile_depth, "—");
        set_label(g_tile_depth_sub, "metres");
    }

    if (inst->have_sog && nmea_state_fresh(inst->sog_ms)) {
        snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(inst->sog_kn));
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_label(g_tile_sog, buf);

    if (inst->have_cog && nmea_state_fresh(inst->cog_ms)) {
        snprintf(buf, sizeof(buf), "%.0f", static_cast<double>(inst->cog_deg));
    } else {
        snprintf(buf, sizeof(buf), "—");
    }
    set_label(g_tile_cog, buf);

    if (inst->have_wind && nmea_state_fresh(inst->wind_ms)) {
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
                 t->id, t->formatters[0] ? t->formatters : "-",
                 static_cast<unsigned long>(age_s),
                 static_cast<unsigned long>(t->sentences));
        strncat(talkers, row, sizeof(talkers) - strlen(talkers) - 1);
    }
    if (talkers[0] == '\0') {
        strncpy(talkers, "Waiting for sentences...", sizeof(talkers) - 1);
    }
    set_label(g_talker_list, talkers);

    char n2k_status[80];
    char n2k_lines[480];
    n2k_status_line(n2k_status, sizeof(n2k_status));
    n2k_pgn_lines(n2k_lines, sizeof(n2k_lines));
    set_label(g_helm_n2k_status, n2k_status);
    set_label(g_helm_n2k, n2k_lines[0] != '\0' ? n2k_lines : "No N2K messages");

    char line[96];
    const int phones = hotspot_phones();
    snprintf(line, sizeof(line), "Bluetooth  %s",
             pairing_is_confirmed() ? "Paired" : pairing_status());
    set_label(g_helm_nmea, line);
    snprintf(line, sizeof(line), "Hotspot  %s", net_wifi_ap_up() ? net_wifi_ap_ssid() : "starting");
    set_label(g_helm_tcp, line);
    snprintf(line, sizeof(line), "Phones on it  %d", phones);
    set_label(g_helm_ip, line);
    lv_obj_set_style_text_color(g_helm_ip, lv_color_hex(phones > 0 ? 0x3DDC97 : 0xE6C35C), 0);
    snprintf(line, sizeof(line), "NMEA listeners  %lu",
             static_cast<unsigned long>(gateway_client_count()));
    set_label(g_helm_ap, line);
    snprintf(line, sizeof(line), "Uplink  %s", net_wifi_sta_up() ? net_wifi_sta_ip() : "none");
    set_label(g_helm_peer, line);
    if (g_gnss_btn_lbl != nullptr) {
        lv_label_set_text(g_gnss_btn_lbl,
                          inject_local_armed() ? "GNSS LOCAL" : "GNSS off");
    }

    present_snapshot_image();
}

/* The same four stops as the phone app, worked out from what the terminal
 * sees: Bluetooth, the hotspot details handed over, phones on the hotspot,
 * and the app's relay on the gateway. Arrival does not need Bluetooth. */
enum class Stop : uint8_t { Find, Connect, WiFi, Charts, Arrived };

static Stop journey_stop(bool *problem)
{
    *problem = false;
    const bool on_hotspot = net_wifi_ap_stations() > 0;
    if (on_hotspot && gateway_client_count() > 0) {
        return Stop::Arrived;
    }
    const bool shared = pairing_wifi_shared();
    if (shared && on_hotspot) {
        return Stop::Charts;
    }
    if (pairing_is_confirmed() || (shared && pairing_is_connected())) {
        return Stop::WiFi;
    }
    if (pairing_is_connected()) {
        return Stop::Connect;
    }
    if (strcmp(pairing_status(), "OOB mismatch") == 0) {
        *problem = true;
        return Stop::Connect;
    }
    return Stop::Find;
}

static constexpr uint32_t kStopDone = 0x3DDC97;
static constexpr uint32_t kStopNow = 0xFFFFFF;
static constexpr uint32_t kStopLater = 0x3A4E66;
static constexpr uint32_t kStopProblem = 0xFF8A80;
/* The boat's name on the landing screen: a warm gold that reads at arm's length. */
static constexpr uint32_t kBoatAccent = 0xF2C94C;

static void paint_stops(Stop stop, bool problem)
{
    const int now = static_cast<int>(stop);
    for (int i = 0; i < kStops; ++i) {
        lv_obj_t *dot = g_stop_dot[i];
        if (dot == nullptr) {
            return;
        }
        const bool done = i < now;
        const bool here = i == now;
        const bool bad = here && problem;
        const uint32_t color = bad ? kStopProblem : done ? kStopDone : here ? kStopNow : kStopLater;
        lv_obj_set_style_border_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_border_width(dot, here ? 4 : 2, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(done || bad ? color : 0x071422), 0);
        lv_label_set_text(g_stop_mark[i], bad ? "!" : done ? LV_SYMBOL_OK : "");
        lv_obj_set_style_text_color(g_stop_mark[i], lv_color_hex(0x071422), 0);
        lv_obj_set_style_text_color(g_stop_label[i],
                                    lv_color_hex(here || done ? 0xFFFFFF : 0x6E8299), 0);
        if (i > 0) {
            lv_obj_set_style_bg_color(g_stop_line[i - 1],
                                      lv_color_hex(i <= now ? kStopDone : kStopLater), 0);
        }
    }
}

static void update_home_status(void)
{
    bool problem = false;
    const Stop stop = journey_stop(&problem);
    paint_stops(stop, problem);
    char body[160];
    const char *title = "";
    switch (stop) {
    case Stop::Find:
        /* A crew member stepping aboard: the boat's name first, then what
         * to do, then the network the app's boat list will show. */
        title = LMYC_BOAT_NAME;
        snprintf(body, sizeof(body), "Welcome aboard. Scan this code in the LMYC app to connect.");
        break;
    case Stop::Connect:
        if (problem) {
            title = "Could not connect";
            snprintf(body, sizeof(body), "The code did not match. Scan this code again.");
        } else {
            title = "Connecting";
            snprintf(body, sizeof(body), "A phone is connecting over Bluetooth.");
        }
        break;
    case Stop::WiFi:
        title = "Boat Wi-Fi";
        snprintf(body, sizeof(body), "The phone is joining %s.", net_wifi_ap_ssid());
        break;
    case Stop::Charts:
        title = "NMEA";
        snprintf(body, sizeof(body),
                 "The phone is on the boat Wi-Fi. Waiting for the LMYC app.");
        break;
    case Stop::Arrived: {
        title = "On board " LMYC_BOAT_NAME;
        const int phones = net_wifi_ap_stations();
        /* N2K chosen with nothing arriving: the link works, the instruments
         * are quiet. That is not a fault, so it is said calmly. A bus fault
         * is said as one, the same as the phone app. */
        N2kStats bus;
        n2k_stats(&bus);
        const bool quiet = bus.state == BusState::Opening || bus.state == BusState::Waiting ||
                           bus.state == BusState::Quiet;
        const bool fault = bus.state == BusState::BusOff || bus.state == BusState::Failed;
        snprintf(body, sizeof(body), "%s %d phone%s on the boat Wi-Fi.\n\nAnother phone? Tap QR.",
                 fault   ? "Connected, but the boat's NMEA 2000 bus has a fault."
                 : quiet ? "Connected. Waiting for data from the boat's instruments."
                         : "Chart apps are getting the boat's data.",
                 phones, phones == 1 ? "" : "s");
        break;
    }
    }
    const bool find = stop == Stop::Find;
    /* On Find the boat's name is the headline, not part of the sentence:
     * set apart by size and the boat accent colour. */
    lv_obj_set_style_text_font(g_home_title,
                               find ? &lv_font_montserrat_44 : &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(g_home_title, lv_color_hex(find ? kBoatAccent : 0xFFFFFF), 0);
    set_label(g_home_title, title);
    set_label(g_home_body, body);
    show_obj(g_home_hint, find);
    if (find) {
        char hint[48];
        snprintf(hint, sizeof(hint), "Boat Wi-Fi  %s", net_wifi_ap_ssid());
        set_label(g_home_hint, hint);
    }
    /* The title grows on Find; keep the body and hint just under it. */
    lv_obj_update_layout(g_home_title);
    lv_obj_align_to(g_home_body, g_home_title, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 14);
    lv_obj_update_layout(g_home_body);
    lv_obj_align_to(g_home_hint, g_home_body, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);
}

/* The code is the boot secret. It shows while a phone has to scan it, and
 * otherwise only while the QR chip holds it up for another phone. */
static void apply_home_qr(void)
{
    bool problem = false;
    const Stop stop = journey_stop(&problem);
    if (stop == Stop::Find) {
        g_qr_hold = false;
    }
    const bool show = stop == Stop::Find || problem || g_qr_hold;
    show_obj(g_qr_plate, show);
    show_obj(g_home_logo, !show);
    paint_chip(g_chip_qr, show ? 0x0E5C55 : 0x1E3A5F);
}

static void update_status_labels(void)
{
    apply_pages();
    update_data_views();
    update_network_view();

    const bool phone = inject_local_armed();
    const char *tag = phone ? "PHONE" : n2k_fix_tag();
    paint_chip(g_chip_fix, (!phone && strcmp(tag, "BOAT") == 0) ? 0x1E4D3A : 0x6B5420);
    char n2k_status[80];
    char n2k_lines[480];
    n2k_status_line(n2k_status, sizeof(n2k_status));
    n2k_pgn_lines(n2k_lines, sizeof(n2k_lines));
    if (n2k_lines[0] == '\0') {
        snprintf(g_fix_msg, sizeof(g_fix_msg), "FIX: %s\n%s\nNo N2K messages", tag, n2k_status);
    } else {
        snprintf(g_fix_msg, sizeof(g_fix_msg), "FIX: %s\n%s\n%s", tag, n2k_status, n2k_lines);
    }
    const char *link = pairing_status();
    const int phones = hotspot_phones();
    note_link_uart(phones);
    paint_hotspot_chip(phones);
    if (pairing_is_confirmed()) {
        paint_chip(g_chip_link, 0x0E5C55);
    } else if (strcmp(link, "Advertising") == 0) {
        paint_chip(g_chip_link, 0x1E3A5F);
    } else {
        const bool bad = strcmp(link, "OOB mismatch") == 0 || strcmp(link, "ADV failed") == 0;
        paint_chip(g_chip_link, bad ? 0x6B5420 : 0x1E3A5F);
    }
    snprintf(g_link_msg, sizeof(g_link_msg),
             "%s\nHotspot %s\n%d phone%s on it\nNMEA listeners %lu",
             pairing_is_confirmed() ? "Bluetooth paired" : link,
             net_wifi_ap_up() ? net_wifi_ap_ssid() : "starting",
             phones, phones == 1 ? "" : "s",
             static_cast<unsigned long>(gateway_client_count()));
    update_home_status();
    apply_home_qr();
    if (g_status_open == 1) {
        set_label(g_status_pop_lbl, g_fix_msg);
    } else if (g_status_open == 2) {
        set_label(g_status_pop_lbl, g_link_msg);
    }

#if LMYC_WITH_REPORTS_CHARTS
    char http_last[80];
    char http_log[6 * 72];
    hub_http_copy_last(http_last, sizeof(http_last));
    hub_http_copy_log(http_log, sizeof(http_log));
    if (http_last[0] == '\0') {
        if (hub_http_listening()) {
            snprintf(http_last, sizeof(http_last),
                     "HTTP: listening %s, no request yet", net_wifi_ap_ip());
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
                 net_wifi_ap_ssid(), net_wifi_ap_ip());
    }
    maint_ui_set_http(http_last, http_log);
    present_snapshot_image();
#endif

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
    char nmea[80];
    char pgns[80];

    snprintf(conn, sizeof(conn), "%lu  R%lu W%lu",
             static_cast<unsigned long>(pairing_connect_count()),
             static_cast<unsigned long>(pairing_read_count()),
             static_cast<unsigned long>(pairing_write_count()));
    snprintf(heap, sizeof(heap), "%lu KB",
             static_cast<unsigned long>(ESP.getFreeHeap() / 1024));
    snprintf(mtu, sizeof(mtu), "%u  (%uB wr)",
             static_cast<unsigned>(pairing_mtu()),
             pairing_mtu() > 3 ? pairing_mtu() - 3 : 0);
    n2k_status_line(nmea, sizeof(nmea));
    n2k_pgn_names(pgns, sizeof(pgns));
    snprintf(wifi, sizeof(wifi), "%s  %d phone%s",
             net_wifi_ap_up() ? net_wifi_ap_ssid() : "starting",
             hotspot_phones(), hotspot_phones() == 1 ? "" : "s");

    set_label(g_wifi_value, wifi);
    set_label(g_ip_value, net_wifi_sta_up() ? net_wifi_sta_ip() : "none");
    set_label(g_ble_value, pairing_status());
    set_label(g_mac_value, pairing_mac());
    set_label(g_peer_value, pairing_peer());
    set_label(g_mtu_value, mtu);
    set_label(g_conn_value, conn);
    set_label(g_event_value, pairing_last_event());
    set_label(g_rx_value, pairing_last_rx());
    set_label(g_adv_value, pairing_adv_layout());
    set_label(g_heap_value, heap);
#if LMYC_WITH_REPORTS_CHARTS
    set_label(g_sd_value, sd_card_summary());
#endif
    set_label(g_nmea_value, nmea);
    set_label(g_pgn_value, pgns);
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
    lv_label_set_text(v, "-");
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
    lv_obj_set_style_text_font(val, &lv_font_montserrat_96, 0);
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
                               uint32_t accent, lv_event_cb_t cb, lv_coord_t y, lv_coord_t h = 96)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, kRailBtnW, h);
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

static void build_journey_bar(lv_obj_t *parent)
{
    static const char *const kNames[kStops] = {"Find", "Connect", "Wi-Fi", "NMEA"};
    constexpr lv_coord_t kLeft = 70;
    constexpr lv_coord_t kRight = kContentW - 70;
    constexpr lv_coord_t kDot = 34;
    constexpr lv_coord_t kY = 18;
    constexpr lv_coord_t kGap = (kRight - kLeft) / (kStops - 1);
    for (int i = 0; i < kStops; ++i) {
        const lv_coord_t cx = kLeft + i * kGap;
        if (i > 0) {
            lv_obj_t *line = lv_obj_create(parent);
            lv_obj_set_size(line, kGap - kDot - 12, 4);
            lv_obj_set_pos(line, cx - kGap + kDot / 2 + 6, kY + kDot / 2 - 2);
            lv_obj_set_style_radius(line, 2, 0);
            lv_obj_set_style_border_width(line, 0, 0);
            lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
            lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            g_stop_line[i - 1] = line;
        }
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_size(dot, kDot, kDot);
        lv_obj_set_pos(dot, cx - kDot / 2, kY);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        g_stop_dot[i] = dot;
        lv_obj_t *mark = lv_label_create(dot);
        lv_label_set_text(mark, "");
        lv_obj_set_style_text_font(mark, &lv_font_montserrat_18, 0);
        lv_obj_center(mark);
        g_stop_mark[i] = mark;
        lv_obj_t *label = lv_label_create(parent);
        lv_label_set_text(label, kNames[i]);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
        lv_obj_set_width(label, 140);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(label, cx - 70, kY + kDot + 8);
        g_stop_label[i] = label;
    }
}

static void build_home(lv_obj_t *parent)
{
    build_journey_bar(parent);

    constexpr lv_coord_t kTop = 104;
    const lv_coord_t right_x = kContentW - 24 - kQrPlate;

    g_home_title = lv_label_create(parent);
    lv_label_set_text(g_home_title, "");
    lv_label_set_long_mode(g_home_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_home_title, right_x - 56);
    lv_obj_set_style_text_color(g_home_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_home_title, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(g_home_title, 32, kTop + 20);
    lv_obj_clear_flag(g_home_title, LV_OBJ_FLAG_CLICKABLE);

    g_home_body = lv_label_create(parent);
    lv_label_set_text(g_home_body, "");
    lv_label_set_long_mode(g_home_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_home_body, right_x - 56);
    lv_obj_set_style_text_color(g_home_body, lv_color_hex(0xB8C7D6), 0);
    lv_obj_set_style_text_font(g_home_body, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_line_space(g_home_body, 6, 0);
    lv_obj_set_pos(g_home_body, 32, kTop + 80);
    lv_obj_clear_flag(g_home_body, LV_OBJ_FLAG_CLICKABLE);

    g_home_hint = lv_label_create(parent);
    lv_label_set_text(g_home_hint, "");
    lv_label_set_long_mode(g_home_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_home_hint, right_x - 56);
    lv_obj_set_style_text_color(g_home_hint, lv_color_hex(0x8EA2B8), 0);
    lv_obj_set_style_text_font(g_home_hint, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(g_home_hint, 32, kTop + 200);
    lv_obj_clear_flag(g_home_hint, LV_OBJ_FLAG_CLICKABLE);

    g_home_logo = lv_img_create(parent);
    lv_img_set_src(g_home_logo, &lmyc_logo);
    lv_obj_set_pos(g_home_logo, right_x + (kQrPlate - 200) / 2, kTop + (kQrPlate - 213) / 2);
    lv_obj_clear_flag(g_home_logo, LV_OBJ_FLAG_CLICKABLE);

    g_qr_plate = lv_obj_create(parent);
    lv_obj_set_size(g_qr_plate, kQrPlate, kQrPlate);
    lv_obj_set_pos(g_qr_plate, right_x, kTop);
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

    update_home_status();
    apply_home_qr();
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
    lv_label_set_text(g_hint_value, "-");
    lv_obj_set_style_text_color(g_hint_value, lv_color_hex(0xF0C674), 0);
    lv_obj_set_style_text_font(g_hint_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_hint_value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_hint_value, 320);
    lv_obj_align(g_hint_value, LV_ALIGN_TOP_LEFT, 0, 270);

    lv_obj_t *boat_value = nullptr;
    lv_obj_t *tid_value = nullptr;
    lv_obj_t *name_value = nullptr;
    add_kv_row(boat, "Boat", 32, &boat_value);
    set_label(boat_value, LMYC_BOAT_NAME);
    add_kv_row(boat, "Terminal", 60, &tid_value);
    set_label(tid_value, LMYC_TERMINAL_ID);
    add_kv_row(boat, "BLE name", 88, &name_value);
    set_label(name_value, pairing_ble_name());
    add_kv_row(boat, "Hotspot", 116, &g_wifi_value);
    add_kv_row(boat, "Uplink", 144, &g_ip_value);
    add_kv_row(boat, "Heap", 172, &g_heap_value);
#if LMYC_WITH_REPORTS_CHARTS
    add_kv_row(boat, "SD", 200, &g_sd_value);
#endif
    add_kv_row(boat, "NMEA", 228, &g_nmea_value, 230);
    add_kv_row(boat, "PGN", 256, &g_pgn_value, 230);

    g_log_value = lv_label_create(boat);
    lv_label_set_text(g_log_value, "-");
    lv_obj_set_style_text_color(g_log_value, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_log_value, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_log_value, LV_LABEL_LONG_DOT);
    lv_obj_set_size(g_log_value, 320, 64);
    lv_obj_align(g_log_value, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

/* ---- Data source ------------------------------------------------------
 * The badge on the top bar says on every screen whether the numbers are
 * simulated or live. The Data source page switches between the two and
 * shows the receive path's counters. */

static constexpr uint32_t kAmber = 0xF0C674;
static constexpr uint32_t kGreen = 0x3DDC97;
static constexpr uint32_t kRed = 0xFF8A80;
static constexpr uint32_t kMuted = 0x8FA3B8;

enum Metric : uint8_t {
    kMetricMps,
    kMetricFps,
    kMetricPeak,
    kMetricLost,
    kMetricBuffer,
    kMetricErrors,
    kMetricPgns,
    kMetricDevices,
    kMetricApps,
    kMetricCount
};

static lv_obj_t *g_metric[kMetricCount] = {};
static lv_obj_t *g_src_panel[2] = {};
static lv_obj_t *g_src_active[2] = {};
static lv_obj_t *g_src_use[2] = {};
static lv_obj_t *g_bus_state = nullptr;
static lv_obj_t *g_bus_sub = nullptr;
static lv_obj_t *g_bus_foot = nullptr;
static lv_obj_t *g_confirm = nullptr;
static lv_obj_t *g_confirm_title = nullptr;
static lv_obj_t *g_confirm_body = nullptr;
static lv_obj_t *g_restarting = nullptr;
static lv_obj_t *g_restarting_lbl = nullptr;
static DataSource g_confirm_target = DataSource::Simulator;

/* 1234567 -> "1,234,567". */
static void fmt_count(uint32_t v, char *out, size_t cap)
{
    char raw[16];
    snprintf(raw, sizeof(raw), "%lu", static_cast<unsigned long>(v));
    const size_t n = strlen(raw);
    size_t o = 0;
    for (size_t i = 0; i < n && o + 2 < cap; ++i) {
        if (i > 0 && (n - i) % 3 == 0) {
            out[o++] = ',';
        }
        out[o++] = raw[i];
    }
    out[o] = '\0';
}

struct ModeLook {
    const char *text;
    uint32_t bg;
    uint32_t dot;
};

static ModeLook mode_look(const N2kStats &s)
{
    switch (s.state) {
    case BusState::Simulated:
        return {"SIMULATION", 0x5A4718, kAmber};
    case BusState::Paused:
        return {"SIMULATION  PAUSED", 0x5A4718, kAmber};
    case BusState::Live:
        return {"LIVE  NMEA 2000", 0x174A36, kGreen};
    case BusState::Opening:
        return {"NMEA 2000  STARTING", 0x1E3A5F, kMuted};
    case BusState::Waiting:
    case BusState::Quiet:
        return {"NMEA 2000  NO DATA", 0x5C2626, kRed};
    case BusState::BusOff:
    case BusState::Failed:
        return {"NMEA 2000  BUS FAULT", 0x5C2626, kRed};
    }
    return {"", 0x1E3A5F, kMuted};
}

static void show_restarting(DataSource next)
{
    if (g_restarting == nullptr) {
        return;
    }
    char line[96];
    snprintf(line, sizeof(line), "Restarting on %s" "\n\n" "Phones reconnect on their own.",
             n2k_source_name(next));
    set_label(g_restarting_lbl, line);
    lv_obj_clear_flag(g_restarting, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_restarting);
}

static void hide_confirm(void)
{
    if (g_confirm != nullptr) {
        lv_obj_add_flag(g_confirm, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ask_switch(DataSource next)
{
    if (g_confirm == nullptr || next == n2k_source() || n2k_restart_pending()) {
        return;
    }
    g_confirm_target = next;
    if (next == DataSource::Nmea2000) {
        set_label(g_confirm_title, "Switch to NMEA 2000?");
        set_label(g_confirm_body,
                  "The simulator stops and the terminal joins the boat's backbone as a "
                  "standard NMEA 2000 node. It claims an address and sends no instrument data.\n\n"
                  "The terminal restarts. USB serial moves to the UART socket.");
    } else {
        set_label(g_confirm_title, "Switch to the simulator?");
        set_label(g_confirm_body,
                  "Chart apps and the phone will get simulated data, not the boat's. "
                  "Use it on the bench, never under way.\n\n"
                  "The terminal restarts.");
    }
    set_rail_open(false);
    hide_status_pop();
    lv_obj_clear_flag(g_confirm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_confirm);
}

static void on_use_n2k(lv_event_t * /*e*/) { ask_switch(DataSource::Nmea2000); }
static void on_use_sim(lv_event_t * /*e*/) { ask_switch(DataSource::Simulator); }
static void on_confirm_cancel(lv_event_t * /*e*/) { hide_confirm(); }

static void on_confirm_go(lv_event_t * /*e*/)
{
    if (n2k_switch_source(g_confirm_target)) {
        hide_confirm();
        show_restarting(g_confirm_target);
        return;
    }
    set_label(g_confirm_body, "The terminal could not save the choice. Nothing changed.");
}

static void on_mode_badge(lv_event_t * /*e*/)
{
    if (g_screen != Screen::Data) {
        go(Screen::Data);
    }
}

static void update_data_views(void)
{
    N2kStats s;
    n2k_stats(&s);
    const ModeLook look = mode_look(s);
    if (g_mode_badge != nullptr) {
        lv_obj_set_style_bg_color(g_mode_badge, lv_color_hex(look.bg), 0);
        lv_obj_set_style_bg_color(g_mode_dot, lv_color_hex(look.dot), 0);
        set_label(g_mode_lbl, look.text);
    }
    if (g_top_rule != nullptr) {
        lv_obj_set_style_bg_color(g_top_rule, lv_color_hex(look.dot), 0);
    }
    if (g_screen != Screen::Data || g_page_data == nullptr) {
        return;
    }

    const int active = s.source == DataSource::Simulator ? 0 : 1;
    for (int i = 0; i < 2; ++i) {
        const bool on = i == active;
        show_obj(g_src_active[i], on);
        show_obj(g_src_use[i], !on && !n2k_restart_pending());
        lv_obj_set_style_border_color(g_src_panel[i], lv_color_hex(on ? look.dot : 0x1E3A5F), 0);
    }

    const bool sim = s.source == DataSource::Simulator;
    uint32_t state_color = kGreen;
    if (sim) {
        state_color = kAmber;
    } else if (s.state != BusState::Live) {
        state_color = s.state == BusState::Opening ? kMuted : kRed;
    }
    set_label(g_bus_state, n2k_state_name(s.state));
    lv_obj_set_style_text_color(g_bus_state, lv_color_hex(state_color), 0);
    char node[64];
    if (sim) {
        snprintf(node, sizeof(node), "Virtual bus. CAN closed. Not the boat's data.");
    } else if (s.node == NodeState::Claimed) {
        snprintf(node, sizeof(node), "NMEA 2000 node, address %u. 250 kbit/s.", static_cast<unsigned>(s.address));
    } else {
        snprintf(node, sizeof(node), "%s. 250 kbit/s.", n2k_node_state_name(s.node));
    }
    set_label(g_bus_sub, node);

    char v[32];
    snprintf(v, sizeof(v), "%lu", static_cast<unsigned long>(s.messages_per_s));
    set_label(g_metric[kMetricMps], v);
    snprintf(v, sizeof(v), "%lu", static_cast<unsigned long>(s.frames_per_s));
    set_label(g_metric[kMetricFps], v);
    snprintf(v, sizeof(v), "%lu", static_cast<unsigned long>(s.frames_per_s_peak));
    set_label(g_metric[kMetricPeak], v);
    const uint32_t lost = s.lost_queue + s.lost_driver + s.lost_controller;
    fmt_count(lost, v, sizeof(v));
    set_label(g_metric[kMetricLost], v);
    lv_obj_set_style_text_color(g_metric[kMetricLost], lv_color_hex(lost == 0 ? kGreen : kRed), 0);
    snprintf(v, sizeof(v), "%lu / %lu", static_cast<unsigned long>(s.queue_peak),
             static_cast<unsigned long>(s.queue_capacity));
    set_label(g_metric[kMetricBuffer], v);
    lv_obj_set_style_text_color(g_metric[kMetricBuffer],
                                lv_color_hex(s.queue_peak * 2 > s.queue_capacity ? kAmber : 0xFFFFFF), 0);
    fmt_count(s.bus_errors, v, sizeof(v));
    set_label(g_metric[kMetricErrors], v);
    lv_obj_set_style_text_color(g_metric[kMetricErrors], lv_color_hex(s.bus_errors == 0 ? 0xFFFFFF : kAmber),
                                0);
    snprintf(v, sizeof(v), "%lu", static_cast<unsigned long>(s.pgn_types));
    set_label(g_metric[kMetricPgns], v);
    snprintf(v, sizeof(v), "%lu", static_cast<unsigned long>(s.devices));
    set_label(g_metric[kMetricDevices], v);

    GatewayStats g = {};
    gateway_stats(&g);
    snprintf(v, sizeof(v), "%lu", static_cast<unsigned long>(g.clients));
    set_label(g_metric[kMetricApps], v);

    char frames[16], sent[16], age[24];
    fmt_count(s.frames, frames, sizeof(frames));
    fmt_count(g.sentences, sent, sizeof(sent));
    if (s.last_frame_ms == UINT32_MAX) {
        snprintf(age, sizeof(age), "none yet");
    } else if (s.last_frame_ms < 1000) {
        snprintf(age, sizeof(age), "%lu ms ago", static_cast<unsigned long>(s.last_frame_ms));
    } else {
        snprintf(age, sizeof(age), "%lu s ago", static_cast<unsigned long>(s.last_frame_ms / 1000));
    }
    char foot[280];
    if (s.hint != N2kHint::None) {
        /* The one thing to do next replaces the counters until it clears. */
        snprintf(foot, sizeof(foot), "%s", n2k_hint_text(s.hint));
        lv_obj_set_style_text_color(g_bus_foot, lv_color_hex(s.hint == N2kHint::TwoSources ? kMuted : kAmber), 0);
    } else {
        snprintf(foot, sizeof(foot),
                 "%s frames since boot, last %s.\n%s sentences to chart apps, %lu not delivered.",
                 frames, age, sent, static_cast<unsigned long>(g.lost_tcp + g.lost_udp));
        lv_obj_set_style_text_color(g_bus_foot, lv_color_hex(kMuted), 0);
    }
    set_label(g_bus_foot, foot);
}

static lv_obj_t *make_metric(lv_obj_t *parent, const char *caption, lv_coord_t x, lv_coord_t y,
                             lv_coord_t w)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, w, 72);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0B1C30), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_pad_all(box, 10, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(box, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *cap = lv_label_create(box);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_color(cap, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *val = lv_label_create(box);
    lv_label_set_text(val, "-");
    lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
    lv_obj_set_width(val, w - 20);
    lv_obj_set_style_text_color(val, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    return val;
}

static void make_source_option(lv_obj_t *card, int index, const char *title, const char *body,
                               const char *use, lv_event_cb_t cb, uint32_t accent, lv_coord_t y)
{
    lv_obj_t *panel = lv_obj_create(card);
    lv_obj_set_size(panel, 268, 150);
    lv_obj_set_pos(panel, 0, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0B1C30), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(panel, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    g_src_panel[index] = panel;

    lv_obj_t *head = lv_label_create(panel);
    lv_label_set_text(head, title);
    lv_obj_set_style_text_color(head, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(head, &lv_font_montserrat_22, 0);
    lv_obj_align(head, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *text = lv_label_create(panel);
    lv_label_set_text(text, body);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text, 240);
    lv_obj_set_style_text_color(text, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(text, &lv_font_montserrat_14, 0);
    lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 32);

    lv_obj_t *chip = lv_obj_create(panel);
    lv_obj_set_size(chip, 84, 30);
    lv_obj_align(chip, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_20, 0);
    lv_obj_set_style_border_width(chip, 0, 0);
    lv_obj_set_style_radius(chip, 15, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *chip_lbl = lv_label_create(chip);
    lv_label_set_text(chip_lbl, "ACTIVE");
    lv_obj_set_style_text_color(chip_lbl, lv_color_hex(accent), 0);
    lv_obj_set_style_text_font(chip_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(chip_lbl);
    g_src_active[index] = chip;

    lv_obj_t *btn = make_ghost_btn(panel, use, cb, 170);
    lv_obj_set_height(btn, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    g_src_use[index] = btn;
}

static lv_obj_t *make_overlay(void)
{
    lv_obj_t *scrim = lv_obj_create(lv_layer_top());
    lv_obj_set_size(scrim, 800, 480);
    lv_obj_set_pos(scrim, 0, 0);
    lv_obj_set_style_bg_color(scrim, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scrim, LV_OPA_70, 0);
    lv_obj_set_style_border_width(scrim, 0, 0);
    lv_obj_set_style_radius(scrim, 0, 0);
    lv_obj_clear_flag(scrim, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scrim, LV_OBJ_FLAG_HIDDEN);
    return scrim;
}

static void build_data_page(lv_obj_t *parent)
{
    lv_obj_t *source = make_card(parent, "DATA SOURCE", 300, kContentH);
    lv_obj_set_pos(source, 8, 0);
    make_source_option(source, 1, "NMEA 2000",
                       "Live from the boat's backbone, as a standard NMEA 2000 node. Sends no instrument data.",
                       "Use NMEA 2000", on_use_n2k, kGreen, 30);
    make_source_option(source, 0, "Simulator",
                       "A boat sailing English Bay on a virtual bus. For the bench, never aboard.",
                       "Use simulator", on_use_sim, kAmber, 190);
    lv_obj_t *hint = lv_label_create(source);
    lv_label_set_text(hint, "Switching restarts the terminal. Phones reconnect on their own.");
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, 268);
    lv_obj_set_style_text_color(hint, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, 352);

    const lv_coord_t bus_x = 316;
    const lv_coord_t bus_w = kContentW - bus_x - 8;
    lv_obj_t *bus = make_card(parent, "BUS", bus_w, kContentH);
    lv_obj_set_pos(bus, bus_x, 0);
    g_bus_state = lv_label_create(bus);
    lv_label_set_text(g_bus_state, "-");
    lv_obj_set_style_text_font(g_bus_state, &lv_font_montserrat_28, 0);
    lv_obj_align(g_bus_state, LV_ALIGN_TOP_LEFT, 0, 26);
    g_bus_sub = lv_label_create(bus);
    lv_label_set_text(g_bus_sub, "");
    lv_obj_set_style_text_color(g_bus_sub, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(g_bus_sub, &lv_font_montserrat_14, 0);
    lv_obj_align(g_bus_sub, LV_ALIGN_TOP_LEFT, 0, 64);

    const lv_coord_t inner = bus_w - 32;
    const lv_coord_t gap = 8;
    const lv_coord_t mw = (inner - gap * 2) / 3;
    static const char *const kCaptions[kMetricCount] = {
        "Messages / s", "Frames / s", "Peak frames / s", "Lost frames", "Buffer peak",
        "Bus errors",   "PGN types",  "Devices",         "Chart apps",
    };
    for (int i = 0; i < kMetricCount; ++i) {
        const lv_coord_t x = (i % 3) * (mw + gap);
        const lv_coord_t y = 92 + (i / 3) * (72 + gap);
        g_metric[i] = make_metric(bus, kCaptions[i], x, y, mw);
    }

    g_bus_foot = lv_label_create(bus);
    lv_label_set_text(g_bus_foot, "");
    lv_label_set_long_mode(g_bus_foot, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_bus_foot, inner);
    lv_obj_set_style_text_color(g_bus_foot, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_font(g_bus_foot, &lv_font_montserrat_14, 0);
    lv_obj_align(g_bus_foot, LV_ALIGN_TOP_LEFT, 0, 342);

    /* Confirm before a restart. */
    g_confirm = make_overlay();
    lv_obj_add_event_cb(g_confirm, on_confirm_cancel, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *box = lv_obj_create(g_confirm);
    lv_obj_set_size(box, 500, 270);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_pad_all(box, 24, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    g_confirm_title = lv_label_create(box);
    lv_obj_set_style_text_color(g_confirm_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_confirm_title, &lv_font_montserrat_24, 0);
    lv_obj_align(g_confirm_title, LV_ALIGN_TOP_LEFT, 0, 0);
    g_confirm_body = lv_label_create(box);
    lv_label_set_long_mode(g_confirm_body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_confirm_body, 452);
    lv_obj_set_style_text_color(g_confirm_body, lv_color_hex(0xD5DEE8), 0);
    lv_obj_set_style_text_font(g_confirm_body, &lv_font_montserrat_16, 0);
    lv_obj_align(g_confirm_body, LV_ALIGN_TOP_LEFT, 0, 44);
    lv_obj_t *cancel = make_ghost_btn(box, "Cancel", on_confirm_cancel, 150);
    lv_obj_set_height(cancel, 48);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_t *go_btn = make_ghost_btn(box, "Switch and restart", on_confirm_go, 220);
    lv_obj_set_height(go_btn, 48);
    lv_obj_set_style_bg_color(go_btn, lv_color_hex(0x0E5C55), 0);
    lv_obj_align(go_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    g_restarting = make_overlay();
    lv_obj_set_style_bg_opa(g_restarting, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(g_restarting, lv_color_hex(0x071422), 0);
    g_restarting_lbl = lv_label_create(g_restarting);
    lv_obj_set_style_text_align(g_restarting_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_restarting_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_restarting_lbl, &lv_font_montserrat_28, 0);
    lv_obj_center(g_restarting_lbl);
}

/* ---- Network: the NMEA 2000 topology ----------------------------------------
 * The backbone as a line with a terminator at each end and every device on a
 * drop. CAN carries no position, so devices are in class order (the terminal
 * first) and each card shows the installer's location text when the device
 * has one. Tap a card for its identity, health, PGNs decoded and recent
 * messages. Same data as the phone's Bus check -> Network. */

static constexpr size_t kNetCards = 24;
static constexpr lv_coord_t kNetCardW = 168;
static constexpr lv_coord_t kNetCardH = 108;
/* Cards alternate above and below the line, so neighbours in one row are
 * two steps apart: 2 x 92 leaves 16 px between them. */
static constexpr lv_coord_t kNetStep = 92;
static constexpr lv_coord_t kNetLineY = 168;

static lv_obj_t *g_net_summary = nullptr;
static lv_obj_t *g_net_map = nullptr;
static lv_obj_t *g_net_backbone = nullptr;
static lv_obj_t *g_net_term[2] = {};
static lv_obj_t *g_net_card[kNetCards] = {};
static lv_obj_t *g_net_drop[kNetCards] = {};
static lv_obj_t *g_net_title[kNetCards] = {};
static lv_obj_t *g_net_role[kNetCards] = {};
static lv_obj_t *g_net_where[kNetCards] = {};
static lv_obj_t *g_net_state[kNetCards] = {};
static uint8_t g_net_addr[kNetCards] = {};
static size_t g_net_shown = 0;
static uint32_t g_net_ms = 0;

static lv_obj_t *g_net_detail = nullptr;
static lv_obj_t *g_net_d_title = nullptr;
static lv_obj_t *g_net_d_sub = nullptr;
static lv_obj_t *g_net_d_ident = nullptr;
static lv_obj_t *g_net_d_issues = nullptr;
static lv_obj_t *g_net_d_pgns = nullptr;
static int g_net_open = -1; /* address on the detail page, -1 closed */

static uint32_t net_border(const N2kNetView &v)
{
    if (v.self) {
        return 0x00B4A6;
    }
    if (v.issues & kNetSilent) {
        return 0x5C7080;
    }
    return v.issues != 0 ? kAmber : 0x1E3A5F;
}

static int issue_count(uint16_t issues)
{
    int n = 0;
    for (uint16_t b = issues; b != 0; b = static_cast<uint16_t>(b & (b - 1))) {
        ++n;
    }
    return n;
}

static void update_net_detail(void);

static void on_net_card(lv_event_t *e)
{
    const size_t i = reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));
    if (i >= g_net_shown || g_net_detail == nullptr) {
        return;
    }
    g_net_open = g_net_addr[i];
    update_net_detail();
    lv_obj_clear_flag(g_net_detail, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_net_detail);
}

static void on_net_back(lv_event_t * /*e*/)
{
    g_net_open = -1;
    if (g_net_detail != nullptr) {
        lv_obj_add_flag(g_net_detail, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_net_refresh(lv_event_t * /*e*/)
{
    n2k_network_refresh();
    set_label(g_net_summary, "Asking every device again...");
}

static lv_obj_t *net_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_coord_t w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (w > 0) {
        /* One line, cut with dots. LVGL 8 only cuts a label whose height is
         * fixed; left to grow it wraps instead. */
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, w);
        lv_obj_set_height(l, lv_font_get_line_height(font));
    }
    return l;
}

static void make_net_card(size_t i)
{
    lv_obj_t *drop = lv_obj_create(g_net_map);
    lv_obj_set_style_bg_color(drop, lv_color_hex(0x3A5A7C), 0);
    lv_obj_set_style_bg_opa(drop, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(drop, 0, 0);
    lv_obj_set_style_radius(drop, 0, 0);
    lv_obj_clear_flag(drop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(drop, LV_OBJ_FLAG_CLICKABLE);
    g_net_drop[i] = drop;

    lv_obj_t *card = lv_obj_create(g_net_map);
    lv_obj_set_size(card, kNetCardW, kNetCardH);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1B4064), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_add_event_cb(card, on_net_card, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<uintptr_t>(i)));
    g_net_card[i] = card;

    const lv_coord_t inner = kNetCardW - 24;
    g_net_title[i] = net_label(card, &lv_font_montserrat_16, 0xFFFFFF, inner);
    lv_obj_align(g_net_title[i], LV_ALIGN_TOP_LEFT, 0, 0);
    g_net_role[i] = net_label(card, &lv_font_montserrat_12, kMuted, inner);
    lv_obj_align(g_net_role[i], LV_ALIGN_TOP_LEFT, 0, 26);
    g_net_where[i] = net_label(card, &lv_font_montserrat_12, 0xD5DEE8, inner);
    lv_obj_align(g_net_where[i], LV_ALIGN_TOP_LEFT, 0, 46);
    g_net_state[i] = net_label(card, &lv_font_montserrat_14, kGreen, inner);
    lv_obj_align(g_net_state[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void update_network_view(void)
{
    if (g_screen != Screen::Network || g_net_map == nullptr) {
        return;
    }
    const uint32_t now = millis();
    if (g_net_ms != 0 && now - g_net_ms < 1000) {
        return;
    }
    g_net_ms = now;
    static N2kNetView v[kNetCards];
    const size_t n = n2k_network_view(v, kNetCards);

    uint32_t len = 0;
    int problems = 0;
    for (size_t i = 0; i < n; ++i) {
        len += v[i].load_equivalency != 0xff ? v[i].load_equivalency : 0;
        problems += v[i].issues != 0 ? 1 : 0;
    }
    char line[160];
    if (n == 0) {
        snprintf(line, sizeof(line), "No devices yet.");
    } else {
        snprintf(line, sizeof(line), "%u devices   " LV_SYMBOL_BULLET "   bus load %lu LEN (%.2f A)   " LV_SYMBOL_BULLET "   %s",
                 static_cast<unsigned>(n), static_cast<unsigned long>(len), len * 0.05,
                 problems == 0 ? "no problems" : (problems == 1 ? "1 device needs a look" : "devices need a look"));
    }
    set_label(g_net_summary, line);

    const lv_coord_t width = 16 + static_cast<lv_coord_t>(n > 0 ? n - 1 : 0) * kNetStep + kNetCardW + 16;
    const lv_coord_t span = width > kContentW ? width : kContentW;
    lv_obj_set_size(g_net_backbone, span - 40, 6);
    lv_obj_set_pos(g_net_backbone, 20, kNetLineY);
    lv_obj_set_pos(g_net_term[0], 8, kNetLineY - 9);
    lv_obj_set_pos(g_net_term[1], span - 32, kNetLineY - 9);

    for (size_t i = 0; i < kNetCards; ++i) {
        if (i >= n) {
            if (g_net_card[i] != nullptr) {
                lv_obj_add_flag(g_net_card[i], LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(g_net_drop[i], LV_OBJ_FLAG_HIDDEN);
            }
            continue;
        }
        if (g_net_card[i] == nullptr) {
            make_net_card(i);
        }
        const N2kNetView &d = v[i];
        g_net_addr[i] = d.address;
        const bool above = (i % 2) == 0;
        const lv_coord_t x = 16 + static_cast<lv_coord_t>(i) * kNetStep;
        const lv_coord_t y = above ? kNetLineY - 24 - kNetCardH : kNetLineY + 30;
        lv_obj_set_pos(g_net_card[i], x, y);
        lv_obj_set_size(g_net_drop[i], 3, 24);
        lv_obj_set_pos(g_net_drop[i], x + kNetCardW / 2, above ? kNetLineY - 24 : kNetLineY + 6);
        lv_obj_set_style_border_color(g_net_card[i], lv_color_hex(net_border(d)), 0);
        set_label(g_net_title[i], d.title);
        set_label(g_net_role[i], d.role[0] != '\0' ? d.role : "Unknown role");
        set_label(g_net_where[i], d.install[0] != '\0' ? d.install : "");
        char state[64];
        char addr[16];
        if (d.address < 252) {
            snprintf(addr, sizeof(addr), "addr %u", static_cast<unsigned>(d.address));
        } else {
            snprintf(addr, sizeof(addr), "no addr");
        }
        const int k = issue_count(d.issues);
        const char *len_s = d.load_equivalency == 0xff ? "LEN ?" : "";
        char len_b[16];
        if (d.load_equivalency != 0xff) {
            snprintf(len_b, sizeof(len_b), "%u LEN", static_cast<unsigned>(d.load_equivalency));
            len_s = len_b;
        }
        if (d.issues & kNetSilent) {
            snprintf(state, sizeof(state), "Silent " LV_SYMBOL_BULLET " %s " LV_SYMBOL_BULLET " %s", len_s, addr);
        } else if (k > 0) {
            snprintf(state, sizeof(state), "%d %s " LV_SYMBOL_BULLET " %s " LV_SYMBOL_BULLET " %s", k,
                     k == 1 ? "issue" : "issues", len_s, addr);
        } else {
            snprintf(state, sizeof(state), "OK " LV_SYMBOL_BULLET " %s " LV_SYMBOL_BULLET " %s", len_s, addr);
        }
        set_label(g_net_state[i], state);
        lv_obj_set_style_text_color(g_net_state[i],
                                    lv_color_hex((d.issues & kNetSilent) ? kMuted : (k > 0 ? kAmber : kGreen)), 0);
        lv_obj_clear_flag(g_net_card[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_net_drop[i], LV_OBJ_FLAG_HIDDEN);
    }
    g_net_shown = n;
    if (g_net_open >= 0) {
        update_net_detail();
    }
}

static void update_net_detail(void)
{
    if (g_net_detail == nullptr || g_net_open < 0) {
        return;
    }
    static N2kNetInfo info;
    static N2kPgnLine pgns[24];
    static N2kPgnLine recent[10];
    size_t np = 0, nr = 0;
    if (!n2k_device_details(static_cast<uint8_t>(g_net_open), &info, pgns, 24, &np, recent, 10, &nr)) {
        set_label(g_net_d_title, "Gone");
        set_label(g_net_d_sub, "This device is no longer on the bus.");
        return;
    }
    const N2kNetView &d = info.view;
    set_label(g_net_d_title, d.title);
    char sub[96];
    snprintf(sub, sizeof(sub), "%s   " LV_SYMBOL_BULLET "   address %u%s%s", d.role[0] ? d.role : "Unknown",
             static_cast<unsigned>(d.address), info.maker[0] ? "   " LV_SYMBOL_BULLET "   " : "", info.maker);
    set_label(g_net_d_sub, sub);

    /* What the terminal takes from it: owners live in loop(), so read here. */
    N2kSourceLock locks[16];
    const size_t nl = n2k_conv_sources(locks, 16);
    char owns[96] = "";
    for (size_t i = 0; i < nl; ++i) {
        if (locks[i].src == d.address) {
            const size_t used = strlen(owns);
            snprintf(owns + used, sizeof(owns) - used, "%s%s", used ? ", " : "", locks[i].what);
        }
    }

    static char id[900];
    size_t u = 0;
    auto add = [&](const char *fmt, auto... args) {
        if (u < sizeof(id)) {
            u += static_cast<size_t>(snprintf(id + u, sizeof(id) - u, fmt, args...));
        }
    };
    if (d.install[0]) {
        add("Installed: %s\n", d.install);
    }
    if (info.install2[0]) {
        add("           %s\n", info.install2);
    }
    if (info.named) {
        add("Maker code %u, class %u, function %u\n", static_cast<unsigned>(info.manufacturer),
            static_cast<unsigned>(d.device_class), static_cast<unsigned>(d.function));
        add("Instance %u, system %u, unique %lu\n", static_cast<unsigned>(info.instance),
            static_cast<unsigned>(info.system_instance), static_cast<unsigned long>(info.unique));
    } else {
        add("No address claim heard yet\n");
    }
    if (info.have_product) {
        add("Model %s\n", info.model);
        add("Software %s\n", info.software);
        if (info.model_version[0]) {
            add("Version %s\n", info.model_version);
        }
        if (info.serial[0]) {
            add("Serial %s\n", info.serial);
        }
        if (d.load_equivalency != 0xff) {
            add("Load %u LEN (%u mA)\n", static_cast<unsigned>(d.load_equivalency),
                static_cast<unsigned>(d.load_equivalency) * 50u);
        }
        if (!d.self) {
            add("NMEA 2000 version %u.%03u, certification level %u\n", static_cast<unsigned>(info.n2k_version / 1000),
                static_cast<unsigned>(info.n2k_version % 1000), static_cast<unsigned>(info.certification));
        }
    } else if (!d.self) {
        add("Product information not received\n");
    }
    if (info.heartbeat_ms != 0) {
        add("Heartbeat every %lu s, last %lu s ago\n", static_cast<unsigned long>(info.heartbeat_ms / 1000),
            static_cast<unsigned long>(info.heartbeat_age_ms / 1000));
    }
    if (info.transmits != 0xff) {
        add("Transmit list: %u PGNs\n", static_cast<unsigned>(info.transmits));
    }
    if (info.address_changes > 0) {
        add("Moved address %u times, last from %u\n", static_cast<unsigned>(info.address_changes),
            static_cast<unsigned>(info.previous_address));
    }
    add("Terminal uses it for: %s", owns[0] ? owns : "nothing");
    set_label(g_net_d_ident, id);
    lv_obj_update_layout(g_net_d_ident);
    lv_obj_align_to(g_net_d_issues, g_net_d_ident, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    static char is[600];
    size_t iu = 0;
    is[0] = '\0';
    for (uint16_t bit = 1; bit != 0 && bit <= kNetSilent; bit = static_cast<uint16_t>(bit << 1)) {
        if (d.issues & bit) {
            iu += static_cast<size_t>(snprintf(is + iu, sizeof(is) - iu, "%s%s", iu ? "\n" : "",
                                               n2k_net_issue_text(static_cast<N2kNetIssue>(bit))));
        }
    }
    set_label(g_net_d_issues, is);

    static char list[2600];
    size_t lu = 0;
    auto addl = [&](const char *fmt, auto... args) {
        if (lu < sizeof(list)) {
            lu += static_cast<size_t>(snprintf(list + lu, sizeof(list) - lu, fmt, args...));
        }
    };
    addl("PGNS IT SENDS\n");
    for (size_t i = 0; i < np; ++i) {
        addl("%6lu  %-15s %3u.%u/s   %s\n", static_cast<unsigned long>(pgns[i].pgn), pgns[i].name,
             static_cast<unsigned>(pgns[i].rate_x10 / 10), static_cast<unsigned>(pgns[i].rate_x10 % 10), pgns[i].text);
    }
    if (np == 0) {
        addl(d.self ? "This terminal sends network management only.\n" : "Nothing heard from it yet.\n");
    }
    addl("\nRECENT MESSAGES\n");
    for (size_t i = 0; i < nr; ++i) {
        addl("%5.1f s  %6lu  %s\n", recent[i].age_ms / 1000.0, static_cast<unsigned long>(recent[i].pgn), recent[i].text);
    }
    set_label(g_net_d_pgns, list);
}

static void build_network_page(lv_obj_t *parent)
{
    lv_obj_t *head = make_card(parent, "NETWORK", kContentW - 16, 64);
    lv_obj_set_pos(head, 8, 0);
    g_net_summary = net_label(head, &lv_font_montserrat_16, 0xFFFFFF, kContentW - 200);
    lv_obj_align(g_net_summary, LV_ALIGN_TOP_LEFT, 0, 22);
    lv_obj_t *refresh = make_ghost_btn(head, "Refresh", on_net_refresh, 120);
    lv_obj_align(refresh, LV_ALIGN_TOP_RIGHT, 0, 4);

    g_net_map = lv_obj_create(parent);
    lv_obj_set_size(g_net_map, kContentW, kContentH - 72);
    lv_obj_set_pos(g_net_map, 0, 72);
    lv_obj_set_style_bg_opa(g_net_map, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_net_map, 0, 0);
    lv_obj_set_style_pad_all(g_net_map, 0, 0);
    lv_obj_set_scroll_dir(g_net_map, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(g_net_map, LV_SCROLLBAR_MODE_ACTIVE);

    g_net_backbone = lv_obj_create(g_net_map);
    lv_obj_set_style_bg_color(g_net_backbone, lv_color_hex(0x3A5A7C), 0);
    lv_obj_set_style_bg_opa(g_net_backbone, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_net_backbone, 0, 0);
    lv_obj_set_style_radius(g_net_backbone, 3, 0);
    lv_obj_clear_flag(g_net_backbone, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *t = lv_obj_create(g_net_map);
        lv_obj_set_size(t, 24, 24);
        lv_obj_set_style_bg_color(t, lv_color_hex(0x5C7080), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_radius(t, 4, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *l = lv_label_create(t);
        lv_label_set_text(l, "T");
        lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_center(l);
        g_net_term[i] = t;
    }

    /* Node detail, over the map. */
    g_net_detail = lv_obj_create(parent);
    lv_obj_set_size(g_net_detail, kContentW, kContentH);
    lv_obj_set_pos(g_net_detail, 0, 0);
    lv_obj_set_style_bg_color(g_net_detail, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(g_net_detail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_net_detail, 0, 0);
    lv_obj_set_style_radius(g_net_detail, 0, 0);
    lv_obj_set_style_pad_all(g_net_detail, 12, 0);
    lv_obj_clear_flag(g_net_detail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_net_detail, LV_OBJ_FLAG_HIDDEN);
    g_net_d_title = net_label(g_net_detail, &lv_font_montserrat_24, 0xFFFFFF, kContentW - 180);
    lv_obj_align(g_net_d_title, LV_ALIGN_TOP_LEFT, 0, 0);
    g_net_d_sub = net_label(g_net_detail, &lv_font_montserrat_14, kMuted, kContentW - 180);
    lv_obj_align(g_net_d_sub, LV_ALIGN_TOP_LEFT, 0, 32);
    lv_obj_t *back = make_ghost_btn(g_net_detail, "Back to map", on_net_back, 150);
    lv_obj_set_height(back, 40);
    lv_obj_align(back, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_t *left = make_card(g_net_detail, "IDENTITY AND HEALTH", 300, kContentH - 84);
    lv_obj_set_pos(left, 0, 60);
    lv_obj_add_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    g_net_d_ident = lv_label_create(left);
    lv_label_set_long_mode(g_net_d_ident, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_net_d_ident, 268);
    lv_obj_set_style_text_font(g_net_d_ident, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_net_d_ident, lv_color_hex(0xD5DEE8), 0);
    lv_obj_align(g_net_d_ident, LV_ALIGN_TOP_LEFT, 0, 24);
    g_net_d_issues = lv_label_create(left);
    lv_label_set_long_mode(g_net_d_issues, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_net_d_issues, 268);
    lv_obj_set_style_text_font(g_net_d_issues, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(g_net_d_issues, lv_color_hex(kAmber), 0);
    lv_label_set_text(g_net_d_issues, "");

    lv_obj_t *right = make_card(g_net_detail, "TRAFFIC", kContentW - 24 - 308, kContentH - 84);
    lv_obj_set_pos(right, 308, 60);
    lv_obj_add_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(right, LV_DIR_VER);
    g_net_d_pgns = lv_label_create(right);
    lv_label_set_long_mode(g_net_d_pgns, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_net_d_pgns, kContentW - 24 - 308 - 32);
    lv_obj_set_style_text_font(g_net_d_pgns, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_net_d_pgns, lv_color_hex(0xD5DEE8), 0);
    lv_obj_align(g_net_d_pgns, LV_ALIGN_TOP_LEFT, 0, 24);
}

static void build_mode_badge(lv_obj_t *bar)
{
    g_mode_badge = lv_obj_create(bar);
    lv_obj_set_size(g_mode_badge, 244, 40);
    lv_obj_set_pos(g_mode_badge, 152, 8);
    lv_obj_set_style_bg_color(g_mode_badge, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_opa(g_mode_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_mode_badge, 0, 0);
    lv_obj_set_style_radius(g_mode_badge, 20, 0);
    lv_obj_set_style_pad_all(g_mode_badge, 0, 0);
    lv_obj_clear_flag(g_mode_badge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_mode_badge, on_mode_badge, LV_EVENT_CLICKED, nullptr);

    g_mode_dot = lv_obj_create(g_mode_badge);
    lv_obj_set_size(g_mode_dot, 12, 12);
    lv_obj_align(g_mode_dot, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_style_radius(g_mode_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(g_mode_dot, 0, 0);
    lv_obj_set_style_bg_opa(g_mode_dot, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_mode_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(g_mode_dot, LV_OBJ_FLAG_SCROLLABLE);

    g_mode_lbl = lv_label_create(g_mode_badge);
    lv_label_set_text(g_mode_lbl, "");
    lv_obj_set_style_text_color(g_mode_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_mode_lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(g_mode_lbl, LV_ALIGN_LEFT_MID, 38, 0);
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
              &g_tile_depth_sub);
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
    g_link_chip = make_icon_chip(parent, 0x6B5420, LV_SYMBOL_WIFI, on_helm_link);
    lv_obj_t *link_btn = g_link_chip;
    lv_obj_set_pos(link_btn, link_x, icon_y);
    lv_obj_set_style_bg_color(link_btn, lv_color_hex(0x1B4064), LV_STATE_PRESSED);

    g_helm_talk = make_helm_sub(parent, "NMEA");
    g_helm_n2k_status = helm_line(g_helm_talk, "SIM - CAN off", 0xFFFFFF, 52,
                                  &lv_font_montserrat_16);
    helm_line(g_helm_talk, "PGN    type    src    age    n", 0x8FA3B8, 80,
              &lv_font_montserrat_14);
    g_helm_n2k = helm_line(g_helm_talk, "No N2K messages", 0xE8F0F7, 104,
                           &lv_font_montserrat_16);
    helm_line(g_helm_talk, "0183    sentence    age    n", 0x8FA3B8, 286,
              &lv_font_montserrat_14);
    g_talker_list = helm_line(g_helm_talk, "Waiting for sentences...", 0xE8F0F7, 310,
                              &lv_font_montserrat_16);

    g_helm_link = make_helm_sub(parent, "Link");
    g_helm_nmea = helm_line(g_helm_link, "Bluetooth  -", 0xFFFFFF, 64, &lv_font_montserrat_20);
    g_helm_tcp = helm_line(g_helm_link, "Hotspot  starting", 0xFFFFFF, 112, &lv_font_montserrat_20);
    g_helm_ip = helm_line(g_helm_link, "Phones on it  0", 0xE6C35C, 160, &lv_font_montserrat_20);
    g_helm_ap = helm_line(g_helm_link, "NMEA listeners  0", 0xFFFFFF, 208, &lv_font_montserrat_20);
    g_helm_peer = helm_line(g_helm_link, "Uplink  none", 0x8FA3B8, 256, &lv_font_montserrat_20);
    helm_line(g_helm_link,
              "Uplink is this terminal's own network. Phones on it is the boat hotspot.",
              0x8FA3B8, 320, &lv_font_montserrat_16);
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
#if LMYC_WITH_REPORTS_CHARTS
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
#endif
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

#if LMYC_WITH_REPORTS_CHARTS
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
#endif

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
        }
#if LMYC_WITH_REPORTS_CHARTS
        else if (g_screen == Screen::Reports) {
            maint_ui_next_report();
        }
#endif
    }
    lv_indev_wait_release(indev);
}

#if LMYC_WITH_REPORTS_CHARTS
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
#endif

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
    bool problem = false;
    if (journey_stop(&problem) == Stop::Find) {
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

#if LMYC_WITH_REPORTS_CHARTS
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
#endif

    g_page_diag = lv_obj_create(scr);
    place_content(g_page_diag);
    lv_obj_set_style_bg_opa(g_page_diag, LV_OPA_TRANSP, 0);
    build_diag_page(g_page_diag);
    lv_obj_add_flag(g_page_diag, LV_OBJ_FLAG_HIDDEN);

    g_page_data = lv_obj_create(scr);
    place_content(g_page_data);
    lv_obj_set_style_bg_opa(g_page_data, LV_OPA_TRANSP, 0);
    build_data_page(g_page_data);
    lv_obj_add_event_cb(g_page_data, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(g_page_data, LV_OBJ_FLAG_HIDDEN);

    g_page_net = lv_obj_create(scr);
    place_content(g_page_net);
    lv_obj_set_style_bg_opa(g_page_net, LV_OPA_TRANSP, 0);
    build_network_page(g_page_net);
    lv_obj_add_event_cb(g_page_net, on_dismiss_rail, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(g_page_net, LV_OBJ_FLAG_HIDDEN);

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
    /* Tinted with the data source: amber simulated, green live, red no data. */
    g_top_rule = lv_obj_create(g_topbar);
    lv_obj_set_size(g_top_rule, 800, 2);
    lv_obj_align(g_top_rule, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(g_top_rule, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_bg_opa(g_top_rule, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_top_rule, 0, 0);
    lv_obj_set_style_radius(g_top_rule, 0, 0);
    lv_obj_clear_flag(g_top_rule, LV_OBJ_FLAG_SCROLLABLE);

    g_chip_fix = make_icon_chip(g_topbar, 0x6B5420, LV_SYMBOL_GPS, on_fix_chip);
    lv_obj_set_pos(g_chip_fix, 8, 8);
    g_chip_link = make_icon_chip(g_topbar, 0x1E3A5F, LV_SYMBOL_BLUETOOTH, on_link_chip);
    lv_obj_set_pos(g_chip_link, 56, 8);
    g_chip_qr = make_icon_chip(g_topbar, 0x0E5C55, "QR", on_qr_chip);
    lv_obj_set_pos(g_chip_qr, 104, 8);
    build_mode_badge(g_topbar);

    g_crumb_home = make_crumb_btn(g_topbar, "Home", on_show_home, kCrumbHomeX, kCrumbHomeW,
                                  &lv_font_montserrat_16);
    g_crumb_sep1 = crumb_sep(g_topbar, 500);
#if LMYC_WITH_REPORTS_CHARTS
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
#endif
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
#if LMYC_WITH_REPORTS_CHARTS
    /* Five on the full build: shorter buttons so all fit in 424 px. */
    make_rail_nav(g_rail_home, "Charts", "Last picture", 0x00B4A6, on_show_chart, 8, 76);
    make_rail_nav(g_rail_home, "Reports", "Open items", 0x00B4A6, on_show_book, 90, 76);
    make_rail_nav(g_rail_home, "Instruments", "Depth and wind", 0x00B4A6, on_show_helm, 172, 76);
    make_rail_nav(g_rail_home, "Network", "Devices on the bus", 0x00B4A6, on_show_net, 254, 76);
    make_rail_nav(g_rail_home, "Diagnostics", "Radio and card", 0x5C7080, on_show_diag, 336, 76);
#else
    make_rail_nav(g_rail_home, "Instruments", "Depth and wind", 0x00B4A6, on_show_helm, 8);
    make_rail_nav(g_rail_home, "Network", "Devices on the bus", 0x00B4A6, on_show_net, 112);
    make_rail_nav(g_rail_home, "Diagnostics", "Radio and Wi-Fi", 0x5C7080, on_show_diag, 216);
#endif

    g_rail_instr = rail_group(g_rail);
    lv_obj_t *gnss_btn = rail_action(g_rail_instr, "GNSS off", on_toggle_gnss, 8);
    g_gnss_btn_lbl = lv_obj_get_child(gnss_btn, 0);
    lv_obj_add_flag(g_rail_instr, LV_OBJ_FLAG_HIDDEN);

#if LMYC_WITH_REPORTS_CHARTS
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
#endif

    g_rail_diag = rail_group(g_rail);
    rail_action(g_rail_diag, "Reset BLE", on_reset_ble, 8);
    rail_action(g_rail_diag, "Data source", on_show_data, 60);
    lv_obj_add_flag(g_rail_diag, LV_OBJ_FLAG_HIDDEN);

    g_rail_data = rail_group(g_rail);
    rail_action(g_rail_data, "Diagnostics", on_show_diag, 8);
    rail_action(g_rail_data, "Network", on_show_net, 60);
    lv_obj_add_flag(g_rail_data, LV_OBJ_FLAG_HIDDEN);

    g_rail_net = rail_group(g_rail);
    rail_action(g_rail_net, "Refresh", on_net_refresh, 8);
    rail_action(g_rail_net, "Data source", on_show_data, 60);
    rail_action(g_rail_net, "Diagnostics", on_show_diag, 112);
    lv_obj_add_flag(g_rail_net, LV_OBJ_FLAG_HIDDEN);

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

void setup()
{
    Serial.begin(115200);
    delay(200);
    g_log.println();
    g_log.println("=== LMYC Boat Terminal ===");

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
        g_log.println("ERROR: board->begin() failed");
        while (true) {
            delay(1000);
        }
    }
    /* The board init drives every CH422G pin high, and EXIO5 high wires
     * GPIO19/20 to the CAN transceiver. Put USB back until n2k_init decides. */
    select_can(false);

    lvgl_port_init(g_board->getLCD(), g_board->getTouch());

    // The QR is painted once from the lmyc:// link, so the link has to exist
    // first. The radio itself still starts after the page is drawn.
    pairing_prepare();
    lvgl_port_lock(-1);
    create_ui();
    lvgl_port_unlock();

#if LMYC_WITH_REPORTS_CHARTS
    {
        auto *expander_dev = g_board->getIO_Expander();
        sd_card_init(expander_dev != nullptr ? expander_dev->getBase() : nullptr);
    }
#endif
    nmea_init();
    nmea_state_init();
    nmea_io_init();
    inject_init();
#if LMYC_WITH_REPORTS_CHARTS
    snapshot_init();
#endif
    gateway_init();
    hub_http_init();
    nmea_set_sink(on_nmea_sentence, nullptr);
    /* Before BLE, Wi-Fi or HTTP start their tasks. On NMEA 2000 this ends
     * USB Serial, which must not happen while another task is printing. */
    n2k_init(select_can);
    pairing_init();
    net_wifi_start();

    auto boot_log = [](const char *msg) {
        if (n2k_using_uart()) {
            Serial0.println(msg);
        } else {
            g_log.println(msg);
        }
    };
    boot_log("UI ready. BLE name is in the primary ADV packet; scan in the LMYC app.");
    if (n2k_source() == DataSource::Nmea2000) {
        boot_log(n2k_using_uart()
                     ? "NMEA source N2K: NMEA 2000 node. Serial log is on the UART Type-C socket."
                     : "NMEA source N2K selected, but CAN did not open. USB serial stays up.");
    } else {
        boot_log("NMEA source simulator (FIX: SIM). Change it on Data source, or NMEA N2K on the console.");
    }
}

void loop()
{
    lvgl_port_handle();
#if LMYC_WITH_REPORTS_CHARTS
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
#endif

    net_wifi_poll();

    nmea_io_poll();
    inject_poll();
    n2k_poll();
    gateway_poll();

    static uint32_t stats_ms = 0;
    if (millis() - stats_ms >= 1000) {
        stats_ms = millis();
        terminal_stats_refresh();
    }

    delay(5);
}
