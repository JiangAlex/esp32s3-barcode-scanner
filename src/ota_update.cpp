/**
 * @file ota_update.cpp
 * @brief OTA firmware update via HTTP
 * 
 * Uses ESP32 HTTPUpdate via esp_https_ota or ArduinoOTA.
 * Version endpoint: GET http://192.168.1.100/api/firmware/version
 * Response: {"version": "0.2.0"}
 */

#include "ota_update.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include "config/config.h"

// OTA server configuration
#define OTA_VERSION_URL   "http://192.168.1.100/api/firmware/version"
#define OTA_FW_URL        "http://192.168.1.100/api/firmware/firmware.bin"

static const char* TAG = "OTA";

// Semver comparison: returns true if a > b
static bool version_greater(const char* a, const char* b) {
    int amaj = 0, amin = 0, apat = 0;
    int bmaj = 0, bmin = 0, bpat = 0;
    sscanf(a, "%d.%d.%d", &amaj, &amin, &apat);
    sscanf(b, "%d.%d.%d", &bmaj, &bmin, &bpat);
    if (amaj != bmaj) return amaj > bmaj;
    if (amin != bmin) return amin > bmin;
    return apat > bpat;
}

bool ota_begin_update(const char* firmware_url) {
    Serial.printf("[OTA] Starting update from: %s\n", firmware_url);

    HTTPClient http;
    http.begin(firmware_url);
    http.setTimeout(30000);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    int contentLen = http.getSize();
    if (contentLen <= 0) {
        Serial.println("[OTA] Unknown content length");
        http.end();
        return false;
    }
    Serial.printf("[OTA] Firmware size: %d bytes\n", contentLen);

    // Notify progress to serial
    Serial.printf("[OTA] Update starting...\n");

    // Perform update; Write object as the target partition
    if (!Update.begin(contentLen)) {
        Serial.println("[OTA] Update begin failed");
        http.end();
        return false;
    }

    // Stream the firmware data
    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    uint32_t last_report = 0;

    while (http.connected() && (written < contentLen)) {
        size_t available = stream->available();
        if (available > 0) {
            uint8_t buf[512];
            size_t read = stream->readBytes(buf, min(available, sizeof(buf)));
            written += Update.write(buf, read);

            if (written - last_report >= 8192) {
                Serial.printf("[OTA] Progress: %u / %d bytes (%.1f%%)\n",
                              (unsigned)written, contentLen,
                              100.0f * written / contentLen);
                last_report = written;
            }
        }
        delay(1);
    }

    http.end();

    if (Update.end(true)) {
        Serial.printf("[OTA] Update complete. Rebooting...\n");
        delay(500);
        ESP.restart();
        return true;
    } else {
        Serial.printf("[OTA] Update failed: %s\n", Update.errorString());
        return false;
    }
}

bool ota_check_and_update(void) {
    Serial.println("[OTA] Checking for updates...");

    // Ensure WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] WiFi not connected, skipping OTA check");
        return false;
    }

    HTTPClient http;
    http.begin(OTA_VERSION_URL);
    http.setTimeout(10000);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[OTA] Version check HTTP error: %d\n", httpCode);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    Serial.printf("[OTA] Version response: %s\n", payload.c_str());

    // Parse JSON: {"version": "0.2.0"}
    // Simple parsing without ArduinoJson to keep binary size small
    const char* verStart = strstr(payload.c_str(), "\"version\"");
    if (!verStart) {
        Serial.println("[OTA] No 'version' field found");
        return false;
    }
    verStart = strchr(verStart, ':');
    if (!verStart) return false;
    verStart++;
    while (*verStart == ' ' || *verStart == '"') verStart++;
    char server_version[32] = {0};
    int i = 0;
    while (i < sizeof(server_version) - 1 && verStart[i] && verStart[i] != '"' && verStart[i] != '}') {
        server_version[i] = verStart[i];
        i++;
    }
    server_version[i] = '\0';

    Serial.printf("[OTA] Server version: %s, Firmware version: %s\n", server_version, FW_VERSION);

    if (version_greater(server_version, FW_VERSION)) {
        Serial.println("[OTA] Newer version available! Downloading...");
        return ota_begin_update(OTA_FW_URL);
    } else {
        Serial.println("[OTA] Firmware is up to date.");
        return false;
    }
}
