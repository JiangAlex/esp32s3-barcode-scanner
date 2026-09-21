/**
 * @file nvs_settings.h
 * @brief NVS-based persistent settings using Preferences library
 */

#ifndef NVS_SETTINGS_H
#define NVS_SETTINGS_H

#include <Arduino.h>

// NVS namespace for scanner settings
#define NVS_NAMESPACE "scanner"

// Keys
#define NVS_KEY_WIFI_SSID     "wifi_ssid"
#define NVS_KEY_WIFI_PASS     "wifi_pass"
#define NVS_KEY_MQTT_BROKER   "mqtt_broker"
#define NVS_KEY_MQTT_PORT     "mqtt_port"
#define NVS_KEY_DEVICE_ID     "device_id"
#define NVS_KEY_BRIGHTNESS    "brightness"
#define NVS_KEY_LANGUAGE      "language"

/**
 * @brief Initialize NVS settings namespace.
 *        Call once during setup().
 */
void nvs_init(void);

/**
 * @brief Get a string value from NVS.
 * @param key Key name
 * @param out Buffer to store result
 * @param max_len Maximum bytes to write to out (including null terminator)
 * @return true if key existed and value was written, false otherwise
 */
bool nvs_get_str(const char* key, char* out, size_t max_len);

/**
 * @brief Set a string value in NVS.
 * @param key Key name
 * @param value String value to store
 * @return true on success, false on failure
 */
bool nvs_set_str(const char* key, const char* value);

/**
 * @brief Get an integer value from NVS.
 * @param key Key name
 * @param out Pointer to store the result
 * @return true if key existed and value was read, false otherwise
 */
bool nvs_get_int(const char* key, int32_t* out);

/**
 * @brief Set an integer value in NVS.
 * @param key Key name
 * @param value Integer value to store
 * @return true on success, false on failure
 */
bool nvs_set_int(const char* key, int32_t value);

/**
 * @brief Check if a key exists in NVS.
 * @param key Key name
 * @return true if key exists, false otherwise
 */
bool nvs_key_exists(const char* key);

#endif /* NVS_SETTINGS_H */
