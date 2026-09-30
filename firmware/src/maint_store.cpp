#include "maint_store.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#include "pairing.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_random.h>
#include <cstdio>
#include <cstring>

static bool parse_string_field(const char *json, const char *key, char *out, size_t cap);

static bool g_ready = false;
static File g_list_dir;
static File g_photo;
static char g_photo_tmp[48];
static char g_photo_dst[48];
static char g_photo_id[kMaintIdHex + 1];
static uint8_t g_photo_n = 0;

bool maint_store_ready(void) { return g_ready; }

bool maint_id_ok(const char *id)
{
    if (id == nullptr) {
        return false;
    }
    for (size_t i = 0; i < kMaintIdHex; ++i) {
        const char c = id[i];
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        if (!hex) {
            return false;
        }
    }
    return id[kMaintIdHex] == '\0';
}

bool maint_category_ok(const char *category)
{
    static const char *kOk[] = {
        "cleanliness", "electrical", "engine", "hull", "interior",
        "other", "plumbing", "rigging", "safety-equipment", "sails",
    };
    if (category == nullptr) {
        return false;
    }
    for (const char *s : kOk) {
        if (strcmp(category, s) == 0) {
            return true;
        }
    }
    return false;
}

bool maint_priority_ok(const char *priority)
{
    return priority != nullptr &&
           (strcmp(priority, "critical") == 0 || strcmp(priority, "high") == 0 ||
            strcmp(priority, "medium") == 0 || strcmp(priority, "low") == 0);
}

static bool item_dir(const char *id, char *out, size_t cap)
{
    return maint_id_ok(id) && snprintf(out, cap, "/maint/%s", id) < static_cast<int>(cap);
}

static bool item_json_path(const char *id, char *out, size_t cap)
{
    char dir[40];
    return item_dir(id, dir, sizeof(dir)) &&
           snprintf(out, cap, "%s/item.json", dir) < static_cast<int>(cap);
}

bool maint_store_begin(void)
{
    g_ready = false;
    if (!SD.exists("/maint") && !SD.mkdir("/maint")) {
        Serial.println("maint: mkdir /maint failed");
        return false;
    }
    g_ready = true;
    Serial.printf("maint: /maint ready, sailboat_id=%u\n",
                  static_cast<unsigned>(LMYC_SAILBOAT_ID));
    return true;
}

static bool append_ch(char *dst, size_t cap, size_t *used, char c)
{
    if (*used + 1 >= cap) {
        return false;
    }
    dst[(*used)++] = c;
    dst[*used] = '\0';
    return true;
}

static bool append_str(char *dst, size_t cap, size_t *used, const char *s)
{
    while (*s != '\0') {
        if (!append_ch(dst, cap, used, *s++)) {
            return false;
        }
    }
    return true;
}

static bool append_json_str(char *dst, size_t cap, size_t *used, const char *s)
{
    if (!append_ch(dst, cap, used, '"')) {
        return false;
    }
    for (; *s != '\0'; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"' || c == '\\') {
            if (!append_ch(dst, cap, used, '\\') || !append_ch(dst, cap, used, static_cast<char>(c))) {
                return false;
            }
        } else if (c == '\n') {
            if (!append_str(dst, cap, used, "\\n")) {
                return false;
            }
        } else if (c == '\r') {
            continue;
        } else if (c < 0x20) {
            continue;
        } else if (!append_ch(dst, cap, used, static_cast<char>(c))) {
            return false;
        }
    }
    return append_ch(dst, cap, used, '"');
}

static void new_id(char out[kMaintIdHex + 1])
{
    const uint32_t a = esp_random();
    const uint32_t b = esp_random();
    snprintf(out, kMaintIdHex + 1, "%08lx%08lx",
             static_cast<unsigned long>(a), static_cast<unsigned long>(b));
}

