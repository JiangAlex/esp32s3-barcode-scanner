/**
 * @file wifi_manager.h
 * @brief WiFi connection manager
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>

/**
 * @brief Initialize WiFi and connect to configured AP.
 */
void wifi_manager_init(void);

/**
 * @brief Check if WiFi is connected.
 */
bool wifi_is_connected(void);

/**
 * @brief Get current IP address as string.
 */
String wifi_get_ip(void);

/**
 * @brief Get WiFi signal strength (RSSI).
 */
int wifi_get_rssi(void);

/**
 * @brief Reconnect WiFi if disconnected.
 */
void wifi_reconnect(void);

#endif /* WIFI_MANAGER_H */
