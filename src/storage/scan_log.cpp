/**
 * @file scan_log.cpp
 * @brief Scan history logging implementation (JSON Lines, date-based, with offline queue)
 */

#include "scan_log.h"
#include <SD.h>
#include <ArduinoJson.h>
#include "sd_card.h"
#include "config/config.h"
#include "network/mqtt_client.h"

// ─── Date helpers ──────────────────────────────────────────────────────────────

static void get_date_str(char* buf, size_t len) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", t);
}

// Date string for a day offset relative to today (0=today, -1=yesterday...).
void scan_log_date_for_offset(int day_offset, char* buf, size_t len) {
    time_t now = time(nullptr) + (time_t)day_offset * 86400;
    struct tm* t = localtime(&now);
    strftime(buf, len, "%Y-%m-%d", t);
}

// Log file path. NOTE: this card/library combo cannot create directories
// (SD.mkdir fails on all names, while root-level file writes succeed — verified
// on hardware 2026-10-20), so the work log lives in flat root-level files:
//   /log_YYYY-MM-DD.json   per-day log
//   /pending_queue.json    offline sync queue
static void log_file_path(char* buf, size_t len) {
    char date[16];
    get_date_str(date, sizeof(date));
    snprintf(buf, len, "/log_%s.json", date);
}

static void pending_file_path(char* buf, size_t len) {
    snprintf(buf, len, "/pending_queue.json");
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

    // Work-log files live at the card root (no subdirectory — this card cannot
    // create directories; see log_file_path). Nothing to mkdir here.

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

// Common writer: build a JSON line {ts,type,content} and append to today's log
// file. Shared by scan_log_add (DecodeResult) and scan_log_add_entry (strings).
// NOTE: this writes the work-log only. The offline sync queue is written
// SEPARATELY via scan_log_queue_pending() — and only for state-changing IN/OUT
// scans done while offline — so we don't try to replay queries/inputs later.
static void scan_log_write(const char* type, const char* content) {
    if (!sd_card_is_mounted()) return;
    if (!content || !content[0]) return;

    if (_total_count >= SCAN_LOG_MAX_ENTRIES) {
        Serial.println("[LOG] Max entries reached, skipping");
        return;
    }

    time_t now = time(nullptr);
    char time_str[32];
    struct tm* t = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", t);

    char json[512];
    snprintf(json, sizeof(json),
        "{\"ts\":\"%s\",\"type\":\"%s\",\"content\":\"%s\"}\n",
        time_str, type ? type : "", content);

    // Append to today's log
    char path[64];
    log_file_path(path, sizeof(path));
    if (sd_card_write_file(path, json)) {
        _total_count++;
        Serial.printf("[LOG] #%u: %s\n", _total_count, content);
    }
}

void scan_log_add(const DecodeResult& result) {
    if (!result.success) return;
    scan_log_write(barcode_type_to_string(result.type), result.content.c_str());
}

void scan_log_add_entry(const char* type, const char* content) {
    scan_log_write(type, content);
}

// Queue an offline IN/OUT scan for later replay. Writes {type,content} to the
// pending file. Only call when offline at scan time.
void scan_log_queue_pending(const char* type, const char* content) {
    if (!sd_card_is_mounted() || !content || !content[0]) return;
    char json[256];
    snprintf(json, sizeof(json),
        "{\"type\":\"%s\",\"content\":\"%s\"}\n", type ? type : "", content);
    char path[64];
    pending_file_path(path, sizeof(path));
    if (sd_card_write_file(path, json)) {
        _pending_count++;
        Serial.printf("[LOG] queued pending (%s %s), total=%u\n",
                      type, content, _pending_count);
    }
}

// ─── Public: log read ─────────────────────────────────────────────────────────

String scan_log_read(const char* date, uint32_t offset, uint32_t limit) {
    if (!sd_card_is_mounted()) return "[]";

    char path[64];
    if (date && date[0]) {
        snprintf(path, sizeof(path), "/log_%s.json", date);
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
    if (!sd_card_is_mounted()) return 0;
    if (!mqtt_is_connected()) {
        Serial.println("[LOG] MQTT not connected, cannot sync pending");
        return 0;
    }

    char path[64];
    pending_file_path(path, sizeof(path));
    String content = sd_card_read_file(path);
    if (content.length() == 0) return 0;

    Serial.println("[LOG] syncing pending offline scans...");

    // Replay each queued line to its per-item topic (IN → stock/in, OUT →
    // stock/out). Lines that publish OK are dropped; any that fail to publish
    // are kept in the rewritten file for the next retry. This replaces the old
    // batch no-op that the server rejected.
    String kept = "";
    uint32_t sent = 0, keptCount = 0;
    int start = 0;
    for (int i = 0; i <= (int)content.length(); i++) {
        if (i == (int)content.length() || content[i] == '\n') {
            if (i > start) {
                String line = content.substring(start, i);
                StaticJsonDocument<256> doc;
                bool ok = false;
                if (deserializeJson(doc, line) == DeserializationError::Ok) {
                    const char* type    = doc["type"]    | "";
                    const char* barcode = doc["content"] | "";
                    if (barcode[0]) {
                        if (strcmp(type, "IN") == 0) {
                            mqtt_stock_in(barcode, 1);  ok = true;
                        } else if (strcmp(type, "OUT") == 0) {
                            mqtt_stock_out(barcode, 1); ok = true;
                        } else {
                            ok = true;  // unknown/legacy type → drop (not replayable)
                        }
                    }
                }
                if (ok) {
                    sent++;
                } else {
                    kept += line; kept += "\n"; keptCount++;
                }
            }
            start = i + 1;
        }
    }

    // Rewrite the pending file with only the lines we could not send.
    SD.remove(path);
    if (kept.length() > 0) {
        File f = SD.open(path, FILE_WRITE);
        if (f) { f.print(kept); f.flush(); f.close(); }
    }
    _pending_count = keptCount;
    Serial.printf("[LOG] pending sync: replayed=%u kept=%u\n", sent, keptCount);
    return sent;
}
