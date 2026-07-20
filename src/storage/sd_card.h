/**
 * @file sd_card.h
 * @brief SD card SPI driver
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#include <Arduino.h>

/**
 * @brief Initialize SD card on SPI bus.
 * @return true on success, false on failure
 */
bool sd_card_init(void);

/**
 * @brief Check if SD card is mounted.
 */
bool sd_card_is_mounted(void);

/**
 * @brief Get SD card total size in MB.
 */
uint32_t sd_card_total_mb(void);

/**
 * @brief Get SD card used size in MB.
 */
uint32_t sd_card_used_mb(void);

/**
 * @brief Write text to a file (append mode).
 * @param path File path (e.g., "/sd/logs/scan.json")
 * @param data Text data to write
 * @return true on success
 */
bool sd_card_write_file(const char* path, const char* data);

/**
 * @brief Read entire file content into a String.
 * @param path File path
 * @return File content, or empty string on failure
 */
String sd_card_read_file(const char* path);

/**
 * @brief Create a directory (recursive).
 * @param path Directory path
 * @return true on success
 */
bool sd_card_mkdir(const char* path);

/**
 * @brief Check if a file exists.
 */
bool sd_card_exists(const char* path);

#endif /* SD_CARD_H */
