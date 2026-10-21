#include "boat_config.h"
#if LMYC_WITH_REPORTS_CHARTS

#include "maint_ui.h"

#include "maint_store.h"
#include "snapshot.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <cstring>

static constexpr int kRows = 6;
static constexpr int kDescLines = 4;

static MaintItem g_items[kRows];
static int g_count = 0;
static int g_selected = 0;
static uint8_t g_photo_n = 1;
static bool g_photo_pending = false;
static bool g_decoded_ok = false;
static bool g_open_on_note = false;
static char g_photo_line[80] = "";
static char g_caption[48] = "";
static char g_note_line[kMaintPhotoNoteMax + 1] = "";
static char g_book_subtitle[72] = "Open maintenance  ·  tap a row";

static bool g_thumb_pending = false;
static uint8_t g_thumb_at = 1;
static bool g_thumb_ok = false;
static int g_thumb_w = 128;
static int g_thumb_h = 96;
static uint16_t *g_thumb_px[kMaintPhotoMax] = {};
static lv_img_dsc_t g_thumb_dsc[kMaintPhotoMax] = {};

static lv_obj_t *g_list = nullptr;
static lv_obj_t *g_rows[kRows] = {};
static lv_obj_t *g_row_title[kRows] = {};
static lv_obj_t *g_row_cat[kRows] = {};
static lv_obj_t *g_row_pri[kRows] = {};
static lv_obj_t *g_row_pri_lbl[kRows] = {};
static lv_obj_t *g_row_photo[kRows] = {};
static bool g_items_fresh = false;
static uint32_t g_items_gen = 0;
static lv_obj_t *g_empty = nullptr;
static lv_obj_t *g_http = nullptr;
static lv_obj_t *g_http_log = nullptr;
static bool g_debug = false;

static lv_coord_t g_inner = 300;
static lv_coord_t g_desc_text_w = 280;

static lv_obj_t *g_detail_page = nullptr;
static lv_obj_t *g_detail_scroll = nullptr;
static lv_obj_t *g_title = nullptr;
static lv_obj_t *g_val_cat = nullptr;
static lv_obj_t *g_val_status = nullptr;
static lv_obj_t *g_val_reporter = nullptr;
static lv_obj_t *g_pri_chip = nullptr;
static lv_obj_t *g_pri_lbl = nullptr;
static lv_obj_t *g_desc_well = nullptr;
static lv_obj_t *g_desc = nullptr;
static lv_obj_t *g_desc_more = nullptr;
static lv_obj_t *g_reader = nullptr;
static lv_obj_t *g_reader_scroll = nullptr;
static lv_obj_t *g_reader_title = nullptr;
static lv_obj_t *g_reader_body = nullptr;
static lv_obj_t *g_photos_hd = nullptr;
static lv_obj_t *g_no_photos = nullptr;
static lv_obj_t *g_status = nullptr;
static lv_obj_t *g_thumb_row[kMaintPhotoMax] = {};
static lv_obj_t *g_thumb_img[kMaintPhotoMax] = {};
static lv_obj_t *g_thumb_note[kMaintPhotoMax] = {};

static void (*g_on_photo)(void) = nullptr;
static void (*g_on_subtitle)(const char *) = nullptr;
static void (*g_on_dismiss)(void) = nullptr;

static void dismiss_rail(void)
{
    if (g_on_dismiss != nullptr) {
        g_on_dismiss();
    }
}

static void on_background(lv_event_t * /*e*/)
{
    dismiss_rail();
}

static bool is_open(const MaintItem *item)
{
    return strcmp(item->status, "completed") != 0 &&
           strcmp(item->status, "future-wishlist") != 0;
}

static void style_btn(lv_obj_t *btn, lv_coord_t w, lv_coord_t h)
{
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
}

static void set_clickable(lv_obj_t *obj, bool on)
{
    if (obj == nullptr) {
        return;
    }
    if (on) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void visible_scrollbar(lv_obj_t *obj)
{
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_width(obj, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x8FA3B8), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(obj, 3, LV_PART_SCROLLBAR);
}

/* "safety-equipment" is shown as "Safety equipment". An empty field is an em dash. */
static void pretty_token(const char *in, char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return;
    }
    if (in == nullptr || in[0] == '\0') {
        snprintf(out, cap, "—");
        return;
    }
    size_t j = 0;
    for (size_t i = 0; in[i] != '\0' && j + 1 < cap; ++i) {
        char c = in[i] == '-' ? ' ' : in[i];
        if (i == 0 && c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        out[j++] = c;
    }
    out[j] = '\0';
}

static uint32_t priority_bg(const char *priority)
{
    if (priority != nullptr &&
        (strcmp(priority, "critical") == 0 || strcmp(priority, "high") == 0)) {
        return 0xF0C674;
    }
    if (priority != nullptr && strcmp(priority, "medium") == 0) {
        return 0x00B4A6;
    }
    return 0x5C7080;
}

static uint32_t priority_fg(const char *priority)
{
    if (priority != nullptr &&
        (strcmp(priority, "critical") == 0 || strcmp(priority, "high") == 0 ||
         strcmp(priority, "medium") == 0)) {
        return 0x1A1408;
    }
    return 0xFFFFFF;
}

static void paint_priority(lv_obj_t *chip, lv_obj_t *lbl, const char *priority)
{
    char word[24];
    pretty_token(priority, word, sizeof(word));
    if (lbl != nullptr) {
        lv_label_set_text(lbl, word);
        lv_obj_set_style_text_color(lbl, lv_color_hex(priority_fg(priority)), 0);
    }
    if (chip != nullptr) {
        lv_obj_set_style_bg_color(chip, lv_color_hex(priority_bg(priority)), 0);
    }
}

static void hide_reader(void)
{
    if (g_reader != nullptr) {
        lv_obj_add_flag(g_reader, LV_OBJ_FLAG_HIDDEN);
    }
}

static void sync_debug_log(void)
{
    if (g_http_log == nullptr) {
        return;
    }
    const bool detail = g_detail_page != nullptr &&
                        !lv_obj_has_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN);
    if (g_debug && !detail) {
        lv_obj_clear_flag(g_http_log, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_http_log);
    } else {
        lv_obj_add_flag(g_http_log, LV_OBJ_FLAG_HIDDEN);
    }
}

