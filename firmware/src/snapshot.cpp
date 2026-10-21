#include "boat_config.h"
#if LMYC_WITH_REPORTS_CHARTS

#include "snapshot.h"

#include <Arduino.h>
#include <cstring>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

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

/* The HTTP task (chart posts) and loop() (book photos and thumbnails) both
 * decode. They share g_pool, g_pixels, the offsets and g_thumb_dst. */
static SemaphoreHandle_t g_decode_lock = nullptr;

struct DecodeLock {
    DecodeLock()
    {
        if (g_decode_lock != nullptr) {
            xSemaphoreTake(g_decode_lock, portMAX_DELAY);
        }
    }
    ~DecodeLock()
    {
        if (g_decode_lock != nullptr) {
            xSemaphoreGive(g_decode_lock);
        }
    }
    DecodeLock(const DecodeLock &) = delete;
    DecodeLock &operator=(const DecodeLock &) = delete;
};

struct JpegSrc {
    const uint8_t *data;
    size_t len;
    size_t off;
};

void snapshot_init(void)
{
    if (g_decode_lock == nullptr) {
        g_decode_lock = xSemaphoreCreateMutex();
    }
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
    DecodeLock lock;
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

struct ThumbDst {
    uint16_t *px;
    int w;
    int h;
    int src_w;
    int src_h;
    int ox;
    int oy;
    int draw_w;
    int draw_h;
};

static ThumbDst g_thumb_dst;

static UINT thumb_out(JDEC * /*jd*/, void *bitmap, JRECT *rect)
{
    if (g_thumb_dst.px == nullptr || g_thumb_dst.src_w < 1 || g_thumb_dst.src_h < 1 ||
        g_thumb_dst.draw_w < 1 || g_thumb_dst.draw_h < 1) {
        return 0;
    }
    const auto *rgb = static_cast<const uint8_t *>(bitmap);
    unsigned i = 0;
    for (int y = rect->top; y <= rect->bottom; ++y) {
        for (int x = rect->left; x <= rect->right; ++x) {
            const uint8_t r = rgb[i++];
            const uint8_t g = rgb[i++];
            const uint8_t b = rgb[i++];
            const int dx = g_thumb_dst.ox + x * g_thumb_dst.draw_w / g_thumb_dst.src_w;
            const int dy = g_thumb_dst.oy + y * g_thumb_dst.draw_h / g_thumb_dst.src_h;
            if (dx < 0 || dy < 0 || dx >= g_thumb_dst.w || dy >= g_thumb_dst.h) {
                continue;
            }
            g_thumb_dst.px[dy * g_thumb_dst.w + dx] =
                static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
    return 1;
}

bool snapshot_load_jpeg_thumb(const uint8_t *jpeg, size_t len, size_t limit,
                              uint16_t *dst, int dst_w, int dst_h)
{
    if (jpeg == nullptr || dst == nullptr || len < 32 || len > limit || dst_w < 1 || dst_h < 1) {
        return false;
    }
    DecodeLock lock;
    if (g_pool == nullptr) {
        g_pool = static_cast<uint8_t *>(
            heap_caps_malloc(16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (g_pool == nullptr) {
            g_pool = static_cast<uint8_t *>(malloc(16384));
        }
        if (g_pool == nullptr) {
            return false;
        }
    }
    JDEC jd = {};
    JpegSrc src{jpeg, len, 0};
    if (jd_prepare(&jd, jpeg_in, g_pool, 16384, &src) != JDR_OK) {
        return false;
    }
    BYTE scale = 0;
    int w = static_cast<int>(jd.width);
    int h = static_cast<int>(jd.height);
    while (scale < 3 && (w / 2) >= dst_w && (h / 2) >= dst_h) {
        ++scale;
        w /= 2;
        h /= 2;
    }
    if (w < 1) {
        w = 1;
    }
    if (h < 1) {
        h = 1;
    }
    memset(dst, 0, static_cast<size_t>(dst_w) * static_cast<size_t>(dst_h) * sizeof(uint16_t));
    int draw_w = dst_w;
    int draw_h = dst_h;
    if (static_cast<long>(w) * dst_h < static_cast<long>(h) * dst_w) {
        draw_h = dst_h;
        draw_w = static_cast<int>((static_cast<long>(w) * dst_h) / h);
    } else {
        draw_w = dst_w;
        draw_h = static_cast<int>((static_cast<long>(h) * dst_w) / w);
    }
    if (draw_w < 1) {
        draw_w = 1;
    }
    if (draw_h < 1) {
        draw_h = 1;
    }
    if (draw_w > dst_w) {
        draw_w = dst_w;
    }
    if (draw_h > dst_h) {
        draw_h = dst_h;
    }
    const int ox = (dst_w - draw_w) / 2;
    const int oy = (dst_h - draw_h) / 2;
    g_thumb_dst = ThumbDst{dst, dst_w, dst_h, w, h, ox, oy, draw_w, draw_h};
    const bool ok = jd_decomp(&jd, thumb_out, scale) == JDR_OK;
    g_thumb_dst.px = nullptr;
    return ok;
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

#endif /* LMYC_WITH_REPORTS_CHARTS */
