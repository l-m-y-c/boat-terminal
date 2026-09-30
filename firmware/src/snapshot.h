#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>

static constexpr uint16_t kSnapshotWidth = 800;
static constexpr uint16_t kSnapshotHeight = 480;
static constexpr size_t kSnapshotJpegMax = 220 * 1024;

void snapshot_init(void);

/* Decode JPEG into an 800×480 RGB565 canvas. Returns false on error. */
bool snapshot_load_jpeg(const uint8_t *jpeg, size_t len);

bool snapshot_has_image(void);
const lv_img_dsc_t *snapshot_dsc(void);
void snapshot_clear(void);
uint32_t snapshot_age_ms(void);
uint32_t snapshot_generation(void);
