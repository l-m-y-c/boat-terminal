#pragma once

#include <lvgl.h>

/* Open-items picker, then one item's details. A thumbnail calls on_photo
 * after that JPEG is decoded into the shared snapshot canvas. The note well
 * uses the same callback and maint_ui_open_on_note() is true so the photo
 * page can start on the note. Home and the screen's own buttons live in the
 * cabin chrome, not on this page. width and height are the page size in
 * pixels. Do not measure the parent here: lv_obj_get_width stays 0 until
 * layout, and that clipped the list. */
void maint_ui_build(lv_obj_t *parent, lv_coord_t width, lv_coord_t height,
                    void (*on_photo)(void), void (*on_subtitle)(const char *text),
                    void (*on_dismiss)(void));
void maint_ui_reload(void);
/* True while an item's detail page is up (including under a book photo). */
bool maint_ui_showing_detail(void);
const char *maint_ui_item_title(void);
/* Back to the picker without reading the card again. */
void maint_ui_show_list(void);
void maint_ui_toggle_debug(void);
void maint_ui_next_photo(void);
/* Header line while a book photo is almost full screen. */
const char *maint_ui_caption(void);
/* Note for the photo on screen. Empty when that photo has none. */
const char *maint_ui_photo_note(void);
/* True when the open was a tap on the note well. Next photo clears it. */
bool maint_ui_open_on_note(void);
/* Header line while the book page itself is showing. */
const char *maint_ui_book_subtitle(void);
/* A thumbnail tap only marks the photo. loop() decodes it so the glass can
 * paint "Opening photo…" before the card read blocks. */
bool maint_ui_photo_pending(void);
void maint_ui_decode_queued_photo(void);
void maint_ui_publish_queued_photo(void);
/* One thumbnail per loop() turn. Five full-size decodes in a tap would stall. */
bool maint_ui_thumb_pending(void);
void maint_ui_decode_queued_thumb(void);
void maint_ui_publish_queued_thumb(void);
/* Last HTTP line, and the Debug log. Called from the LVGL task. */
void maint_ui_set_http(const char *last, const char *log);
