/**
 * @file main.cpp
 * @brief ESP32-S3 Barcode Scanner — Main Entry Point
 *
 * Integrates: OV5640 Camera, ST7789 TFT LCD (LVGL + CST816D touch), SD Card, MQTT, Barcode Decode/Generate
 */

#include <Arduino.h>
#include "config/config.h"
#include "config/pinout.h"
#include "display/display.h"
#include "camera/camera.h"
#include "storage/sd_card.h"
#include "network/wifi_manager.h"
#include "network/mqtt_client.h"
#include "decoder/barcode_decoder.h"
#include "generator/barcode_generator.h"
#include "storage/scan_log.h"
#include "ui/ui_main.h"

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("================================");
    Serial.printf("%s v%s\n", BOARD_NAME, FW_VERSION);
    Serial.println("================================");

    // Initialize display (TFT + LVGL)
    display_init();
    Serial.println("[OK] Display initialized");

    // Initialize SD card
    if (sd_card_init()) {
        Serial.println("[OK] SD card initialized");
    } else {
        Serial.println("[WARN] SD card not available");
    }

    // Initialize camera
    if (camera_init()) {
        Serial.println("[OK] Camera initialized");
    } else {
        Serial.println("[ERR] Camera init failed");
    }

    // Initialize WiFi + MQTT
    wifi_manager_init();
    mqtt_client_init();
    Serial.println("[OK] Network initialized");

    // Initialize barcode modules
    barcode_decoder_init();
    barcode_generator_init();
    Serial.println("[OK] Barcode modules initialized");

    // Initialize scan log
    scan_log_init();

    // Initialize LVGL UI (must be last — depends on other modules)
    ui_main_init();
    Serial.println("[OK] UI initialized");

    Serial.println("================================");
    Serial.println("System ready.");
    Serial.println("================================");
}

void loop() {
    // LVGL task handler
    lv_timer_handler();

    // MQTT keep-alive
    mqtt_client_loop();

    // Small delay to yield
    delay(5);
}
