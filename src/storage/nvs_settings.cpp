/**
 * @file nvs_settings.cpp
 * @brief NVS-based persistent settings using Preferences library
 */

#include "nvs_settings.h"
#include <Preferences.h>

static Preferences g_prefs;

void nvs_init(void) {
    // Begin with read-write mode
    bool ok = g_prefs.begin(NVS_NAMESPACE, false);
    Serial.printf("[NVS] init: %s\n", ok ? "OK" : "FAILED");
}

bool nvs_get_str(const char* key, char* out, size_t max_len) {
    if (!out || max_len == 0) return false;
    out[0] = '\0';
    size_t len = g_prefs.getString(key, out, max_len);
    return len > 0;
}

bool nvs_set_str(const char* key, const char* value) {
    if (!key || !value) return false;
    return g_prefs.putString(key, value) > 0;
}

bool nvs_get_int(const char* key, int32_t* out) {
    if (!out) return false;
    *out = g_prefs.getInt(key, INT32_MIN);
    // Check if key existed (INT32_MIN is unlikely real value, but is a valid int)
    // We use a sentinel: store a known-bad value to detect missing keys.
    // Actually, getInt returns 0 on failure. But 0 might be a valid brightness.
    // Use getBytesLength to check existence.
    if (g_prefs.getBytesLength(key) == 0) return false;
    *out = g_prefs.getInt(key);
    return true;
}

bool nvs_set_int(const char* key, int32_t value) {
    if (!key) return false;
    return g_prefs.putInt(key, value) > 0;
}

bool nvs_key_exists(const char* key) {
    if (!key) return false;
    return g_prefs.getBytesLength(key) > 0;
}
