/**
 * @file wifi_manager.cpp
 * @brief WiFi connection manager implementation
 */

#include "wifi_manager.h"
#include <WiFi.h>
#include "config/config.h"
#include "storage/scan_log.h"

void wifi_manager_init(void) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.printf("[WIFI] Connecting to %s", WIFI_SSID);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("\n[WIFI] Connection timeout");
            return;
        }
        delay(500);
        Serial.print(".");
    }

    Serial.printf("\n[WIFI] Connected! IP: %s, RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());

    // Sync any pending offline scans now that WiFi is up
    scan_log_sync_pending();
}

bool wifi_is_connected(void) {
    return WiFi.status() == WL_CONNECTED;
}

String wifi_get_ip(void) {
    if (!wifi_is_connected()) return "N/A";
    return WiFi.localIP().toString();
}

int wifi_get_rssi(void) {
    if (!wifi_is_connected()) return 0;
    return WiFi.RSSI();
}

void wifi_reconnect(void) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Reconnecting...");
        WiFi.disconnect();
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("[WIFI] Reconnection failed");
                return;
            }
            delay(500);
        }
        Serial.printf("[WIFI] Reconnected, IP: %s\n", WiFi.localIP().toString().c_str());

        // Sync any pending offline scans now that WiFi is back
        scan_log_sync_pending();
    }
}
