#include "snapshot.h"

#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>

extern "C" {
#include "rom/tjpgd.h"
}

static uint16_t *g_pixels = nullptr;
static lv_img_dsc_t g_dsc = {};
static bool g_have = false;
static uint32_t g_loaded_ms = 0;
static uint32_t g_generation = 0;
static int g_off_x = 0;
static int g_off_y = 0;
static uint8_t *g_pool = nullptr;

struct JpegSrc {
    const uint8_t *data;
    size_t len;
    size_t off;
};

void snapshot_init(void)
{
    g_have = false;
    g_pixels = nullptr;
    memset(&g_dsc, 0, sizeof(g_dsc));
}

static UINT jpeg_in(JDEC *jd, BYTE *buf, UINT n)
{
    auto *src = static_cast<JpegSrc *>(jd->device);
    if (src->off >= src->len) {
        return 0;
    }
    if (src->off + n > src->len) {
        n = static_cast<UINT>(src->len - src->off);
    }
    if (buf != nullptr) {
        memcpy(buf, src->data + src->off, n);
    }
    src->off += n;
    return n;
}

static UINT jpeg_out(JDEC * /*jd*/, void *bitmap, JRECT *rect)
{
    if (g_pixels == nullptr) {
        return 0;
    }
    const auto *rgb = static_cast<const uint8_t *>(bitmap);
    const int left = rect->left;
    const int right = rect->right;
    const int top = rect->top;
    const int bottom = rect->bottom;
    unsigned i = 0;
    for (int y = top; y <= bottom; ++y) {
        if (y < 0 || y >= kSnapshotHeight) {
            i += static_cast<unsigned>((right - left + 1) * 3);
            continue;
        }
        for (int x = left; x <= right; ++x) {
            const uint8_t r = rgb[i++];
            const uint8_t g = rgb[i++];
            const uint8_t b = rgb[i++];
            const int dx = x + g_off_x;
            const int dy = y + g_off_y;
            if (dx < 0 || dy < 0 || dx >= kSnapshotWidth || dy >= kSnapshotHeight) {
                continue;
            }
            g_pixels[dy * kSnapshotWidth + dx] =
                static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    return 1;
}

bool snapshot_load_jpeg(const uint8_t *jpeg, size_t len)
{
    return snapshot_load_jpeg_limited(jpeg, len, kSnapshotJpegMax);
}

bool snapshot_load_jpeg_limited(const uint8_t *jpeg, size_t len, size_t limit)
{
    if (jpeg == nullptr || len < 32 || len > limit) {
        return false;
    }
    if (g_pixels == nullptr) {
        g_pixels = static_cast<uint16_t *>(
            heap_caps_malloc(kSnapshotWidth * kSnapshotHeight * sizeof(uint16_t),
                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (g_pixels == nullptr) {
            g_pixels = static_cast<uint16_t *>(
                malloc(kSnapshotWidth * kSnapshotHeight * sizeof(uint16_t)));
        }
        if (g_pixels == nullptr) {
            Serial.println("snapshot: no canvas memory");
            return false;
        }
        memset(g_pixels, 0, kSnapshotWidth * kSnapshotHeight * sizeof(uint16_t));
    }

    if (g_pool == nullptr) {
        g_pool = static_cast<uint8_t *>(
            heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (g_pool == nullptr) {
            g_pool = static_cast<uint8_t *>(malloc(16384));
        }
        if (g_pool == nullptr) {
            Serial.println("snapshot: no jpeg pool");
            return false;
        }
    }
    JDEC jd = {};
    JpegSrc src{jpeg, len, 0};
    if (jd_prepare(&jd, jpeg_in, g_pool, 16384, &src) != JDR_OK) {
        Serial.println("snapshot: jd_prepare failed");
        return false;
    }

    BYTE scale = 0;
    UINT w = jd.width;
    UINT h = jd.height;
    while (scale < 3 && (w > kSnapshotWidth || h > kSnapshotHeight)) {
        ++scale;
        w >>= 1;
        h >>= 1;
    }
    g_off_x = (static_cast<int>(kSnapshotWidth) - static_cast<int>(w)) / 2;
    g_off_y = (static_cast<int>(kSnapshotHeight) - static_cast<int>(h)) / 2;
    if (g_off_x < 0) {
        g_off_x = 0;
    }
    if (g_off_y < 0) {
        g_off_y = 0;
    }

    memset(g_pixels, 0, kSnapshotWidth * kSnapshotHeight * sizeof(uint16_t));
    if (jd_decomp(&jd, jpeg_out, scale) != JDR_OK) {
        Serial.println("snapshot: jd_decomp failed");
        g_have = false;
        return false;
    }

    g_dsc.header.always_zero = 0;
    g_dsc.header.w = kSnapshotWidth;
    g_dsc.header.h = kSnapshotHeight;
    g_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    g_dsc.data_size = kSnapshotWidth * kSnapshotHeight * sizeof(uint16_t);
    g_dsc.data = reinterpret_cast<const uint8_t *>(g_pixels);
    g_have = true;
    g_loaded_ms = millis();
    ++g_generation;
    Serial.printf("snapshot: ok %ux%u jpeg %uB\n",
                  static_cast<unsigned>(jd.width),
                  static_cast<unsigned>(jd.height),
                  static_cast<unsigned>(len));
    return true;
}

bool snapshot_has_image(void) { return g_have; }
const lv_img_dsc_t *snapshot_dsc(void) { return g_have ? &g_dsc : nullptr; }
void snapshot_clear(void)
{
    g_have = false;
    ++g_generation;
}
uint32_t snapshot_generation(void) { return g_generation; }
uint32_t snapshot_age_ms(void)
{
    return g_have ? (millis() - g_loaded_ms) : 0;
}