bool maint_store_save(const MaintItem *item)
{
    if (!g_ready || item == nullptr || !maint_id_ok(item->id)) {
        return false;
    }
    char dir[40];
    char path[56];
    char tmp[56];
    if (!item_dir(item->id, dir, sizeof(dir)) || !item_json_path(item->id, path, sizeof(path))) {
        return false;
    }
    if (!SD.exists(dir) && !SD.mkdir(dir)) {
        return false;
    }
    if (snprintf(tmp, sizeof(tmp), "%s/item.tmp", dir) >= static_cast<int>(sizeof(tmp))) {
        return false;
    }

    static constexpr size_t kJsonCap = 8192;
    char *json = static_cast<char *>(malloc(kJsonCap));
    if (json == nullptr) {
        return false;
    }
    size_t n = 0;
    json[0] = '\0';
    bool ok = append_ch(json, kJsonCap, &n, '{');
    auto field_str = [&](const char *key, const char *val, bool comma) {
        if (comma) {
            ok = ok && append_ch(json, kJsonCap, &n, ',');
        }
        ok = ok && append_json_str(json, kJsonCap, &n, key);
        ok = ok && append_ch(json, kJsonCap, &n, ':');
        ok = ok && append_json_str(json, kJsonCap, &n, val);
    };
    auto field_u32 = [&](const char *key, uint32_t val) {
        char num[16];
        snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(val));
        ok = ok && append_ch(json, kJsonCap, &n, ',');
        ok = ok && append_json_str(json, kJsonCap, &n, key);
        ok = ok && append_ch(json, kJsonCap, &n, ':');
        ok = ok && append_str(json, kJsonCap, &n, num);
    };
    field_str("id", item->id, false);
    field_u32("wp_id", item->wp_id);
    field_u32("sailboat_id", item->sailboat_id);
    field_str("boat", LMYC_BOAT_ID, true);
    field_str("title", item->title, true);
    field_str("description", item->description, true);
    field_str("category", item->category, true);
    field_str("priority", item->priority, true);
    field_str("status", item->status, true);
    field_str("reporter", item->reporter, true);
    field_str("created", item->created, true);
    field_str("modified", item->modified, true);
    field_str("sync", item->sync_state, true);
    field_u32("photo_count", item->photo_count);
    ok = ok && append_str(json, kJsonCap, &n, "}\n");
    if (!ok) {
        free(json);
        return false;
    }

    File out = SD.open(tmp, FILE_WRITE);
    if (!out) {
        free(json);
        return false;
    }
    const size_t wrote = out.print(json);
    out.close();
    free(json);
    if (wrote != n) {
        SD.remove(tmp);
        return false;
    }
    SD.remove(path);
    if (!SD.rename(tmp, path)) {
        SD.remove(tmp);
        return false;
    }
    return true;
}

bool maint_item_from_json(const char *json, MaintItem *out)
{
    if (json == nullptr || out == nullptr) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    parse_string_field(json, "title", out->title, sizeof(out->title));
    parse_string_field(json, "description", out->description, sizeof(out->description));
    parse_string_field(json, "category", out->category, sizeof(out->category));
    parse_string_field(json, "priority", out->priority, sizeof(out->priority));
    parse_string_field(json, "reporter", out->reporter, sizeof(out->reporter));
    parse_string_field(json, "created", out->created, sizeof(out->created));
    parse_string_field(json, "modified", out->modified, sizeof(out->modified));
    return out->title[0] != '\0' && maint_category_ok(out->category) &&
           maint_priority_ok(out->priority);
}

bool maint_store_create(MaintItem *item)
{
    if (!g_ready || item == nullptr) {
        return false;
    }
    if (item->title[0] == '\0' || !maint_category_ok(item->category) ||
        !maint_priority_ok(item->priority)) {
        return false;
    }
    new_id(item->id);
    item->wp_id = 0;
    item->sailboat_id = LMYC_SAILBOAT_ID;
    if (item->status[0] == '\0') {
        snprintf(item->status, sizeof(item->status), "reported");
    }
    snprintf(item->sync_state, sizeof(item->sync_state), "pending");
    item->photo_count = 0;
    return maint_store_save(item);
}

static const char *find_key(const char *json, const char *key)
{
    char pat[40];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(json, pat);
    if (p == nullptr) {
        return nullptr;
    }
    p += strlen(pat);
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p != ':') {
        return nullptr;
    }
    ++p;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    return p;
}

static bool parse_string_field(const char *json, const char *key, char *out, size_t cap)
{
    const char *p = find_key(json, key);
    if (p == nullptr || *p != '"') {
        out[0] = '\0';
        return false;
    }
    ++p;
    size_t n = 0;
    while (*p != '\0' && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p != '\0') {
            const char e = *p++;
            c = (e == 'n') ? '\n' : e;
        }
        if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return true;
}

static uint32_t parse_u32_field(const char *json, const char *key)
{
    const char *p = find_key(json, key);
    if (p == nullptr) {
        return 0;
    }
    return static_cast<uint32_t>(strtoul(p, nullptr, 10));
}

