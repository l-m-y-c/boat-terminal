/* Host test: the flat JSON reader and writer behind item.json and the
 * maintenance POST bodies. No board, no card. */

#include "json_lite.h"

#include <cstdio>
#include <cstring>
#include <string>

static int g_fail = 0;
static int g_pass = 0;

static void expect(bool ok, const char *what)
{
    if (ok) {
        ++g_pass;
    } else {
        ++g_fail;
        printf("FAIL: %s\n", what);
    }
}

static std::string get(const char *json, const char *key, size_t cap = 64)
{
    char out[256];
    if (!json_get_string(json, key, out, cap)) {
        return "<absent>";
    }
    return out;
}

static bool valid_utf8(const char *s)
{
    const auto *p = reinterpret_cast<const unsigned char *>(s);
    while (*p != 0) {
        int n = 0;
        if (*p < 0x80) {
            n = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            n = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            n = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            n = 3;
        } else {
            return false;
        }
        ++p;
        for (int i = 0; i < n; ++i, ++p) {
            if ((*p & 0xC0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

int main()
{
    expect(get(R"({"title":"Bilge pump"})", "title") == "Bilge pump", "plain string");
    expect(get(R"( { "title" : "x" , "a":1 } )", "title") == "x", "whitespace");
    expect(get(R"({"a":1})", "title") == "<absent>", "absent key");
    expect(get(R"({"title":null})", "title") == "<absent>", "null is not a string");

    /* A key name used as a value must not hide the real key after it. */
    expect(get(R"({"description":"title","title":"Real"})", "title") == "Real",
           "key name as an earlier value");
    expect(get(R"({"note":"\"title\": fake","title":"Real"})", "title") == "Real",
           "escaped key text inside a value");
    expect(get(R"({"meta":{"title":"nested"},"title":"Top"})", "title") == "Top",
           "nested object skipped");
    expect(get(R"({"list":["title",{"title":"x"}],"title":"Top"})", "title") == "Top",
           "array skipped");
    expect(get(R"({"sailboat_id":5,"id":"abc"})", "id") == "abc", "id vs sailboat_id");

    /* Escapes. */
    expect(get(R"({"d":"a\nb\tc\rd"})", "d") == "a\nb\tc\rd", "\\n \\t \\r");
    expect(get(R"({"d":"q\"b\\s\/"})", "d") == "q\"b\\s/", "quote backslash slash");
    expect(get(R"({"d":"café"})", "d") == "caf\xC3\xA9", "\\u00e9 to UTF-8");
    expect(get(R"({"d":"€"})", "d") == "\xE2\x82\xAC", "\\u20ac three bytes");
    expect(get(R"({"d":"🚤"})", "d") == "\xF0\x9F\x9A\xA4", "surrogate pair");
    expect(get(R"({"d":"\ud83d"})", "d") == "\xEF\xBF\xBD", "lone surrogate");
    expect(get(R"({"d":"\u"})", "d") == "", "truncated unicode escape");
    expect(get(R"({"d":"caf\u00e9"})", "d") == "caf\xC3\xA9", "unicode escape e9");
    expect(get(R"({"d":"\u20ac"})", "d") == "\xE2\x82\xAC", "unicode escape euro");
    expect(get(R"({"d":"\uD83D\uDEA4"})", "d") == "\xF0\x9F\x9A\xA4", "surrogate pair escape");
    expect(get("{\"d\":\"caf\xC3\xA9\"}", "d") == "caf\xC3\xA9", "raw UTF-8 kept");

    /* Truncation keeps whole characters. "é" is 2 bytes, "€" is 3. */
    {
        const char *json = "{\"d\":\"ab\xC3\xA9\xC3\xA9\"}";
        const std::string s = get(json, "d", 4); /* room for 3 bytes */
        expect(s == "ab", "2-byte char not split");
        expect(valid_utf8(s.c_str()), "2-byte truncation valid");
    }
    {
        const char *json = R"({"d":"a€€"})";
        const std::string s = get(json, "d", 6); /* room for 5 bytes */
        expect(s == "a\xE2\x82\xAC", "3-byte char not split");
        expect(valid_utf8(s.c_str()), "3-byte truncation valid");
    }
    {
        /* Stops at the first char that does not fit. A later short char
         * must not be appended after a dropped one. */
        const char *json = "{\"d\":\"a\xE2\x82\xAC" "b\"}";
        expect(get(json, "d", 3) == "a", "no gap after a dropped char");
    }

    /* Numbers. */
    expect(json_get_u32(R"({"wp_id":4321})", "wp_id") == 4321, "u32");
    expect(json_get_u32(R"({"wp_id" : 7 })", "wp_id") == 7, "u32 whitespace");
    expect(json_get_u32(R"({"wp_id":"12"})", "wp_id") == 0, "string is not a number");
    expect(json_get_u32(R"({"photo_count":2,"wp_id":0})", "wp_id") == 0, "zero");
    expect(json_get_u32(R"({"a":1})", "wp_id") == 0, "absent number");

    /* Writer round trip. */
    {
        char buf[128];
        size_t n = 0;
        buf[0] = '\0';
        bool ok = json_append_ch(buf, sizeof(buf), &n, '{');
        ok = ok && json_append_string(buf, sizeof(buf), &n, "t");
        ok = ok && json_append_ch(buf, sizeof(buf), &n, ':');
        ok = ok && json_append_string(buf, sizeof(buf), &n, "a\"b\\c\nd\re\x01" "caf\xC3\xA9");
        ok = ok && json_append_ch(buf, sizeof(buf), &n, '}');
        expect(ok, "writer fits");
        expect(strcmp(buf, "{\"t\":\"a\\\"b\\\\c\\nde" "caf\xC3\xA9\"}") == 0, "writer escapes");
        expect(get(buf, "t") == "a\"b\\c\nde" "caf\xC3\xA9", "round trip");
    }
    {
        char buf[6];
        size_t n = 0;
        buf[0] = '\0';
        expect(!json_append_string(buf, sizeof(buf), &n, "toolong"), "writer reports overflow");
        expect(n < sizeof(buf) && buf[n] == '\0', "overflow stays terminated");
    }

    printf("json_lite: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
