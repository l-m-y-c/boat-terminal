#pragma once

#include <lvgl.h>

/* Open-items page. A row with a photo calls on_photo after the JPEG is decoded
 * into the shared snapshot canvas. */
void maint_ui_build(lv_obj_t *parent, lv_event_cb_t on_back, void (*on_photo)(void));
void maint_ui_reload(void);
void maint_ui_next_photo(void);
const char *maint_ui_caption(void);
