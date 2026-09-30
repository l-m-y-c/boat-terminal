#include "nmea.h"

#include <cctype>
#include <cstdio>
#include <cstring>

static char g_acc[kNmeaMaxSentence + 4];
static size_t g_acc_len = 0;
static uint32_t g_good = 0;
static uint32_t g_bad = 0;
static NmeaSinkFn g_sink = nullptr;
static void *g_sink_ctx = nullptr;

void nmea_init(void)
{
    g_acc_len = 0;
    g_good = 0;
    g_bad = 0;
}

void nmea_set_sink(NmeaSinkFn fn, void *ctx)
{
    g_sink = fn;
    g_sink_ctx = ctx;
}

uint32_t nmea_good_count(void) { return g_good; }
uint32_t nmea_bad_count(void) { return g_bad; }

uint8_t nmea_checksum(const char *sentence)
{
    if (sentence == nullptr || sentence[0] == '\0') {
        return 0;
    }
    const char *p = sentence;
    if (*p == '$' || *p == '!') {
        ++p;
    }
    uint8_t cs = 0;
    while (*p != '\0' && *p != '*') {
        cs ^= static_cast<uint8_t>(*p++);
    }
    return cs;
}

void nmea_append_checksum(char *sentence, size_t cap)
{
    if (sentence == nullptr || cap < 4) {
        return;
    }
    const uint8_t cs = nmea_checksum(sentence);
    const size_t len = strlen(sentence);
    if (len + 3 >= cap) {
        return;
    }
    snprintf(sentence + len, cap - len, "*%02X", static_cast<unsigned>(cs));
}

static bool validate_and_emit(char *line)
{
    /* Trim CR/LF already stripped; reject empty / too short. */
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == ' ' || line[n - 1] == '\t')) {
        line[--n] = '\0';
    }
    if (n < 6 || (line[0] != '$' && line[0] != '!')) {
        ++g_bad;
        return false;
    }

    char *star = strrchr(line, '*');
    if (star == nullptr || star[1] == '\0' || star[2] == '\0') {
        ++g_bad;
        return false;
    }
    char hex[3] = {star[1], star[2], '\0'};
    char *end = nullptr;
    const unsigned long offered = strtoul(hex, &end, 16);
    if (end == hex || *end != '\0') {
        ++g_bad;
        return false;
    }

    /* Checksum covers chars between '$'/'!' and '*'. */
    *star = '\0';
    const uint8_t expect = nmea_checksum(line);
    *star = '*';

    if (expect != static_cast<uint8_t>(offered)) {
        ++g_bad;
        return false;
    }

    /* Truncate trailing junk after *HH */
    star[3] = '\0';
    n = static_cast<size_t>(star - line) + 3;

    ++g_good;
    if (g_sink != nullptr) {
        g_sink(line, n, g_sink_ctx);
    }
    return true;
}

bool nmea_feed_line(const char *line)
{
    if (line == nullptr) {
        return false;
    }
    char tmp[kNmeaMaxSentence + 4];
    size_t i = 0;
    for (; line[i] != '\0' && i + 1 < sizeof(tmp); ++i) {
        if (line[i] == '\r' || line[i] == '\n') {
            break;
        }
        tmp[i] = line[i];
    }
    tmp[i] = '\0';
    return validate_and_emit(tmp);
}

void nmea_feed_bytes(const uint8_t *data, size_t n)
{
    if (data == nullptr) {
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == '\r' || c == '\n') {
            if (g_acc_len > 0) {
                g_acc[g_acc_len] = '\0';
                validate_and_emit(g_acc);
                g_acc_len = 0;
            }
            continue;
        }
        if (g_acc_len + 1 >= sizeof(g_acc)) {
            /* Overflow — drop line. */
            g_acc_len = 0;
            ++g_bad;
            continue;
        }
        if (g_acc_len == 0 && c != '$' && c != '!') {
            continue;
        }
        g_acc[g_acc_len++] = c;
    }
}