static void publish_book_subtitle(const char *text)
{
    snprintf(g_book_subtitle, sizeof(g_book_subtitle), "%s", text != nullptr ? text : "");
    if (g_on_subtitle != nullptr) {
        g_on_subtitle(g_book_subtitle);
    }
}

const char *maint_ui_caption(void) { return g_caption; }
const char *maint_ui_photo_note(void) { return g_note_line; }
bool maint_ui_open_on_note(void) { return g_open_on_note; }
const char *maint_ui_book_subtitle(void) { return g_book_subtitle; }

void maint_ui_set_http(const char *last, const char *log)
{
    if (g_http != nullptr && last != nullptr &&
        strcmp(lv_label_get_text(g_http), last) != 0) {
        lv_label_set_text(g_http, last);
    }
    if (g_http_log != nullptr && log != nullptr &&
        strcmp(lv_label_get_text(g_http_log), log) != 0) {
        lv_label_set_text(g_http_log, log);
    }
}

void maint_ui_toggle_debug(void)
{
    g_debug = !g_debug;
    sync_debug_log();
}

static void set_status(const char *text)
{
    if (g_status != nullptr) {
        lv_label_set_text(g_status, text != nullptr ? text : "");
    }
}

static void show_list(void)
{
    g_photo_pending = false;
    g_thumb_pending = false;
    if (g_list != nullptr) {
        lv_obj_clear_flag(g_list, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_detail_page != nullptr) {
        lv_obj_add_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN);
    }
    hide_reader();
    publish_book_subtitle("Open maintenance  ·  tap a row");
    sync_debug_log();
}

bool maint_ui_showing_detail(void)
{
    return g_detail_page != nullptr && !lv_obj_has_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN);
}

const char *maint_ui_item_title(void)
{
    if (g_selected < 0 || g_selected >= g_count) {
        return "";
    }
    return g_items[g_selected].title;
}

void maint_ui_show_list(void)
{
    show_list();
}

static uint8_t *read_jpeg(const MaintItem *item, uint8_t n, size_t *out_len)
{
    *out_len = 0;
    char path[56];
    if (!maint_store_photo_path(item->id, n, path, sizeof(path))) {
        return nullptr;
    }
    File in = SD.open(path, FILE_READ);
    if (!in) {
        return nullptr;
    }
    const size_t len = in.size();
    uint8_t *buf = nullptr;
    if (len >= 32 && len <= kMaintPhotoBytesMax) {
        buf = static_cast<uint8_t *>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buf == nullptr) {
            buf = static_cast<uint8_t *>(malloc(len));
        }
    }
    if (buf == nullptr) {
        in.close();
        return nullptr;
    }
    const size_t got = in.read(buf, len);
    in.close();
    if (got != len) {
        free(buf);
        return nullptr;
    }
    *out_len = len;
    return buf;
}

static void queue_photo(void)
{
    g_photo_pending = true;
    set_status("Opening photo…");
}

bool maint_ui_photo_pending(void) { return g_photo_pending; }

void maint_ui_decode_queued_photo(void)
{
    g_photo_pending = false;
    g_decoded_ok = false;
    g_note_line[0] = '\0';
    snprintf(g_photo_line, sizeof(g_photo_line), "Could not show the photo");
    if (g_selected < 0 || g_selected >= g_count || g_items[g_selected].photo_count == 0) {
        snprintf(g_photo_line, sizeof(g_photo_line), "No photo on this item");
        return;
    }

    const MaintItem *item = &g_items[g_selected];
    if (g_photo_n < 1 || g_photo_n > item->photo_count) {
        g_photo_n = 1;
    }
    size_t len = 0;
    uint8_t *buf = read_jpeg(item, g_photo_n, &len);
    if (buf == nullptr) {
        snprintf(g_photo_line, sizeof(g_photo_line), "Photo file missing");
        return;
    }
    const bool ok = snapshot_load_jpeg_limited(buf, len, kMaintPhotoBytesMax);
    free(buf);
    if (!ok || snapshot_dsc() == nullptr) {
        snprintf(g_photo_line, sizeof(g_photo_line), "Could not show the photo");
        return;
    }
    snprintf(g_caption, sizeof(g_caption), "Photo %u of %u",
             static_cast<unsigned>(g_photo_n),
             static_cast<unsigned>(item->photo_count));
    snprintf(g_note_line, sizeof(g_note_line), "%s", item->photo_note[g_photo_n - 1]);
    g_decoded_ok = true;
}

