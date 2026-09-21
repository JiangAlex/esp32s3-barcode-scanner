/**
 * @file ota_update.h
 * @brief OTA firmware update via HTTP
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <Arduino.h>

/**
 * @brief Check for OTA update.
 * 
 * Connects to WiFi (if not already connected), fetches version from
 * OTA_VERSION_URL, compares with FW_VERSION, and applies update if newer.
 * 
 * @return true if update was applied (device will reboot), false otherwise.
 */
bool ota_check_and_update(void);

/**
 * @brief Begin OTA update with a given firmware URL.
 *        Called internally by ota_check_and_update().
 * 
 * @param firmware_url Full URL to the firmware binary (.bin)
 * @return true if update started successfully
 */
bool ota_begin_update(const char* firmware_url);

#endif /* OTA_UPDATE_H */
