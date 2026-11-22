/**
 * @file sd_card.cpp
 * @brief SD card SPI driver implementation
 *        Shares the FSPI (SPI2) bus with the ST7789 LCD; only CS differs.
 *        LovyanGFX runs in bus_shared mode so display and SD access interleave
 *        safely over the same SCLK/MOSI/MISO lines.
 */

#include "sd_card.h"   // SD_FS + <SD.h> or <SD_MMC.h> per board
#if !SD_USE_MMC
#include <SPI.h>
#endif
#include "config/pinout.h"
#include "config/config.h"
#include "scan_preview.h"   // extern s_spi_mutex (shared FSPI lock with LCD)

#if !SD_USE_MMC
static SPIClass sd_spi(FSPI);       // Same host as the LCD (SPI2 / FSPI)
#endif
static bool _mounted = false;

// SD and the LCD share the FSPI bus. LovyanGFX serializes its own access with
// s_spi_mutex (held during LVGL flush + the preview render task). SD access
// (open/read/write/close) MUST take the same lock, otherwise a FAT write can
// interleave with an LCD transaction and silently fail to commit (the dir
// entry is created but the data blocks / size are never written → file exists
// but reads back as 0 bytes). Verified on hardware 2026-10-20.
static inline void sd_bus_lock(void)   { if (s_spi_mutex) xSemaphoreTake(s_spi_mutex, portMAX_DELAY); }
static inline void sd_bus_unlock(void) { if (s_spi_mutex) xSemaphoreGive(s_spi_mutex); }

bool sd_card_init(void) {
#if SD_USE_MMC
    // SD_MMC 1-bit (ESP32-S3-LCD-2.8). D3 must be held HIGH for the card to
    // stay in SD mode (official demo SD_D3_EN). The pins are routed through
    // the GPIO matrix, so setPins() must be called before begin().
    pinMode(SD_PIN_D3, OUTPUT);
    digitalWrite(SD_PIN_D3, HIGH);
    if (!SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD, SD_PIN_D0)) {
        Serial.println("[SD] SD_MMC setPins failed");
        _mounted = false;
        return false;
    }
    if (!SD_MMC.begin("/sdcard", true /* 1-bit */, false /* no format */)) {
        Serial.println("[SD] Mount failed");
        _mounted = false;
        return false;
    }
#else
    sd_spi.begin(SD_PIN_SCLK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);

    if (!SD.begin(SD_PIN_CS, sd_spi, SD_SPI_FREQ)) {
        Serial.println("[SD] Mount failed");
        _mounted = false;
        return false;
    }
#endif

    uint8_t cardType = SD_FS.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[SD] No card detected");
        _mounted = false;
        return false;
    }

    const char* typeStr = "UNKNOWN";
    if (cardType == CARD_MMC) typeStr = "MMC";
    else if (cardType == CARD_SD) typeStr = "SD";
    else if (cardType == CARD_SDHC) typeStr = "SDHC";

    Serial.printf("[SD] Card type: %s, Size: %lluMB\n", typeStr, SD_FS.cardSize() / (1024 * 1024));

    // NOTE: do NOT mkdir here — on the test card SD.mkdir() fails for every
    // name while root-level file writes succeed (verified on hardware
    // 2026-10-20). The work log therefore uses flat root-level files
    // (/log_YYYY-MM-DD.json, /pending_queue.json); see scan_log.cpp.

    _mounted = true;

    // Boot health check: verify the card can actually COMMIT a write (some
    // cards — notably counterfeit/failing ones — accept writes and allocate a
    // cluster yet never persist the data, leaving a 0-byte file). If this
    // fails, the work log will not function; the log is disabled rather than
    // silently losing entries.
    {
        const char* tp = "/.wrtest";
        SD_FS.remove(tp);
        File wf = SD_FS.open(tp, FILE_WRITE);
        if (wf) { wf.print("x"); wf.flush(); wf.close(); }
        File rf = SD_FS.open(tp, FILE_READ);
        size_t rsz = rf ? rf.size() : 0;
        if (rf) rf.close();
        SD_FS.remove(tp);
        if (rsz == 0) {
            Serial.println("[SD] WARNING: write did not commit (0-byte readback) "
                           "— card is failing/incompatible; work log DISABLED");
            _mounted = false;   // disable logging on a card that cannot persist
            return false;
        }
        Serial.println("[SD] write-commit OK");
    }

    return true;
}

bool sd_card_is_mounted(void) {
    return _mounted;
}

uint32_t sd_card_total_mb(void) {
    if (!_mounted) return 0;
    return SD_FS.cardSize() / (1024 * 1024);
}

uint32_t sd_card_used_mb(void) {
    if (!_mounted) return 0;
    return SD_FS.usedBytes() / (1024 * 1024);
}

bool sd_card_write_file(const char* path, const char* data) {
    if (!_mounted) return false;

    sd_bus_lock();
    File file = SD_FS.open(path, FILE_APPEND);
    if (!file) {
        sd_bus_unlock();
        Serial.printf("[SD] Failed to open %s for writing\n", path);
        return false;
    }

    file.print(data);
    file.flush();      // commit data + FAT size update before releasing the bus
    file.close();
    sd_bus_unlock();
    return true;
}

String sd_card_read_file(const char* path) {
    if (!_mounted) return "";

    sd_bus_lock();
    File file = SD_FS.open(path, FILE_READ);
    if (!file) {
        sd_bus_unlock();
        return "";
    }

    // NOTE: File::readString() intermittently returns an empty string on the
    // ESP32 Arduino SD driver (verified on hardware 2026-10-20: file exists,
    // size > 0, yet readString() gives 0 bytes). Read explicitly by available()
    // bytes into the String instead, which is reliable.
    String content;
    size_t sz = file.size();
    if (sz > 0) content.reserve(sz + 1);
    uint8_t buf[256];
    while (file.available()) {
        int n = file.read(buf, sizeof(buf));
        if (n <= 0) break;
        for (int i = 0; i < n; i++) content += (char)buf[i];
    }
    file.close();
    sd_bus_unlock();
    return content;
}

bool sd_card_mkdir(const char* path) {
    if (!_mounted) return false;
    return SD_FS.mkdir(path);
}

bool sd_card_exists(const char* path) {
    if (!_mounted) return false;
    return SD_FS.exists(path);
}