void maint_ui_publish_queued_photo(void)
{
    if (!g_decoded_ok) {
        set_status(g_photo_line);
        return;
    }
    set_status("");
    if (g_on_photo != nullptr) {
        g_on_photo();
    }
}

bool maint_ui_thumb_pending(void) { return g_thumb_pending; }

void maint_ui_decode_queued_thumb(void)
{
    g_thumb_pending = false;
    g_thumb_ok = false;
    if (g_selected < 0 || g_selected >= g_count) {
        return;
    }
    const MaintItem *item = &g_items[g_selected];
    if (g_thumb_at < 1 || g_thumb_at > item->photo_count || g_thumb_at > kMaintPhotoMax) {
        return;
    }
    uint16_t *dst = g_thumb_px[g_thumb_at - 1];
    if (dst == nullptr) {
        return;
    }
    size_t len = 0;
    uint8_t *buf = read_jpeg(item, g_thumb_at, &len);
    if (buf == nullptr) {
        return;
    }
    g_thumb_ok = snapshot_load_jpeg_thumb(buf, len, kMaintPhotoBytesMax, dst, g_thumb_w, g_thumb_h);
    free(buf);
}

void maint_ui_publish_queued_thumb(void)
{
    if (g_selected < 0 || g_selected >= g_count) {
        return;
    }
    const MaintItem *item = &g_items[g_selected];
    const int slot = static_cast<int>(g_thumb_at) - 1;
    if (g_thumb_ok && slot >= 0 && slot < static_cast<int>(kMaintPhotoMax) &&
        g_thumb_img[slot] != nullptr && g_thumb_px[slot] != nullptr) {
        lv_img_dsc_t *dsc = &g_thumb_dsc[slot];
        memset(dsc, 0, sizeof(*dsc));
        dsc->header.w = static_cast<uint16_t>(g_thumb_w);
        dsc->header.h = static_cast<uint16_t>(g_thumb_h);
        dsc->header.cf = LV_IMG_CF_TRUE_COLOR;
        dsc->data_size = static_cast<uint32_t>(g_thumb_w) * static_cast<uint32_t>(g_thumb_h) *
                         sizeof(uint16_t);
        dsc->data = reinterpret_cast<const uint8_t *>(g_thumb_px[slot]);
        lv_img_cache_invalidate_src(dsc);
        lv_img_set_src(g_thumb_img[slot], dsc);
        lv_obj_invalidate(g_thumb_img[slot]);
    }
    if (g_thumb_at < item->photo_count) {
        ++g_thumb_at;
        g_thumb_pending = true;
        char line[40];
        snprintf(line, sizeof(line), "Loading photo %u of %u…",
                 static_cast<unsigned>(g_thumb_at),
                 static_cast<unsigned>(item->photo_count));
        set_status(line);
    } else {
        set_status("");
    }
}

static void open_photo(int slot, bool on_note)
{
    if (g_selected < 0 || g_selected >= g_count) {
        return;
    }
    if (slot < 0 || slot >= g_items[g_selected].photo_count) {
        return;
    }
    g_photo_n = static_cast<uint8_t>(slot + 1);
    g_open_on_note = on_note;
    dismiss_rail();
    queue_photo();
}

static void on_thumb(lv_event_t *e)
{
    const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    open_photo(slot, false);
}

static void on_note(lv_event_t *e)
{
    const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    open_photo(slot, true);
}

static void show_reader(void)
{
    if (g_reader == nullptr || g_selected < 0 || g_selected >= g_count) {
        return;
    }
    const MaintItem *item = &g_items[g_selected];
    if (g_reader_title != nullptr) {
        lv_label_set_text(g_reader_title, item->title[0] != '\0' ? item->title : "—");
    }
    if (g_reader_body != nullptr) {
        lv_label_set_text(g_reader_body,
                          item->description[0] != '\0' ? item->description : "—");
    }
    if (g_reader_scroll != nullptr) {
        lv_obj_scroll_to_y(g_reader_scroll, 0, LV_ANIM_OFF);
    }
    lv_obj_clear_flag(g_reader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_reader);
    dismiss_rail();
}

static void on_open_reader(lv_event_t * /*e*/) { show_reader(); }

static void on_reader_close(lv_event_t * /*e*/) { hide_reader(); }

