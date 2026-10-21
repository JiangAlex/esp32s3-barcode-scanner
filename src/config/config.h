/**
 * @file config.h
 * @brief System configuration — board name, version, network settings
 */

#ifndef CONFIG_H
#define CONFIG_H

// ─── Board Info ─────────────────────────────────────────────────────────────

#define BOARD_NAME          "ESP32-S3 Barcode Scanner"
#define FW_VERSION          "0.1.0"
#define DEVICE_ID           "esp32-001"
#define BLE_DEVICE_NAME     "SoftSnail"   // BLE HID keyboard advertised name

// ─── NVS Settings Persistence ──────────────────────────────────────────────
// If NVS has stored values they override these defaults.
// See src/storage/nvs_settings.h

#define DEFAULT_WIFI_SSID       "YOUR_WIFI_SSID"
#define DEFAULT_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define DEFAULT_MQTT_BROKER    "blog.softsnail.com"
#define DEFAULT_MQTT_PORT      1883
#define DEFAULT_BRIGHTNESS      200
#define DEFAULT_LANGUAGE        "en"

// ─── WiFi Settings ──────────────────────────────────────────────────────────

#define WIFI_SSID           "YOUR_WIFI_SSID"
#define WIFI_PASSWORD       "YOUR_WIFI_PASSWORD"
#define WIFI_CONNECT_TIMEOUT_MS  10000   // 10 seconds

// ─── MQTT Settings ──────────────────────────────────────────────────────────

#define MQTT_BROKER         "blog.softsnail.com"   // Mosquitto broker (public domain; B400A)
#define MQTT_PORT           1883
#define MQTT_USER           ""                 // Leave empty if no auth
#define MQTT_PASSWORD       ""

// MQTT Topics
#define MQTT_TOPIC_QUERY    "warehouse/query"
#define MQTT_TOPIC_CREATE   "warehouse/create"
#define MQTT_TOPIC_STOCK_IN   "warehouse/stock/in"
#define MQTT_TOPIC_STOCK_OUT  "warehouse/stock/out"
#define MQTT_TOPIC_STOCKTAKE_START "warehouse/stocktake/start"
#define MQTT_TOPIC_STOCKTAKE_SCAN  "warehouse/stocktake/scan"
#define MQTT_TOPIC_STOCKTAKE_END   "warehouse/stocktake/end"
#define MQTT_TOPIC_SCAN_INPUT      "warehouse/scan-input/" DEVICE_ID
#define MQTT_TOPIC_RESPONSE "warehouse/response/" DEVICE_ID

// ─── Display Settings (ST7789T3, shared SPI bus with SD) ────────────────────

#define DISP_BL_DEFAULT     200     // Backlight PWM duty (0-255)
#define DISP_ROTATION       0       // 0=Portrait 240x320, matches UI layout
                                    // (official demo uses 1=landscape; we keep
                                    //  portrait to preserve the 240x320 UI)
#define DISP_INVERT         true    // ST7789T3 IPS panel needs color inversion
#define DISP_BUS_SHARED     true    // LCD shares SPI bus with SD card

// ─── Camera Settings (OV5640, compatible with OV2640) ───────────────────────

// Always-on SVGA 800x600: decoding needs the resolution to resolve fine 1D
// product-label bars; preview is downsampled from this to the viewfinder. Note:
// esp32-camera allocates the framebuffer at init from this size and cannot grow
// it at runtime, so we must init at the largest size we ever decode.
#define CAM_FRAME_SIZE      FRAMESIZE_SVGA      // 800x600
#define CAM_JPEG_QUALITY    12                  // 0-63, lower = better quality
#define CAM_FB_COUNT        2                   // Frame buffer count (PSRAM)

// ─── Scan Settings ──────────────────────────────────────────────────────────

// Buzzer is NOT on the Waveshare ESP32-S3-Touch-LCD-2 board.
// Set BUZZER_ENABLED to 1 and wire a passive buzzer to BUZZER_PIN to use it.
#define BUZZER_ENABLED          0
#define BUZZER_PIN              41     // Placeholder; only used if BUZZER_ENABLED
#define BUZZER_PWM_CH           1      // LEDC channel for buzzer
#define BUZZER_FREQ             2700   // Buzzer frequency (Hz)

#define SCAN_BEEP_DURATION_MS   100    // Buzzer beep duration on successful scan
#define SCAN_COOLDOWN_MS        1500   // Debounce between duplicate scans of the
                                       // SAME barcode. Short so STOCK_IN/OUT and
                                       // STOCKTAKE can re-scan an item to count
                                       // it again; QUERY stops the preview on a
                                       // hit so it never re-scans anyway.
#define SCAN_LOG_FILE           "/logs/scan_log.json"
#define SCAN_LOG_MAX_ENTRIES    1000

// ─── SD Card Settings ───────────────────────────────────────────────────────
// NOTE: the Arduino ESP32 SD library mounts the card at SD_MOUNT_POINT ("/sd")
// and AUTOMATICALLY prepends it inside SD.open()/mkdir(). Therefore every path
// handed to the SD API must be CARD-ROOT-RELATIVE (e.g. "/logs/..."), WITHOUT
// the "/sd" prefix — otherwise it resolves to "/sd/sd/logs/..." and fopen fails.

#define SD_MOUNT_POINT      "/sd"
#define SD_LOG_DIR          "/logs"
#define SD_RESOURCE_DIR     "/res"

#endif /* CONFIG_H */
