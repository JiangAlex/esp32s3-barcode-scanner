/**
 * @file pinout.h
 * @brief ESP32-S3-DevKitC-1 (N16R8) GPIO Pin Definitions
 *
 * Pin allocation for: OV2640 Camera, TFT LCD (SPI), SD Card (SPI), Buzzer, LED, Button
 * Note: GPIO26-32 are used by PSRAM (N16R8 module), do NOT use.
 *       GPIO19/20 reserved for USB OTG.
 *       GPIO43/44 reserved for UART0 (Serial debug).
 */

#ifndef PINOUT_H
#define PINOUT_H

// ─── Board ──────────────────────────────────────────────────────────────────

#define PIN_BOOT_BTN        0       // BOOT button (user button)

// ─── Camera OV2640 ──────────────────────────────────────────────────────────

#define CAM_PIN_XCLK        15
#define CAM_PIN_SIOD        4       // SCCB I2C SDA
#define CAM_PIN_SIOC        5       // SCCB I2C SCL
#define CAM_PIN_VSYNC       6
#define CAM_PIN_HREF        7
#define CAM_PIN_PCLK        13
#define CAM_PIN_D0          11
#define CAM_PIN_D1          9
#define CAM_PIN_D2          8
#define CAM_PIN_D3          10
#define CAM_PIN_D4          12
#define CAM_PIN_D5          18
#define CAM_PIN_D6          17
#define CAM_PIN_D7          16
#define CAM_PIN_PWDN        -1      // Not connected (tie to GND)
#define CAM_PIN_RESET       -1      // Not connected (software reset)

// Camera clock frequency
#define CAM_XCLK_FREQ      20000000    // 20MHz

// ─── TFT LCD (SPI — HSPI / SPI2) ───────────────────────────────────────────

#define TFT_PIN_MOSI        35
#define TFT_PIN_SCLK        36
#define TFT_PIN_CS          37
#define TFT_PIN_DC          38
#define TFT_PIN_RST         39
#define TFT_PIN_BL          40      // Backlight (PWM)

// TFT display parameters
#define TFT_WIDTH           240
#define TFT_HEIGHT          320
#define TFT_SPI_FREQ        40000000   // 40MHz SPI clock
#define TFT_BL_PWM_CH       0          // LEDC channel for backlight

// ─── SD Card (SPI — VSPI / SPI3) ───────────────────────────────────────────

#define SD_PIN_MISO         41
#define SD_PIN_MOSI         42
#define SD_PIN_SCLK         2
#define SD_PIN_CS           1

// SD SPI frequency
#define SD_SPI_FREQ         20000000   // 20MHz

// ─── Buzzer ─────────────────────────────────────────────────────────────────

#define BUZZER_PIN          14
#define BUZZER_PWM_CH       1          // LEDC channel for buzzer
#define BUZZER_FREQ         2700       // Default buzzer frequency (Hz)

// ─── Status LED ─────────────────────────────────────────────────────────────

#define LED_PIN             48         // Onboard RGB or GPIO LED

// ─── Reserved (DO NOT USE) ──────────────────────────────────────────────────

// GPIO19, GPIO20 — USB OTG (D-, D+)
// GPIO26-32      — PSRAM (Octal SPI, internal to N16R8 module)
// GPIO43, GPIO44 — UART0 TXD/RXD (Serial debug)

#endif /* PINOUT_H */
