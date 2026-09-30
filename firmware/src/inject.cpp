#include "inject.h"

#include <cstring>

static bool g_local = false;

void inject_init(void) { g_local = false; }
bool inject_local_armed(void) { return g_local; }
void inject_set_local(bool armed) { g_local = armed; }

bool inject_is_gnss_sentence(const char *sentence)
{
    if (sentence == nullptr || strlen(sentence) < 6) {
        return false;
    }
    if (sentence[0] != '$') {
        return false;
    }
    const char fmt[4] = {sentence[3], sentence[4], sentence[5], '\0'};
    return strcmp(fmt, "RMC") == 0 || strcmp(fmt, "GGA") == 0 ||
           strcmp(fmt, "VTG") == 0 || strcmp(fmt, "GLL") == 0;
}
