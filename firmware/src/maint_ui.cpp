#include "maint_ui.h"

#include "maint_store.h"
#include "snapshot.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <cstring>

static constexpr int kRows = 6;

static MaintItem g_items[kRows];
static int g_count = 0;
static int g_selected = 0;
static uint8_t g_photo_n = 1;

static lv_obj_t *g_rows[kRows];
static lv_obj_t *g_row_lbl[kRows];
static lv_obj_t *g_empty = nullptr;
static lv_obj_t *g_detail = nullptr;
static void (*g_on_photo)(void) = nullptr;
static char g_caption[120] = "";

static bool is_open(const MaintItem *item)
{
    return strcmp(item->status, "completed") != 0 &&
           strcmp(item->status, "future-wishlist") != 0;
}

static void style_btn(lv_obj_t *btn)
{
    lv_obj_set_size(btn, 330, 40);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E3A5F), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

const char *maint_ui_caption(void) { return g_caption; }

static void set_detail(const char *text)
{
    snprintf(g_caption, sizeof(g_caption), "%s", text);
    if (g_detail != nullptr) {
        lv_label_set_text(g_detail, g_caption);
    }
}

static void describe_selected(void)
{
    if (g_count == 0) {
        set_detail("No open items on this boat");
        return;
    }
    if (g_selected < 0 || g_selected >= g_count) {
        g_selected = 0;
    }
    const MaintItem *item = &g_items[g_selected];
    char line[120];
    if (item->photo_count == 0) {
        snprintf(line, sizeof(line), "%s  ·  %s  ·  no photo", item->category, item->priority);
    } else {
        snprintf(line, sizeof(line), "%s  ·  %s  ·  %u photo(s)",
                 item->category, item->priority,
                 static_cast<unsigned>(item->photo_count));
    }
    set_detail(line);
}

static void show_photo(bool open_viewer)
{
    if (g_selected < 0 || g_selected >= g_count || g_items[g_selected].photo_count == 0) {
        if (g_count == 0) {
            set_detail("No open items on this boat");
        } else if (g_selected >= 0 && g_selected < g_count) {
            char line[120];
            const MaintItem *item = &g_items[g_selected];
            snprintf(line, sizeof(line), "%s  ·  %s  ·  no photo", item->category, item->priority);
            set_detail(line);
        }
        return;
    }

    const MaintItem *item = &g_items[g_selected];
    if (g_photo_n < 1 || g_photo_n > item->photo_count) {
        g_photo_n = 1;
    }
    char path[56];
    if (!maint_store_photo_path(item->id, g_photo_n, path, sizeof(path))) {
        set_detail("Photo file missing");
        return;
    }
    File in = SD.open(path, FILE_READ);
    if (!in) {
        set_detail("Photo file missing");
        return;
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
        set_detail("Photo too large for the screen");
        return;
    }
    const size_t got = in.read(buf, len);
    in.close();
    const bool ok = got == len && snapshot_load_jpeg_limited(buf, got, kMaintPhotoBytesMax);
    free(buf);
    if (!ok || snapshot_dsc() == nullptr) {
        set_detail("Could not show the photo");
        return;
    }
    char line[120];
    snprintf(line, sizeof(line), "%s  ·  %s  ·  photo %u/%u",
             item->category, item->priority,
             static_cast<unsigned>(g_photo_n),
             static_cast<unsigned>(item->photo_count));
    set_detail(line);
    if (open_viewer && g_on_photo != nullptr) {
        g_on_photo();
    }
}

static void on_row(lv_event_t *e)
{
    const int idx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (idx < 0 || idx >= g_count) {
        return;
    }
    g_selected = idx;
    g_photo_n = 1;
    show_photo(true);
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
    show_photo(true);
}

void maint_ui_reload(void)
{
    g_count = 0;
    g_selected = 0;
    g_photo_n = 1;
    if (maint_store_ready() && maint_store_list_start()) {
        MaintItem item;
        while (g_count < kRows && maint_store_list_next(&item)) {
            if (!is_open(&item)) {
                continue;
            }
            g_items[g_count++] = item;
        }
        maint_store_list_end();
    }
    for (int i = 0; i < kRows; ++i) {
        if (g_rows[i] == nullptr) {
            continue;
        }
        if (i < g_count) {
            char line[80];
            snprintf(line, sizeof(line), "%s  %s", g_items[i].priority, g_items[i].title);
            lv_label_set_text(g_row_lbl[i], line);
            lv_label_set_long_mode(g_row_lbl[i], LV_LABEL_LONG_DOT);
            lv_obj_set_width(g_row_lbl[i], 300);
            lv_obj_clear_flag(g_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_empty != nullptr) {
        if (g_count == 0) {
            lv_obj_clear_flag(g_empty, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_empty, LV_OBJ_FLAG_HIDDEN);
        }
    }
    describe_selected();
}

void maint_ui_build(lv_obj_t *parent, lv_event_cb_t on_back, void (*on_photo)(void))
{
    g_on_photo = on_photo;
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_set_size(list, 360, 360);
    lv_obj_align(list, LV_ALIGN_LEFT_MID, 8, 10);
    lv_obj_set_style_bg_color(list, lv_color_hex(0x12243A), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 14, 0);
    lv_obj_set_style_pad_all(list, 12, 0);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *heading = lv_label_create(list);
    lv_label_set_text(heading, "OPEN ITEMS");
    lv_obj_set_style_text_color(heading, lv_color_hex(0x8FA3B8), 0);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_16, 0);
    lv_obj_align(heading, LV_ALIGN_TOP_LEFT, 0, 0);

    g_empty = lv_label_create(list);
    lv_label_set_text(g_empty, "No open items yet");
    lv_obj_set_style_text_color(g_empty, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(g_empty, &lv_font_montserrat_16, 0);
    lv_obj_align(g_empty, LV_ALIGN_CENTER, 0, 10);

    for (int i = 0; i < kRows; ++i) {
        lv_obj_t *btn = lv_btn_create(list);
        style_btn(btn);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 28 + i * 46);
        lv_obj_add_event_cb(btn, on_row, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
        g_rows[i] = btn;
        g_row_lbl[i] = lbl;
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    }

    g_detail = lv_label_create(parent);
    lv_label_set_text(g_detail, "");
    lv_obj_set_style_text_color(g_detail, lv_color_hex(0xE8F0F7), 0);
    lv_obj_set_style_text_font(g_detail, &lv_font_montserrat_14, 0);
    lv_obj_align(g_detail, LV_ALIGN_BOTTOM_RIGHT, -16, -8);

    if (on_back != nullptr) {
        lv_obj_t *back = lv_btn_create(parent);
        style_btn(back);
        lv_obj_set_size(back, 100, 36);
        lv_obj_align(back, LV_ALIGN_TOP_RIGHT, -148, 8);
        lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, nullptr);
        lv_obj_t *back_lbl = lv_label_create(back);
        lv_label_set_text(back_lbl, "Helm");
        lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
        lv_obj_center(back_lbl);
    }
}