static void show_detail(int idx)
{
    if (idx < 0 || idx >= g_count || g_detail_page == nullptr) {
        return;
    }
    g_selected = idx;
    g_photo_n = 1;
    g_photo_pending = false;
    g_thumb_pending = false;
    g_open_on_note = false;
    hide_reader();
    const MaintItem *item = &g_items[idx];

    lv_label_set_text(g_title, item->title[0] != '\0' ? item->title : "—");
    lv_point_t title_sz;
    lv_txt_get_size(&title_sz, item->title, &lv_font_montserrat_20, 0, 0, LV_COORD_MAX,
                    LV_TEXT_FLAG_NONE);
    set_clickable(g_title, item->title[0] != '\0' && title_sz.x > g_inner);

    char token[48];
    pretty_token(item->category, token, sizeof(token));
    lv_label_set_text(g_val_cat, token);
    paint_priority(g_pri_chip, g_pri_lbl, item->priority);
    pretty_token(item->status, token, sizeof(token));
    lv_label_set_text(g_val_status, token);
    lv_label_set_text(g_val_reporter, item->reporter[0] != '\0' ? item->reporter : "—");

    const char *desc = item->description[0] != '\0' ? item->description : "—";
    lv_label_set_text(g_desc, desc);
    lv_point_t desc_sz;
    lv_txt_get_size(&desc_sz, item->description, &lv_font_montserrat_16, 0, 0, g_desc_text_w,
                    LV_TEXT_FLAG_NONE);
    const lv_coord_t desc_limit =
        lv_font_get_line_height(&lv_font_montserrat_16) * kDescLines;
    const bool desc_clip = item->description[0] != '\0' && desc_sz.y > desc_limit;
    set_clickable(g_desc_well, desc_clip);
    if (g_desc_more != nullptr) {
        if (desc_clip) {
            lv_obj_clear_flag(g_desc_more, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_desc_more, LV_OBJ_FLAG_HIDDEN);
        }
    }

    const int photos = item->photo_count;
    if (g_photos_hd != nullptr) {
        if (photos > 0) {
            lv_obj_clear_flag(g_photos_hd, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_photos_hd, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_no_photos != nullptr) {
        if (photos == 0) {
            lv_obj_clear_flag(g_no_photos, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_no_photos, LV_OBJ_FLAG_HIDDEN);
        }
    }
    for (int i = 0; i < static_cast<int>(kMaintPhotoMax); ++i) {
        if (g_thumb_row[i] == nullptr) {
            continue;
        }
        if (i < photos) {
            lv_img_set_src(g_thumb_img[i], nullptr);
            if (item->photo_note[i][0] != '\0') {
                lv_label_set_text(g_thumb_note[i], item->photo_note[i]);
                lv_obj_set_style_text_color(g_thumb_note[i], lv_color_hex(0xE8F0F7), 0);
            } else {
                lv_label_set_text(g_thumb_note[i], "No note for this photo");
                lv_obj_set_style_text_color(g_thumb_note[i], lv_color_hex(0x8FA3B8), 0);
            }
            lv_obj_clear_flag(g_thumb_row[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_thumb_row[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (photos > 0) {
        g_thumb_at = 1;
        g_thumb_pending = true;
        char line[40];
        snprintf(line, sizeof(line), "Loading photo 1 of %u…", static_cast<unsigned>(photos));
        set_status(line);
    } else {
        set_status("");
    }

    lv_obj_add_flag(g_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN);
    if (g_detail_scroll != nullptr) {
        lv_obj_scroll_to_y(g_detail_scroll, 0, LV_ANIM_OFF);
    }
    publish_book_subtitle(item->title);
    sync_debug_log();
    dismiss_rail();
}

static void on_row(lv_event_t *e)
{
    const int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (idx < 0 || idx >= g_count) {
        return;
    }
    show_detail(idx);
}

void maint_ui_next_photo(void)
{
    if (g_selected < 0 || g_selected >= g_count) {
        return;
    }
    const uint8_t n = g_items[g_selected].photo_count;
    if (n < 2) {
        return;
    }
    g_photo_n = static_cast<uint8_t>(g_photo_n % n) + 1;
    g_open_on_note = false;
    queue_photo();
}

void maint_ui_next_report(void)
{
    if (!maint_ui_showing_detail()) {
        return;
    }
    if (g_reader != nullptr && !lv_obj_has_flag(g_reader, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (g_count < 2 || g_selected < 0 || g_selected >= g_count) {
        return;
    }
    show_detail((g_selected + 1) % g_count);
}

void maint_ui_reload(void)
{
    const bool ready = maint_store_ready();
    const uint32_t gen = maint_store_generation();
    /* The six open items already live in g_items. Read the card again only
     * when it changed, or when this is the first look. */
    if (!ready || !g_items_fresh || gen != g_items_gen) {
        g_count = 0;
        MaintListCursor cursor;
        if (ready && maint_store_list_start(&cursor)) {
            MaintItem item;
            while (g_count < kRows && maint_store_list_next(&cursor, &item)) {
                if (!is_open(&item)) {
                    continue;
                }
                g_items[g_count++] = item;
            }
            maint_store_list_end(&cursor);
        }
        g_items_fresh = ready;
        g_items_gen = gen;
    }
    g_selected = 0;
    g_photo_n = 1;
    for (int i = 0; i < kRows; ++i) {
        if (g_rows[i] == nullptr) {
            continue;
        }
        if (i < g_count) {
            lv_label_set_text(g_row_title[i],
                              g_items[i].title[0] != '\0' ? g_items[i].title : "—");
            char cat[40];
            pretty_token(g_items[i].category, cat, sizeof(cat));
            lv_label_set_text(g_row_cat[i], cat);
            paint_priority(g_row_pri[i], g_row_pri_lbl[i], g_items[i].priority);
            if (g_row_photo[i] != nullptr) {
                if (g_items[i].photo_count > 0) {
                    lv_obj_clear_flag(g_row_photo[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_add_flag(g_row_photo[i], LV_OBJ_FLAG_HIDDEN);
                }
            }
            lv_obj_clear_flag(g_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_empty != nullptr) {
        if (g_count == 0) {
            lv_label_set_text(g_empty, maint_store_ready() ? "No open reports"
                                                          : "No card in the slot");
            lv_obj_clear_flag(g_empty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }
    show_list();
}

static lv_obj_t *wrap_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_coord_t width)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl, width);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);
    lv_label_set_text(lbl, "");
    return lbl;
}

static void alloc_thumbs(void)
{
    const size_t bytes = static_cast<size_t>(g_thumb_w) * static_cast<size_t>(g_thumb_h) *
                         sizeof(uint16_t);
    for (int i = 0; i < static_cast<int>(kMaintPhotoMax); ++i) {
        if (g_thumb_px[i] != nullptr) {
            continue;
        }
        g_thumb_px[i] = static_cast<uint16_t *>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (g_thumb_px[i] == nullptr) {
            g_thumb_px[i] = static_cast<uint16_t *>(malloc(bytes));
        }
        if (g_thumb_px[i] != nullptr) {
            memset(g_thumb_px[i], 0, bytes);
        }
    }
}

void maint_ui_build(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                    void (*on_photo)(void), void (*on_subtitle)(const char *text),
                    void (*on_dismiss)(void))
{
    g_on_photo = on_photo;
    g_on_subtitle = on_subtitle;
    g_on_dismiss = on_dismiss;

    g_inner = width > 40 ? width - 32 : width;
    const lv_coord_t split = g_inner > 24 ? (g_inner - 12) / 2 : g_inner / 2;
    g_thumb_w = split > 16 ? split : 16;
    g_thumb_h = g_thumb_w * 2 / 3;
    if (g_thumb_h < 16) {
        g_thumb_h = 16;
    }
    alloc_thumbs();

    const lv_coord_t list_w = width > 16 ? width - 16 : width;
    const lv_coord_t list_h = height > 16 ? height - 16 : height;
    const lv_coord_t row_w = list_w > 24 ? list_w - 24 : list_w;
    const lv_coord_t chip_w = 112;
    const lv_coord_t cat_w = 170;
    const lv_coord_t photo_w = 36;
    const lv_coord_t gap = 10;
    const lv_coord_t chip_x = row_w > chip_w + 20 ? row_w - 10 - chip_w : row_w / 2;
    const lv_coord_t photo_x = chip_x > photo_w + gap ? chip_x - gap - photo_w : 0;
    const lv_coord_t cat_x = photo_x > cat_w + gap ? photo_x - gap - cat_w : 0;
    const lv_coord_t title_x = 12;
    const lv_coord_t title_w = cat_x > title_x + gap ? cat_x - gap - title_x : 80;

    g_list = lv_obj_create(parent);
    lv_obj_set_size(g_list, list_w, list_h);
    lv_obj_set_pos(g_list, 8, 8);
    lv_obj_set_style_bg_color(g_list, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(g_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_list, 0, 0);
    lv_obj_set_style_radius(g_list, 14, 0);
    lv_obj_set_style_pad_all(g_list, 12, 0);
    lv_obj_clear_flag(g_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(g_list, on_background, LV_EVENT_CLICKED, nullptr);

    auto col_head = [](lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t w) {
        lv_obj_t *lbl = lv_label_create(parent);
        lv_label_set_text(lbl, text);
        lv_obj_set_width(lbl, w);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x8FA3B8), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x, 0);
    };
    col_head(g_list, "Title", title_x, title_w);
    col_head(g_list, "Category", cat_x, cat_w);
    col_head(g_list, "Priority", chip_x, chip_w);
    lv_obj_t *photo_hd = lv_label_create(g_list);
    lv_label_set_text(photo_hd, LV_SYMBOL_IMAGE);
    lv_obj_set_style_text_font(photo_hd, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(photo_hd, lv_color_hex(0x8FA3B8), 0);
    lv_obj_align(photo_hd, LV_ALIGN_TOP_LEFT, photo_x + 6, -2);

    g_empty = lv_label_create(g_list);
    lv_label_set_text(g_empty, "No open reports");
    lv_obj_set_style_text_color(g_empty, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_empty, &lv_font_montserrat_16, 0);
    lv_obj_align(g_empty, LV_ALIGN_CENTER, 0, 10);

    const lv_coord_t title_line = lv_font_get_line_height(&lv_font_montserrat_16);
    const lv_coord_t cat_line = lv_font_get_line_height(&lv_font_montserrat_14);
    for (int i = 0; i < kRows; ++i) {
        lv_obj_t *btn = lv_btn_create(g_list);
        style_btn(btn, row_w, 50);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 22 + i * 54);
        lv_obj_add_event_cb(btn, on_row, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));

        lv_obj_t *title = lv_label_create(btn);
        lv_label_set_text(title, "");
        lv_obj_set_width(title, title_w);
        lv_obj_set_height(title, title_line);
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(title, LV_ALIGN_LEFT_MID, title_x, 0);

        lv_obj_t *photo = lv_label_create(btn);
        lv_label_set_text(photo, LV_SYMBOL_IMAGE);
        lv_obj_set_style_text_font(photo, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(photo, lv_color_hex(0x00B4A6), 0);
        lv_obj_align(photo, LV_ALIGN_LEFT_MID, photo_x + 6, 0);
        lv_obj_add_flag(photo, LV_OBJ_FLAG_HIDDEN);

        lv_obj_t *cat = lv_label_create(btn);
        lv_label_set_text(cat, "");
        lv_obj_set_width(cat, cat_w);
        lv_obj_set_height(cat, cat_line);
        lv_label_set_long_mode(cat, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(cat, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(cat, lv_color_hex(0x8FA3B8), 0);
        lv_obj_align(cat, LV_ALIGN_LEFT_MID, cat_x, 0);

        lv_obj_t *chip = lv_obj_create(btn);
        lv_obj_set_size(chip, chip_w, 28);
        lv_obj_set_style_radius(chip, 8, 0);
        lv_obj_set_style_border_width(chip, 0, 0);
        lv_obj_set_style_shadow_width(chip, 0, 0);
        lv_obj_set_style_pad_all(chip, 0, 0);
        lv_obj_set_style_bg_color(chip, lv_color_hex(0x5C7080), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_align(chip, LV_ALIGN_LEFT_MID, chip_x, 0);
        lv_obj_t *pri = lv_label_create(chip);
        lv_label_set_text(pri, "");
        lv_obj_set_width(pri, chip_w - 8);
        lv_label_set_long_mode(pri, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(pri, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(pri, &lv_font_montserrat_14, 0);
        lv_obj_center(pri);

        g_rows[i] = btn;
        g_row_title[i] = title;
        g_row_cat[i] = cat;
        g_row_pri[i] = chip;
        g_row_pri_lbl[i] = pri;
        g_row_photo[i] = photo;
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    }

    g_http = lv_label_create(g_list);
    lv_label_set_text(g_http, "HTTP: no request yet");
    lv_obj_set_style_text_color(g_http, lv_color_hex(0xF0C674), 0);
    lv_obj_set_style_text_font(g_http, &lv_font_montserrat_12, 0);
    lv_label_set_long_mode(g_http, LV_LABEL_LONG_DOT);
    lv_obj_set_width(g_http, row_w);
    lv_obj_align(g_http, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    g_http_log = lv_label_create(parent);
    lv_label_set_text(g_http_log, "No request has reached this boat.");
    lv_obj_set_style_text_color(g_http_log, lv_color_hex(0xE8F0F7), 0);
    lv_obj_set_style_text_font(g_http_log, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(g_http_log, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(g_http_log, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g_http_log, 14, 0);
    lv_obj_set_style_pad_all(g_http_log, 12, 0);
    lv_label_set_long_mode(g_http_log, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_http_log, width > 440 ? 420 : width - 24);
    lv_obj_align(g_http_log, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_add_flag(g_http_log, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(g_http_log);

    const lv_coord_t text_w = g_inner;
    const lv_coord_t well_gap = 10;
    const lv_coord_t well_w = (g_inner - well_gap) / 2;
    const lv_coord_t well_h = 58;
    const lv_coord_t note_w = g_inner - 12 - g_thumb_w;
    g_desc_text_w = g_inner > 16 ? g_inner - 16 : g_inner;

    g_detail_page = lv_obj_create(parent);
    lv_obj_set_size(g_detail_page, width, height);
    lv_obj_set_pos(g_detail_page, 0, 0);
    lv_obj_set_style_bg_color(g_detail_page, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(g_detail_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_detail_page, 0, 0);
    lv_obj_set_style_radius(g_detail_page, 0, 0);
    lv_obj_set_style_pad_all(g_detail_page, 0, 0);
    lv_obj_clear_flag(g_detail_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_detail_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_detail_page, on_background, LV_EVENT_CLICKED, nullptr);

    g_status = lv_label_create(g_detail_page);
    lv_label_set_text(g_status, "");
    lv_obj_set_width(g_status, text_w);
    lv_label_set_long_mode(g_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(g_status, lv_color_hex(0xF0C674), 0);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_14, 0);
    lv_obj_align(g_status, LV_ALIGN_TOP_RIGHT, -8, 8);

    g_detail_scroll = lv_obj_create(g_detail_page);
    lv_obj_set_size(g_detail_scroll, width, height > 36 ? height - 36 : height);
    lv_obj_align(g_detail_scroll, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(g_detail_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_detail_scroll, 0, 0);
    lv_obj_set_style_radius(g_detail_scroll, 0, 0);
    lv_obj_set_style_pad_all(g_detail_scroll, 16, 0);
    lv_obj_set_style_pad_row(g_detail_scroll, 10, 0);
    lv_obj_set_flex_flow(g_detail_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_detail_scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_detail_scroll, LV_DIR_VER);
    visible_scrollbar(g_detail_scroll);
    lv_obj_add_event_cb(g_detail_scroll, on_background, LV_EVENT_CLICKED, nullptr);

    g_title = lv_label_create(g_detail_scroll);
    lv_label_set_text(g_title, "");
    lv_obj_set_width(g_title, text_w);
    lv_obj_set_height(g_title, lv_font_get_line_height(&lv_font_montserrat_20));
    lv_label_set_long_mode(g_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(g_title, on_open_reader, LV_EVENT_CLICKED, nullptr);

    auto make_pair = [](lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
        lv_obj_t *row = lv_obj_create(parent);
        lv_obj_set_size(row, w, h);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);
        return row;
    };
    auto make_well = [](lv_obj_t *parent, const char *caption, lv_coord_t w, lv_coord_t h) {
        lv_obj_t *box = lv_obj_create(parent);
        lv_obj_set_size(box, w, h);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x12243A), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(box, 0, 0);
        lv_obj_set_style_radius(box, 10, 0);
        lv_obj_set_style_pad_all(box, 8, 0);
        lv_obj_set_style_shadow_width(box, 0, 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_t *cap = lv_label_create(box);
        lv_label_set_text(cap, caption);
        lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(cap, lv_color_hex(0x8FA3B8), 0);
        lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, 0);
        return box;
    };
    auto well_value = [](lv_obj_t *box, lv_coord_t w) {
        lv_obj_t *val = lv_label_create(box);
        lv_label_set_text(val, "");
        lv_obj_set_width(val, w > 16 ? w - 16 : w);
        lv_obj_set_height(val, lv_font_get_line_height(&lv_font_montserrat_16));
        lv_label_set_long_mode(val, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        return val;
    };

    lv_obj_t *pair1 = make_pair(g_detail_scroll, text_w, well_h);
    g_val_cat = well_value(make_well(pair1, "Category", well_w, well_h), well_w);
    lv_obj_t *pri_well = make_well(pair1, "Priority", well_w, well_h);
    g_pri_chip = lv_obj_create(pri_well);
    lv_obj_set_size(g_pri_chip, 120, 26);
    lv_obj_set_style_radius(g_pri_chip, 8, 0);
    lv_obj_set_style_border_width(g_pri_chip, 0, 0);
    lv_obj_set_style_shadow_width(g_pri_chip, 0, 0);
    lv_obj_set_style_pad_all(g_pri_chip, 0, 0);
    lv_obj_set_style_bg_color(g_pri_chip, lv_color_hex(0x5C7080), 0);
    lv_obj_set_style_bg_opa(g_pri_chip, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_pri_chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_pri_chip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(g_pri_chip, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    g_pri_lbl = lv_label_create(g_pri_chip);
    lv_label_set_text(g_pri_lbl, "");
    lv_obj_set_width(g_pri_lbl, 112);
    lv_label_set_long_mode(g_pri_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g_pri_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_pri_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(g_pri_lbl);

    lv_obj_t *pair2 = make_pair(g_detail_scroll, text_w, well_h);
    g_val_status = well_value(make_well(pair2, "Status", well_w, well_h), well_w);
    g_val_reporter = well_value(make_well(pair2, "Reporter", well_w, well_h), well_w);

    const lv_coord_t desc_line = lv_font_get_line_height(&lv_font_montserrat_16);
    const lv_coord_t desc_body_h = desc_line * kDescLines;
    g_desc_well = lv_obj_create(g_detail_scroll);
    lv_obj_set_size(g_desc_well, text_w, 8 + 18 + desc_body_h + 8);
    lv_obj_set_style_bg_color(g_desc_well, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(g_desc_well, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_desc_well, 0, 0);
    lv_obj_set_style_radius(g_desc_well, 10, 0);
    lv_obj_set_style_pad_all(g_desc_well, 8, 0);
    lv_obj_set_style_shadow_width(g_desc_well, 0, 0);
    lv_obj_clear_flag(g_desc_well, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_desc_well, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_desc_well, on_open_reader, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *desc_cap = lv_label_create(g_desc_well);
    lv_label_set_text(desc_cap, "Description");
    lv_obj_set_style_text_font(desc_cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(desc_cap, lv_color_hex(0x8FA3B8), 0);
    lv_obj_align(desc_cap, LV_ALIGN_TOP_LEFT, 0, 0);
    g_desc_more = lv_label_create(g_desc_well);
    lv_label_set_text(g_desc_more, "More");
    lv_obj_set_style_text_font(g_desc_more, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(g_desc_more, lv_color_hex(0x00B4A6), 0);
    lv_obj_align(g_desc_more, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_flag(g_desc_more, LV_OBJ_FLAG_HIDDEN);
    g_desc = lv_label_create(g_desc_well);
    lv_label_set_text(g_desc, "");
    lv_obj_set_width(g_desc, g_desc_text_w);
    lv_obj_set_height(g_desc, desc_body_h);
    lv_label_set_long_mode(g_desc, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_desc, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_desc, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(g_desc, LV_ALIGN_TOP_LEFT, 0, 18);

    g_photos_hd = lv_label_create(g_detail_scroll);
    lv_label_set_text(g_photos_hd, "PHOTOS");
    lv_obj_set_style_text_color(g_photos_hd, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(g_photos_hd, &lv_font_montserrat_14, 0);
    lv_obj_add_flag(g_photos_hd, LV_OBJ_FLAG_HIDDEN);

    g_no_photos = lv_label_create(g_detail_scroll);
    lv_label_set_text(g_no_photos, "No photos on this item");
    lv_obj_set_style_text_color(g_no_photos, lv_color_hex(0xE8F0F7), 0);
    lv_obj_set_style_text_font(g_no_photos, &lv_font_montserrat_16, 0);
    lv_obj_add_flag(g_no_photos, LV_OBJ_FLAG_HIDDEN);

    const lv_coord_t note_line = lv_font_get_line_height(&lv_font_montserrat_16);
    for (int i = 0; i < static_cast<int>(kMaintPhotoMax); ++i) {
        lv_obj_t *row = lv_obj_create(g_detail_scroll);
        lv_obj_set_width(row, text_w);
        lv_obj_set_height(row, g_thumb_h);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_set_style_pad_column(row, 12, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_set_size(btn, g_thumb_w, g_thumb_h);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_radius(btn, 10, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_clip_corner(btn, true, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(btn, on_thumb, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *img = lv_img_create(btn);
        lv_obj_center(img);
        lv_obj_clear_flag(img, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t *note_well = lv_obj_create(row);
        lv_obj_set_size(note_well, note_w > 16 ? note_w : 16, g_thumb_h);
        lv_obj_set_style_bg_color(note_well, lv_color_hex(0x12243A), 0);
        lv_obj_set_style_bg_opa(note_well, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(note_well, 0, 0);
        lv_obj_set_style_radius(note_well, 10, 0);
        lv_obj_set_style_pad_all(note_well, 10, 0);
        lv_obj_set_style_shadow_width(note_well, 0, 0);
        lv_obj_clear_flag(note_well, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(note_well, on_note, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *note = lv_label_create(note_well);
        lv_label_set_text(note, "");
        lv_obj_set_width(note, note_w > 36 ? note_w - 20 : note_w);
        lv_obj_set_height(note, g_thumb_h > note_line + 20 ? g_thumb_h - 20 : note_line);
        lv_label_set_long_mode(note, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(note, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(note, lv_color_hex(0xE8F0F7), 0);
        lv_obj_align(note, LV_ALIGN_TOP_LEFT, 0, 0);

        g_thumb_row[i] = row;
        g_thumb_img[i] = img;
        g_thumb_note[i] = note;
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    }

    g_reader = lv_obj_create(g_detail_page);
    lv_obj_set_size(g_reader, width, height);
    lv_obj_set_pos(g_reader, 0, 0);
    lv_obj_set_style_bg_color(g_reader, lv_color_hex(0x071422), 0);
    lv_obj_set_style_bg_opa(g_reader, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_reader, 0, 0);
    lv_obj_set_style_radius(g_reader, 0, 0);
    lv_obj_set_style_pad_all(g_reader, 0, 0);
    lv_obj_clear_flag(g_reader, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_reader, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(g_reader, on_background, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *close = lv_btn_create(g_reader);
    style_btn(close, 100, 36);
    lv_obj_align(close, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_add_event_cb(close, on_reader_close, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *close_lbl = lv_label_create(close);
    lv_label_set_text(close_lbl, "Close");
    lv_obj_set_style_text_font(close_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(close_lbl);

    g_reader_scroll = lv_obj_create(g_reader);
    lv_obj_set_size(g_reader_scroll, width, height > 56 ? height - 56 : height);
    lv_obj_align(g_reader_scroll, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(g_reader_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_reader_scroll, 0, 0);
    lv_obj_set_style_radius(g_reader_scroll, 0, 0);
    lv_obj_set_style_pad_all(g_reader_scroll, 16, 0);
    lv_obj_set_style_pad_row(g_reader_scroll, 12, 0);
    lv_obj_set_flex_flow(g_reader_scroll, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_reader_scroll, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(g_reader_scroll, LV_DIR_VER);
    visible_scrollbar(g_reader_scroll);
    lv_obj_add_event_cb(g_reader_scroll, on_background, LV_EVENT_CLICKED, nullptr);

    g_reader_title = wrap_label(g_reader_scroll, &lv_font_montserrat_20, 0xFFFFFF, text_w);
    g_reader_body = wrap_label(g_reader_scroll, &lv_font_montserrat_16, 0xE8F0F7, text_w);
}

#endif /* LMYC_WITH_REPORTS_CHARTS */
