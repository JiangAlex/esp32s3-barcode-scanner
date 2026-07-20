/**
 * @file scan_log.h
 * @brief Scan history logging to SD card (JSON format)
 */

#ifndef SCAN_LOG_H
#define SCAN_LOG_H

#include <Arduino.h>
#include "decoder/barcode_decoder.h"

/**
 * @brief Initialize scan log system.
 */
void scan_log_init(void);

/**
 * @brief Log a scan result to SD card.
 * @param result Decode result to log
 */
void scan_log_add(const DecodeResult& result);

/**
 * @brief Get total number of logged scans.
 */
uint32_t scan_log_count(void);

/**
 * @brief Read scan log as JSON string.
 * @param offset Start index
 * @param limit Max entries to return
 * @return JSON array string of scan entries
 */
String scan_log_read(uint32_t offset, uint32_t limit);

/**
 * @brief Clear all scan logs from SD card.
 */
void scan_log_clear(void);

#endif /* SCAN_LOG_H */
