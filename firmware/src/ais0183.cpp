#include "ais0183.h"

#include "nmea.h"

#include <cstdio>
#include <cstring>

void AisBits::clear(void)
{
    memset(data, 0, sizeof(data));
    nbits = 0;
}

void AisBits::put(uint32_t value, int width)
{
    if (width <= 0 || width > 32) {
        return;
    }
    for (int i = width - 1; i >= 0; --i) {
        if (nbits >= static_cast<int>(sizeof(data) * 8)) {
            return;
        }
        const int bit = (value >> i) & 1;
        data[nbits / 8] |= static_cast<uint8_t>(bit << (7 - (nbits % 8)));
        ++nbits;
    }
}

void AisBits::put_signed(int32_t value, int width)
{
    if (width <= 0 || width > 32) {
        return;
    }
    const uint32_t mask = (width == 32) ? 0xffffffffu : ((1u << width) - 1u);
    put(static_cast<uint32_t>(value) & mask, width);
}

static uint8_t sixbit(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    /* ITU 6-bit ASCII: '@' is 0, 'A' is 1, space stays 32. */
    if (static_cast<uint8_t>(c) >= 64) {
        c = static_cast<char>(c - 64);
    }
    return static_cast<uint8_t>(c) & 0x3f;
}

void AisBits::put_text(const char *text, int nchars)
{
    for (int i = 0; i < nchars; ++i) {
        const char c = (text != nullptr && text[i] != '\0') ? text[i] : '@';
        put(sixbit(c), 6);
    }
}

static char arm(uint8_t six)
{
    char c = static_cast<char>(six + 48);
    if (c > 87) {
        c = static_cast<char>(c + 8);
    }
    return c;
}

int ais_finish(const AisBits &bits, char channel, bool own, char out[][84], int max_out)
{
    if (out == nullptr || max_out <= 0 || bits.nbits <= 0) {
        return 0;
    }
    if (channel != 'B') {
        channel = 'A';
    }

    const int fill = (6 - (bits.nbits % 6)) % 6;
    const int nchars = (bits.nbits + fill) / 6;
    if (nchars <= 0 || nchars > 96) {
        return 0;
    }

    char payload[97];
    for (int i = 0; i < nchars; ++i) {
        uint8_t v = 0;
        for (int b = 0; b < 6; ++b) {
            const int idx = i * 6 + b;
            int bit = 0;
            if (idx < bits.nbits) {
                bit = (bits.data[idx / 8] >> (7 - (idx % 8))) & 1;
            }
            v = static_cast<uint8_t>((v << 1) | bit);
        }
        payload[i] = arm(v);
    }
    payload[nchars] = '\0';

    /* Leave room for "!AIVDM,2,1,0,A," + ",0*HH" inside 82 characters. */
    constexpr int kChunk = 56;
    int parts = (nchars + kChunk - 1) / kChunk;
    if (parts < 1) {
        parts = 1;
    }
    if (parts > max_out) {
        parts = max_out;
    }

    static uint8_t seq = 0;
    const char seq_digit = static_cast<char>('0' + (seq % 10));
    if (parts > 1) {
        seq = static_cast<uint8_t>((seq + 1) % 10);
    }
    const char *talker = own ? "!AIVDO" : "!AIVDM";

    int written = 0;
    for (int part = 0; part < parts; ++part) {
        const int off = part * kChunk;
        int len = nchars - off;
        if (len > kChunk) {
            len = kChunk;
        }
        if (len <= 0) {
            break;
        }
        const int this_fill = (part == parts - 1) ? fill : 0;
        char body[80];
        if (parts == 1) {
            snprintf(body, sizeof(body), "%s,1,1,,%c,%.*s,%d", talker, channel, len, payload + off,
                     this_fill);
        } else {
            snprintf(body, sizeof(body), "%s,%d,%d,%c,%c,%.*s,%d", talker, parts, part + 1, seq_digit,
                     channel, len, payload + off, this_fill);
        }
        nmea_append_checksum(body, sizeof(body));
        if (strlen(body) > kNmeaMaxSentence) {
            continue;
        }
        snprintf(out[written], 84, "%s", body);
        ++written;
    }
    return written;
}
