#pragma once
/* Host stand-in for ESP32 Preferences (NVS): one in-memory store shared by
 * every instance, so a test can "restart" and read back what was saved. */
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

struct PrefsStore {
    std::map<std::string, std::string> values;
    int writes = 0;
    static PrefsStore &get(void)
    {
        static PrefsStore store;
        return store;
    }
};

class Preferences {
public:
    bool begin(const char *space, bool /*read_only*/ = false)
    {
        space_ = space;
        return true;
    }
    void end(void) {}
    bool isKey(const char *key) { return PrefsStore::get().values.count(k(key)) != 0; }
    uint8_t getUChar(const char *key, uint8_t def = 0)
    {
        auto &v = PrefsStore::get().values;
        auto it = v.find(k(key));
        return it == v.end() || it->second.empty() ? def : static_cast<uint8_t>(it->second[0]);
    }
    size_t putUChar(const char *key, uint8_t value)
    {
        PrefsStore::get().values[k(key)] = std::string(1, static_cast<char>(value));
        ++PrefsStore::get().writes;
        return 1;
    }
    size_t getString(const char *key, char *out, size_t cap)
    {
        auto &v = PrefsStore::get().values;
        auto it = v.find(k(key));
        if (it == v.end() || cap == 0) {
            return 0;
        }
        snprintf(out, cap, "%s", it->second.c_str());
        return strlen(out);
    }
    size_t putString(const char *key, const char *value)
    {
        PrefsStore::get().values[k(key)] = value;
        ++PrefsStore::get().writes;
        return strlen(value);
    }

private:
    std::string k(const char *key) const { return space_ + "/" + key; }
    std::string space_;
};
