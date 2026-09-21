/**
 * @file scan_log.cpp
 * @brief Scan history logging implementation (JSON Lines, date-based, with offline queue)
 */

#include "scan_log.h"
#include <SD.h>
#include "sd_card.h"
#include "config/config.h"
#include "network/mqtt_client.h"

// ─── Date helpers ──────────────────────────────────────────────────────────────

static void get_date_str(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", t);
}

// Returns today's date file path: /sd/logs/YYYY-MM-DD.json
static void log_file_path(char* buf, size_t len) {
    char date[16];
    get_date_str(date, sizeof(date));
    snprintf(buf, len, "%s/%s.json", SD_LOG_DIR, date);
}

// Returns pending queue file path: /sd/logs/pending/queue.json
static void pending_file_path(char* buf, size_t len) {
    snprintf(buf, len, "%s/pending/queue.json", SD_LOG_DIR);
}

// ─── State ────────────────────────────────────────────────────────────────────

static uint32_t _total_count = 0;   // total across all log files
static uint32_t _pending_count = 0; // pending queue size

// ─── Public: init ──────────────────────────────────────────────────────────────

void scan_log_init(void) {
    if (!sd_card_is_mounted()) {
        Serial.println("[LOG] SD card not available, logging disabled");
        return;
    }

    // Ensure directories exist
    sd_card_mkdir(SD_LOG_DIR);
    {
        char p[64];
        snprintf(p, sizeof(p), "%s/pending", SD_LOG_DIR);
        sd_card_mkdir(p);
    }

    // Count existing entries across all date files
    // Simple approach: just count today's file (full date-dir enumeration is expensive)
    char path[64];
    log_file_path(path, sizeof(path));
    String content = sd_card_read_file(path);
    for (size_t i = 0; i < content.length(); i++) {
        if (content[i] == '\n') _total_count++;
    }

    // Count pending
    pending_file_path(path, sizeof(path));
    content = sd_card_read_file(path);
    for (size_t i = 0; i < content.length(); i++) {
        if (content[i] == '\n') _pending_count++;
    }

    Serial.printf("[LOG] Init: total=%u pending=%u\n", _total_count, _pending_count);
}

// ─── Public: log add ──────────────────────────────────────────────────────────

void scan_log_add(const DecodeResult& result) {
    if (!sd_card_is_mounted()) return;
    if (!result.success) return;

    if (_total_count >= SCAN_LOG_MAX_ENTRIES) {
        Serial.println("[LOG] Max entries reached, skipping");
        return;
    }

    // Build JSON line with ISO timestamp
    char date_str[16];
    get_date_str(date_str, sizeof(date_str));

    time_t now = time(nullptr);
    char time_str[32];
    struct tm* t = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", t);

    // Build JSON line
    char json[512];
    snprintf(json, sizeof(json),
        "{\"ts\":\"%s\",\"type\":\"%s\",\"content\":\"%s\"}\n",
        time_str,
        barcode_type_to_string(result.type),
        result.content.c_str()
    );

    // Append to today's log
    char path[64];
    log_file_path(path, sizeof(path));
    if (sd_card_write_file(path, json)) {
        _total_count++;
        Serial.printf("[LOG] #%u: %s\n", _total_count, result.content.c_str());
    }

    // Also add to pending queue for sync
    pending_file_path(path, sizeof(path));
    sd_card_write_file(path, json);
    _pending_count++;
}

// ─── Public: log read ─────────────────────────────────────────────────────────

