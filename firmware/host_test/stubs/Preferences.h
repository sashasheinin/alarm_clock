#pragma once
#include <map>
#include <string>
#include <vector>
#include <stdint.h>
#include <string.h>
extern std::map<std::string, std::vector<uint8_t>> g_nvs;
struct Preferences {
    bool begin(const char*, bool) { return true; }
    size_t putBytes(const char* k, const void* v, size_t n) { g_nvs[k].assign((const uint8_t*)v, (const uint8_t*)v + n); return n; }
    size_t getBytesLength(const char* k) { return g_nvs.count(k) ? g_nvs[k].size() : 0; }
    size_t getBytes(const char* k, void* v, size_t n) { memcpy(v, g_nvs[k].data(), n); return n; }
    size_t putInt(const char* k, int32_t v) { return putBytes(k, &v, 4); }
    int32_t getInt(const char* k, int32_t d) { if (!g_nvs.count(k)) return d; int32_t v; memcpy(&v, g_nvs[k].data(), 4); return v; }
    bool isKey(const char* k) { return g_nvs.count(k) > 0; }
    bool remove(const char* k) { return g_nvs.erase(k) > 0; }
};
