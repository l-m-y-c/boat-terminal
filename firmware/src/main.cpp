/**
 * LMYC Boat Terminal - Firmware Foundation
 * Hardware: Waveshare ESP32-S3-Touch-LCD-7 (800x480 RGB ST7262 + GT911 touch)
 *
 * This is the minimal viable display + touch skeleton.
 * - Initializes the official ESP32_Display_Panel driver (handles LCD, backlight via CH422G IO expander, GT911 touch)
 * - Sets up LVGL v8
 * - Draws a clean title screen ("LMYC Boat Terminal")
 * - Draws a colored rectangle as visual anchor
 * - Registers touch input and prints coordinates to Serial on press/move
 *
 * Context: See docs/03-system-architecture.md (phone-primary hybrid design),
 * docs/04-hardware.md, and docs/14-prototype-hardware.md (bench testing before
 * isolated NMEA or BLE pairing).
 *
 * Next steps after this foundation:
 *   - Add LVGL styles/themes matching LMYC branding
 *   - Implement logbook entry screen, instrument gauges, QR pairing screen
 *   - Add persistent storage (LittleFS on TF card) for logs
 *   - Integrate isolated NMEA 0183/2000 (CAN/RS485 headers)
 *
 * Build: pio run
 * Upload: pio run -t upload  (hold BOOT button on first flash if needed)
 * Monitor: pio device monitor
 */

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <lvgl.h>

using namespace esp_panel::drivers;  // For LCD and Touch classes in callbacks
using namespace esp_panel::board;   // For Board

// Use the official board abstraction (handles LCD, touch, IO expander, backlight)
using namespace esp_panel::board;

// LVGL objects
static lv_obj_t *label_title = nullptr;
static lv_obj_t *rect_box = nullptr;

// LVGL display and input buffers
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;  // Optional second buffer for better performance

// Panel instance (global so touch callback can access it)
static Board *panel_board = nullptr;

// Forward declarations
void lvgl_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p);
void lvgl_touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data);
void create_ui(void);

/* ------------------------------------------------------------------ */
/* LVGL Display Flush Callback                                        */
/* ------------------------------------------------------------------ */
void lvgl_flush_cb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    // The ESP32_Display_Panel library provides a direct draw interface.
    // For RGB panels like this one, it uses frame buffer or DMA; we cast to uint8_t* as the library expects raw bytes for RGB565.
    LCD *lcd = panel_board->getLCD();
    if (lcd != nullptr) {
        int w = area->x2 - area->x1 + 1;
        int h = area->y2 - area->y1 + 1;
        lcd->drawBitmap(area->x1, area->y1, w, h, (const uint8_t *)color_p);
    }

    lv_disp_flush_ready(disp);
}

/* ------------------------------------------------------------------ */
/* LVGL Touch Input Callback                                          */
/* ------------------------------------------------------------------ */
void lvgl_touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
    Touch *touch = panel_board->getTouch();
    if (touch == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    // The modern API requires readRawData() before getPoints()
    // We read one point for LVGL pointer input (LVGL handles debouncing and state)
    TouchPoint points[1];
    int num_points = touch->readPoints(points, 1);

    if (num_points > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = points[0].x;
        data->point.y = points[0].y;

        // Print to Serial for development (visible in pio device monitor)
        Serial.printf("Touch: (%d, %d)\n", points[0].x, points[0].y);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* ------------------------------------------------------------------ */
/* UI Creation - Simple title + colored visual element               */
/* ------------------------------------------------------------------ */
void create_ui(void)
{
    // Main screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x001F3F), 0);  // Navy background (LMYC nautical feel)

    // Title label - large, centered, white
    label_title = lv_label_create(scr);
    lv_label_set_text(label_title, "LMYC Boat Terminal");
    lv_obj_set_style_text_color(label_title, lv_color_white(), 0);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_32, 0);
    lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 40);

    // Colored box (teal rectangle) as visual anchor / placeholder for future gauges or cards
    rect_box = lv_obj_create(scr);
    lv_obj_set_size(rect_box, 400, 200);
    lv_obj_align(rect_box, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_style_bg_color(rect_box, lv_color_hex(0x00B4A6), 0);  // Teal - nautical accent
    lv_obj_set_style_border_width(rect_box, 4, 0);
    lv_obj_set_style_border_color(rect_box, lv_color_white(), 0);
    lv_obj_set_style_radius(rect_box, 16, 0);

    // Label inside the box
    lv_obj_t *box_label = lv_label_create(rect_box);
    lv_label_set_text(box_label, "Display + Touch Ready\n\nTouch anywhere to test");
    lv_obj_set_style_text_color(box_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(box_label, &lv_font_montserrat_16, 0);
    lv_obj_center(box_label);

    Serial.println("LVGL UI created successfully.");
}

/* ------------------------------------------------------------------ */
/* Arduino Setup                                                      */
/* ------------------------------------------------------------------ */
void setup()
{
    Serial.begin(115200);
    Serial.println("\n\n=== LMYC Boat Terminal - Firmware Foundation ===");
    Serial.println("Initializing Waveshare ESP32-S3-Touch-LCD-7...");

    // Create and initialize the official board driver
    panel_board = new Board();
    if (!panel_board) {
        Serial.println("ERROR: Failed to create Board object!");
        while (1) delay(100);
    }

    // The Board::init() and begin() call the factory with the BOARD_WAVESHARE_... define
    // This selects the correct LCD, touch, and backlight configuration for this exact Waveshare board.
    panel_board->init();
    if (!panel_board->begin()) {
        Serial.println("ERROR: Board begin() failed! Check connections and defines.");
        while (1) delay(100);
    }

    Serial.println("Panel (LCD + Touch + IO Expander) initialized.");

    // Allocate LVGL draw buffers (two for smoother RGB panel updates)
    const uint32_t buf_size = LV_HOR_RES_MAX * 40;  // Partial buffer (good balance for 800x480)
    buf1 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!buf1 || !buf2) {
        Serial.println("ERROR: Failed to allocate LVGL buffers in PSRAM!");
        while (1) delay(100);
    }

    lv_init();
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_size);

    // Register display driver
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_HOR_RES_MAX;
    disp_drv.ver_res = LV_VER_RES_MAX;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Register touch driver
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    // Create the initial UI
    create_ui();

    Serial.println("Setup complete. Touch the screen to see coordinates in Serial Monitor.");
    Serial.println("Ready for further UI development (logbook, instruments, QR pairing).");
}

/* ------------------------------------------------------------------ */
/* Arduino Loop - LVGL Task Handler                                   */
/* ------------------------------------------------------------------ */
void loop()
{
    lv_timer_handler();   // Must be called periodically (LVGL heartbeat)
    delay(5);             // Keep responsive; ~200 Hz update rate
}