String scan_log_read(const char* date, uint32_t offset, uint32_t limit) {
    if (!sd_card_is_mounted()) return "[]";

    char path[64];
    if (date && date[0]) {
        snprintf(path, sizeof(path), "%s/%s.json", SD_LOG_DIR, date);
    } else {
        log_file_path(path, sizeof(path));
    }

    String content = sd_card_read_file(path);
    if (content.length() == 0) return "[]";

    // Parse JSON lines and build array (skip offset, take limit)
    String result = "[";
    uint32_t line_idx = 0;
    int start = 0;
    bool first = true;

    for (int i = 0; i <= (int)content.length(); i++) {
        if (i == (int)content.length() || content[i] == '\n') {
            if (i > start) {
                if (line_idx >= offset && line_idx < offset + limit) {
                    String line = content.substring(start, i);
                    if (!first) result += ",";
                    result += line;
                    first = false;
                }
                line_idx++;
            }
            start = i + 1;
        }
    }
    result += "]";
    return result;
}

// ─── Public: pending queue ─────────────────────────────────────────────────────

String scan_log_read_pending(void) {
    if (!sd_card_is_mounted()) return "[]";

    char path[64];
    pending_file_path(path, sizeof(path));
    String content = sd_card_read_file(path);
    if (content.length() == 0) return "[]";

    // Count total lines first
    uint32_t total = 0;
    for (size_t i = 0; i < content.length(); i++) {
        if (content[i] == '\n') total++;
    }
    if (total == 0) return "[]";

    // Build JSON array
    String result = "[";
    int start = 0;
    bool first = true;
    for (int i = 0; i <= (int)content.length(); i++) {
        if (i == (int)content.length() || content[i] == '\n') {
            if (i > start) {
                if (!first) result += ",";
                result += content.substring(start, i);
                first = false;
            }
            start = i + 1;
        }
    }
    result += "]";
    return result;
}

uint32_t scan_log_pending_count(void) {
    return _pending_count;
}

void scan_log_remove_pending(uint32_t idx) {
    if (!sd_card_is_mounted() || _pending_count == 0) return;

    char path[64];
    pending_file_path(path, sizeof(path));
    String content = sd_card_read_file(path);
    if (content.length() == 0) return;

    // Collect all lines except the one at idx
    String new_content = "";
    uint32_t line_idx = 0;
    int start = 0;
    bool first = true;

    for (int i = 0; i <= (int)content.length(); i++) {
        if (i == (int)content.length() || content[i] == '\n') {
            if (i > start) {
                if (line_idx != idx) {
                    String line = content.substring(start, i);
                    line += "\n";
                    new_content += line;
                } else {
                    // Skip this entry
                }
                line_idx++;
            }
            start = i + 1;
        }
    }

    // Overwrite file with new content
    SD.remove(path);
    if (new_content.length() > 0) {
        // Write back using sd_card_write_file (truncate + rewrite)
        File f = SD.open(path, FILE_WRITE);
        if (f) {
            f.print(new_content);
            f.close();
        }
    }

    if (_pending_count > 0) _pending_count--;
    Serial.printf("[LOG] Pending: removed idx=%u, remaining=%u\n", idx, _pending_count);
}

// ─── Public: clear ────────────────────────────────────────────────────────────

void scan_log_clear(void) {
    if (!sd_card_is_mounted()) return;

    char path[64];
    log_file_path(path, sizeof(path));
    SD.remove(path);
    _total_count = 0;
    Serial.println("[LOG] Today's log cleared");
}

// ─── Public: sync pending ─────────────────────────────────────────────────────

uint32_t scan_log_sync_pending(void) {
    uint32_t count = scan_log_pending_count();
    if (count == 0) return 0;

    if (!mqtt_is_connected()) {
        Serial.println("[LOG] MQTT not connected, cannot sync pending");
        return 0;
    }

    Serial.printf("[LOG] Syncing %u pending items...\n", count);

    // Use mqtt batch publish which reads pending entries and publishes them
    uint16_t published = mqtt_publish_inventory_batch();

    // After publish, clear the pending queue (items are now on server)
    // Rewrite the pending file to empty
    if (published > 0) {
        char path[64];
        pending_file_path(path, sizeof(path));
        SD.remove(path);
        _pending_count = 0;
        Serial.printf("[LOG] Pending queue cleared after sync (%u items)\n", published);
    }

    return published;
}