bool maint_store_load(const char *id, MaintItem *out)
{
    if (!g_ready || out == nullptr || !maint_id_ok(id)) {
        return false;
    }
    char path[56];
    if (!item_json_path(id, path, sizeof(path))) {
        return false;
    }
    File in = SD.open(path, FILE_READ);
    if (!in) {
        return false;
    }
    char *json = static_cast<char *>(malloc(8192));
    if (json == nullptr) {
        in.close();
        return false;
    }
    const size_t n = in.read(reinterpret_cast<uint8_t *>(json), 8191);
    in.close();
    json[n] = '\0';
    memset(out, 0, sizeof(*out));
    parse_string_field(json, "id", out->id, sizeof(out->id));
    if (!maint_id_ok(out->id)) {
        snprintf(out->id, sizeof(out->id), "%s", id);
    }
    out->wp_id = parse_u32_field(json, "wp_id");
    out->sailboat_id = parse_u32_field(json, "sailboat_id");
    parse_string_field(json, "title", out->title, sizeof(out->title));
    parse_string_field(json, "description", out->description, sizeof(out->description));
    parse_string_field(json, "category", out->category, sizeof(out->category));
    parse_string_field(json, "priority", out->priority, sizeof(out->priority));
    parse_string_field(json, "status", out->status, sizeof(out->status));
    parse_string_field(json, "reporter", out->reporter, sizeof(out->reporter));
    parse_string_field(json, "created", out->created, sizeof(out->created));
    parse_string_field(json, "modified", out->modified, sizeof(out->modified));
    parse_string_field(json, "sync", out->sync_state, sizeof(out->sync_state));
    out->photo_count = static_cast<uint8_t>(parse_u32_field(json, "photo_count"));
    free(json);
    return out->title[0] != '\0';
}

bool maint_store_list_start(void)
{
    maint_store_list_end();
    if (!g_ready) {
        return false;
    }
    g_list_dir = SD.open("/maint");
    return static_cast<bool>(g_list_dir);
}

bool maint_store_list_next(MaintItem *out)
{
    if (!g_list_dir) {
        return false;
    }
    for (;;) {
        File entry = g_list_dir.openNextFile();
        if (!entry) {
            return false;
        }
        const bool dir = entry.isDirectory();
        char path_copy[64];
        path_copy[0] = '\0';
        if (entry.path() != nullptr) {
            snprintf(path_copy, sizeof(path_copy), "%s", entry.path());
        }
        entry.close();
        if (!dir || path_copy[0] == '\0') {
            continue;
        }
        const char *slash = strrchr(path_copy, '/');
        const char *id = (slash != nullptr) ? slash + 1 : path_copy;
        if (maint_store_load(id, out)) {
            return true;
        }
    }
}

void maint_store_list_end(void)
{
    if (g_list_dir) {
        g_list_dir.close();
    }
}

bool maint_store_photo_path(const char *id, uint8_t n, char *out, size_t cap)
{
    char dir[40];
    if (n == 0 || n > kMaintPhotoMax || !item_dir(id, dir, sizeof(dir))) {
        return false;
    }
    if (snprintf(out, cap, "%s/%u.jpg", dir, static_cast<unsigned>(n)) >= static_cast<int>(cap)) {
        return false;
    }
    return SD.exists(out);
}

bool maint_store_photo_begin(const char *id, uint8_t n)
{
    maint_store_photo_abort();
    MaintItem item;
    char dir[40];
    if (!maint_store_load(id, &item) || n == 0 || n > kMaintPhotoMax ||
        !item_dir(id, dir, sizeof(dir))) {
        return false;
    }
    snprintf(g_photo_tmp, sizeof(g_photo_tmp), "%s/%u.tmp", dir, static_cast<unsigned>(n));
    snprintf(g_photo_dst, sizeof(g_photo_dst), "%s/%u.jpg", dir, static_cast<unsigned>(n));
    snprintf(g_photo_id, sizeof(g_photo_id), "%s", id);
    g_photo_n = n;
    SD.remove(g_photo_tmp);
    g_photo = SD.open(g_photo_tmp, FILE_WRITE);
    return static_cast<bool>(g_photo);
}

bool maint_store_photo_write(const uint8_t *data, size_t n)
{
    if (!g_photo || data == nullptr || n == 0) {
        return false;
    }
    return g_photo.write(data, n) == n;
}

bool maint_store_photo_finish(void)
{
    if (!g_photo) {
        return false;
    }
    g_photo.close();
    SD.remove(g_photo_dst);
    if (!SD.rename(g_photo_tmp, g_photo_dst)) {
        SD.remove(g_photo_tmp);
        g_photo_n = 0;
        return false;
    }
    MaintItem item;
    if (!maint_store_load(g_photo_id, &item)) {
        g_photo_n = 0;
        return false;
    }
    if (item.photo_count < g_photo_n) {
        item.photo_count = g_photo_n;
        maint_store_save(&item);
    }
    g_photo_n = 0;
    return true;
}

void maint_store_photo_abort(void)
{
    if (g_photo) {
        g_photo.close();
        if (g_photo_tmp[0] != '\0') {
            SD.remove(g_photo_tmp);
        }
    }
    g_photo_n = 0;
    g_photo_tmp[0] = '\0';
}
