#include "json_lite.h"

#include <cstdlib>
#include <cstring>

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    return p;
}

/* p is at the opening quote. Returns the char after the closing quote. */
static const char *skip_string(const char *p)
{
    ++p;
    while (*p != '\0' && *p != '"') {
        if (*p == '\\' && p[1] != '\0') {
            ++p;
        }
        ++p;
    }
    return (*p == '"') ? p + 1 : p;
}

/* Skips one value of any kind. Nested objects and arrays are skipped whole. */
static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"') {
        return skip_string(p);
    }
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p != '\0') {
            if (*p == '"') {
                p = skip_string(p);
                continue;
            }
            if (*p == '{' || *p == '[') {
                ++depth;
            } else if (*p == '}' || *p == ']') {
                --depth;
                if (depth == 0) {
                    return p + 1;
                }
            }
            ++p;
        }
        return p;
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') {
        ++p;
    }
    return p;
}

/* True when the quoted key at p equals key exactly. Keys here are ASCII. */
static bool key_equals(const char *p, const char *key)
{
    ++p;
    const size_t n = strlen(key);
    return strncmp(p, key, n) == 0 && p[n] == '"';
}

const char *json_find(const char *json, const char *key)
{
    if (json == nullptr || key == nullptr) {
        return nullptr;
    }
    const char *p = skip_ws(json);
    if (*p != '{') {
        return nullptr;
    }
    ++p;
    for (;;) {
        p = skip_ws(p);
        if (*p != '"') {
            return nullptr;
        }
        const bool match = key_equals(p, key);
        p = skip_ws(skip_string(p));
        if (*p != ':') {
            return nullptr;
        }
        p = skip_ws(p + 1);
        if (match) {
            return p;
        }
        p = skip_ws(skip_value(p));
        if (*p != ',') {
            return nullptr;
        }
        ++p;
    }
}

static int hex4(const char *p)
{
    int v = 0;
    for (int i = 0; i < 4; ++i) {
        const char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') {
            v |= c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v |= c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v |= c - 'A' + 10;
        } else {
            return -1;
        }
    }
    return v;
}

static size_t utf8_encode(uint32_t cp, char out[4])
{
    if (cp < 0x80) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

/* Bytes in the UTF-8 sequence that starts with lead. 1 for a stray byte. */
static size_t utf8_len(unsigned char lead)
{
    if (lead >= 0xF0 && lead < 0xF8) {
        return 4;
    }
    if (lead >= 0xE0) {
        return (lead < 0xF0) ? 3 : 1;
    }
    if (lead >= 0xC0) {
        return 2;
    }
    return 1;
}

bool json_get_string(const char *json, const char *key, char *out, size_t cap)
{
    if (out == nullptr || cap == 0) {
        return false;
    }
    out[0] = '\0';
    const char *p = json_find(json, key);
    if (p == nullptr || *p != '"') {
        return false;
    }
    ++p;
    size_t n = 0;
    bool full = false;
    while (*p != '\0' && *p != '"') {
        char buf[4];
        size_t len = 0;
        if (*p == '\\' && p[1] != '\0') {
            const char e = p[1];
            p += 2;
            switch (e) {
            case 'b': buf[0] = '\b'; len = 1; break;
            case 'f': buf[0] = '\f'; len = 1; break;
            case 'n': buf[0] = '\n'; len = 1; break;
            case 'r': buf[0] = '\r'; len = 1; break;
            case 't': buf[0] = '\t'; len = 1; break;
            case 'u': {
                int cp = hex4(p);
                if (cp < 0) {
                    break;
                }
                p += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && p[0] == '\\' && p[1] == 'u') {
                    const int lo = hex4(p + 2);
                    if (lo >= 0xDC00 && lo < 0xE000) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        p += 6;
                    }
                }
                if (cp >= 0xD800 && cp < 0xE000) {
                    cp = 0xFFFD; /* lone surrogate */
                }
                len = utf8_encode(static_cast<uint32_t>(cp), buf);
                break;
            }
            default: buf[0] = e; len = 1; break;
            }
        } else {
            len = utf8_len(static_cast<unsigned char>(*p));
            for (size_t i = 0; i < len; ++i) {
                if (p[i] == '\0' || (i > 0 && (static_cast<unsigned char>(p[i]) & 0xC0) != 0x80)) {
                    len = i;
                    break;
                }
                buf[i] = p[i];
            }
            if (len == 0) {
                len = 1;
                buf[0] = *p;
            }
            p += len;
        }
        /* Keep whole characters only once the buffer is full. */
        if (full || n + len >= cap) {
            full = true;
            continue;
        }
        memcpy(out + n, buf, len);
        n += len;
    }
    out[n] = '\0';
    return true;
}

uint32_t json_get_u32(const char *json, const char *key)
{
    const char *p = json_find(json, key);
    if (p == nullptr || *p < '0' || *p > '9') {
        return 0;
    }
    return static_cast<uint32_t>(strtoul(p, nullptr, 10));
}

bool json_append_ch(char *dst, size_t cap, size_t *used, char c)
{
    if (*used + 1 >= cap) {
        return false;
    }
    dst[(*used)++] = c;
    dst[*used] = '\0';
    return true;
}

bool json_append_raw(char *dst, size_t cap, size_t *used, const char *s)
{
    for (; *s != '\0'; ++s) {
        if (!json_append_ch(dst, cap, used, *s)) {
            return false;
        }
    }
    return true;
}

bool json_append_string(char *dst, size_t cap, size_t *used, const char *s)
{
    if (!json_append_ch(dst, cap, used, '"')) {
        return false;
    }
    for (; *s != '\0'; ++s) {
        const unsigned char c = static_cast<unsigned char>(*s);
        if (c == '"' || c == '\\') {
            if (!json_append_ch(dst, cap, used, '\\') ||
                !json_append_ch(dst, cap, used, static_cast<char>(c))) {
                return false;
            }
        } else if (c == '\n') {
            if (!json_append_raw(dst, cap, used, "\\n")) {
                return false;
            }
        } else if (c < 0x20) {
            continue;
        } else if (!json_append_ch(dst, cap, used, static_cast<char>(c))) {
            return false;
        }
    }
    return json_append_ch(dst, cap, used, '"');
}
