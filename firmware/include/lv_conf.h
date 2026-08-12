/**
 * @file lv_conf.h
 * LMYC Boat Terminal - LVGL v8 configuration
 * Minimal but complete for 800x480 RGB display + touch.
 * Based on LVGL template + Waveshare/ESP32_Display_Panel recommendations.
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Enable LVGL features needed for the cabin terminal */
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1

/* Memory and performance */
#define LV_MEM_SIZE (32 * 1024)          /* 32 KB internal memory */
#define LV_MEM_ADR 0                     /* NULL = use malloc */
#define LV_MEM_POOL_INCLUDE <stdlib.h>

/* Display */
#define LV_HOR_RES_MAX 800
#define LV_VER_RES_MAX 480
#define LV_COLOR_DEPTH 16                /* RGB565 for ST7262 panel */
#define LV_COLOR_16_SWAP 0
#define LV_COLOR_SCREEN_TRANSP 0

/* Widgets for simple UI (title, label, colored box/rectangle) */
#define LV_USE_LABEL 1
#define LV_USE_OBJ 1
#define LV_USE_BTN 1
#define LV_USE_ARC 1
#define LV_USE_BAR 0
#define LV_USE_SLIDER 0
#define LV_USE_SWITCH 0
#define LV_USE_CANVAS 1
#define LV_USE_IMG 1

/* Fonts */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1

/* Input devices */
#define LV_USE_INDEV 1
#define LV_USE_TOUCH 1

/* Debug */
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Theme */
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 0

/* Enable extra drawing features */
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/* Buffer for direct mode / partial updates on RGB panel */
#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4

/* Tick */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE <Arduino.h>
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#endif /* LV_CONF_H */
