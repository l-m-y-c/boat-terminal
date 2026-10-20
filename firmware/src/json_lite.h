#pragma once

#include <cstddef>
#include <cstdint>

/* Flat JSON objects for the maintenance book: item.json on the card and the
 * bodies the phone posts. No Arduino dependency, so the host test covers it.
 *
 * Only top-level keys of the outer object are matched. A key name that
 * appears as a value, or inside a nested object, is skipped. */

/* Pointer to the value of key, or nullptr. */
const char *json_find(const char *json, const char *key);

/* Decodes \" \\ \/ \b \f \n \r \t and \uXXXX (surrogate pairs too) into
 * UTF-8. Truncates at cap - 1 bytes without splitting a UTF-8 character.
 * out is "" and the result false when the key is absent or not a string. */
bool json_get_string(const char *json, const char *key, char *out, size_t cap);

/* 0 when absent or not a number. */
uint32_t json_get_u32(const char *json, const char *key);

/* Writers. Each returns false, leaving *used unchanged past cap, when the
 * text does not fit. Strings are quoted and escaped. CR and other control
 * characters except LF are dropped. */
bool json_append_ch(char *dst, size_t cap, size_t *used, char c);
bool json_append_raw(char *dst, size_t cap, size_t *used, const char *s);
bool json_append_string(char *dst, size_t cap, size_t *used, const char *s);
