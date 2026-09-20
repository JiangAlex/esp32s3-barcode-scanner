/**
 * @file scan_log.cpp
 * @brief Scan history logging implementation (JSON lines format on SD card)
 */

#include "scan_log.h"
#include <ArduinoJson.h>
#include <SD.h>
#include "sd_card.h"
#include "config/config.h"

static uint32_t _log_count = 0;

void scan_log_init(void) {
    if (!sd_card_is_mounted()) {
        Serial.println("[LOG] SD card not available, logging disabled");
        return;
    }

    // Ensure log directory exists
    sd_card_mkdir(SD_LOG_DIR);

    // Count existing log entries
    String content = sd_card_read_file(SCAN_LOG_FILE);
    if (content.length() > 0) {
        // Count newlines (each entry is one JSON line)
        for (size_t i = 0; i < content.length(); i++) {
            if (content[i] == '\n') _log_count++;
        }
    }

    Serial.printf("[LOG] Scan log initialized, %u existing entries\n", _log_count);
}

void scan_log_add(const DecodeResult& result) {
    if (!sd_card_is_mounted()) return;
    if (!result.success) return;

    // Check max entries
    if (_log_count >= SCAN_LOG_MAX_ENTRIES) {
        Serial.println("[LOG] Max entries reached, skipping");
        return;
    }

    // Create JSON line
    JsonDocument doc;
    doc["ts"] = millis();   // TODO: Use RTC or NTP time
    doc["type"] = barcode_type_to_string(result.type);
    doc["content"] = result.content;

    String line;
    serializeJson(doc, line);
    line += "\n";

    // Append to log file
    if (sd_card_write_file(SCAN_LOG_FILE, line.c_str())) {
        _log_count++;
        Serial.printf("[LOG] Logged scan #%u: %s\n", _log_count, result.content.c_str());
    }
}

uint32_t scan_log_count(void) {
    return _log_count;
}

String scan_log_read(uint32_t offset, uint32_t limit) {
    if (!sd_card_is_mounted()) return "[]";

    String content = sd_card_read_file(SCAN_LOG_FILE);
    if (content.length() == 0) return "[]";

    // Parse JSON lines and build array
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    uint32_t line_idx = 0;
    int start = 0;
    for (int i = 0; i <= (int)content.length(); i++) {
        if (i == (int)content.length() || content[i] == '\n') {
            if (i > start) {
                if (line_idx >= offset && line_idx < offset + limit) {
                    JsonDocument entry;
                    DeserializationError err = deserializeJson(entry, content.substring(start, i));
                    if (!err) {
                        arr.add(entry);
                    }
                }
                line_idx++;
            }
            start = i + 1;
        }
    }

    String result;
    serializeJson(arr, result);
    return result;
}

void scan_log_clear(void) {
    if (!sd_card_is_mounted()) return;

    // Overwrite with empty content
    SD.remove(SCAN_LOG_FILE);
    _log_count = 0;
    Serial.println("[LOG] Scan log cleared");
}
