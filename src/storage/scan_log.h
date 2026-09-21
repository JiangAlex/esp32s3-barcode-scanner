/**
 * @file scan_log.h
 * @brief Scan history logging to SD card (JSON Lines, date-based files)
 *
 * File layout:
 *   /sd/logs/YYYY-MM-DD.json   — scan records for each day
 *   /sd/logs/pending/           — offline queue (unsent scans)
 */

#ifndef SCAN_LOG_H
#define SCAN_LOG_H

#include <Arduino.h>
#include "decoder/barcode_decoder.h"

/**
 * @brief Initialize scan log system (creates dirs, counts existing entries).
 */
void scan_log_init(void);

/**
 * @brief Log a scan result to today's log file.
 *        Also adds to pending/ queue for sync.
 * @param result Decode result to log
 */
void scan_log_add(const DecodeResult& result);

/**
 * @brief Get total number of logged scans (all files).
 */
uint32_t scan_log_count(void);

/**
 * @brief Read scan log as JSON array string.
 * @param date Date string "YYYY-MM-DD", or nullptr for today
 * @param offset Start index
 * @param limit Max entries to return
 * @return JSON array string of scan entries
 */
String scan_log_read(const char* date, uint32_t offset, uint32_t limit);

/**
 * @brief Read pending (offline) queue entries as JSON array.
 * @return JSON array string of pending scan entries
 */
String scan_log_read_pending(void);

/**
 * @brief Remove a specific entry from pending queue by index.
 * @param idx Index of entry to remove (from scan_log_pending_list)
 */
void scan_log_remove_pending(uint32_t idx);

/**
 * @brief Get number of pending (offline) entries.
 */
uint32_t scan_log_pending_count(void);

/**
 * @brief Clear all scan logs from SD card.
 */
void scan_log_clear(void);

/**
 * @brief Sync pending (offline) entries to MQTT server.
 *        Called by wifi_manager when WiFi reconnects.
 * @return number of items uploaded
 */
uint32_t scan_log_sync_pending(void);

#endif /* SCAN_LOG_H */
