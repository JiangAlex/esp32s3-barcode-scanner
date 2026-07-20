/**
 * @file sd_card.cpp
 * @brief SD card SPI driver implementation (VSPI / SPI3)
 */

#include "sd_card.h"
#include <SPI.h>
#include <SD.h>
#include "config/pinout.h"
#include "config/config.h"

static SPIClass sd_spi(SPI3_HOST);  // VSPI
static bool _mounted = false;

bool sd_card_init(void) {
    sd_spi.begin(SD_PIN_SCLK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);

    if (!SD.begin(SD_PIN_CS, sd_spi, SD_SPI_FREQ)) {
        Serial.println("[SD] Mount failed");
        _mounted = false;
        return false;
    }

    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No card detected");
        _mounted = false;
        return false;
    }

    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC) typeStr = "MMC";
    else if (cardType == CARD_SD) typeStr = "SD";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";

    Serial.printf("[SD] Card type: %s, Size: %lluMB\n", typeStr, SD.cardSize() / (1024 * 1024));

    // Create log directory if not exists
    if (!SD.exists(SD_LOG_DIR)) {
        SD.mkdir(SD_LOG_DIR);
    }

    _mounted = true;
    return true;
}

bool sd_card_is_mounted(void) {
    return _mounted;
}

uint32_t sd_card_total_mb(void) {
    if (!_mounted) return 0;
    return SD.cardSize() / (1024 * 1024);
}

uint32_t sd_card_used_mb(void) {
    if (!_mounted) return 0;
    return SD.usedBytes() / (1024 * 1024);
}

bool sd_card_write_file(const char* path, const char* data) {
    if (!_mounted) return false;

    File file = SD.open(path, FILE_APPEND);
    if (!file) {
        Serial.printf("[SD] Failed to open %s for writing\n", path);
        return false;
    }

    file.print(data);
    file.close();
    return true;
}

String sd_card_read_file(const char* path) {
    if (!_mounted) return "";

    File file = SD.open(path, FILE_READ);
    if (!file) {
        return "";
    }

    String content = file.readString();
    file.close();
    return content;
}

bool sd_card_mkdir(const char* path) {
    if (!_mounted) return false;
    return SD.mkdir(path);
}

bool sd_card_exists(const char* path) {
    if (!_mounted) return false;
    return SD.exists(path);
}
