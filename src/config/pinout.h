/**
 * @file pinout.h
 * @brief Waveshare ESP32-S3-Touch-LCD-2 (ESP32-S3R8) GPIO Pin Definitions
 *
 * Pin allocation for: OV5640 Camera, ST7789T3 TFT LCD (SPI), SD Card (SPI),
 *                     CST816D Touch (I2C), QMI8658 IMU (I2C), Battery ADC.
 *
 * Source: Official Waveshare Demo (reference/waveshare-demo/), cross-verified
 *         across 01_factory / 02_gfx_helloworld / 03_sd_card_test /
 *         06_lvgl_battery / 07_lvgl_brightness / bsp_cst816.
 *
 * NOTE: LCD and SD Card SHARE the same SPI bus (SCLK=39, MOSI=38, MISO=40);
 *       only the CS lines differ (LCD CS=45, SD CS=41). The display driver
 *       must run in shared-bus mode so SD access does not corrupt the panel.
 *       Touch (CST816D) and IMU (QMI8658) SHARE the same I2C bus (SDA=48, SCL=47).
 */

#ifndef PINOUT_H
#define PINOUT_H

// ─── Board ──────────────────────────────────────────────────────────────────

#define PIN_BOOT_BTN        0       // BOOT button (chip-level fixed pin, active LOW)

// ─── Camera OV5640 (compatible with OV2640) ─────────────────────────────────

#define CAM_PIN_XCLK        8
#define CAM_PIN_SIOD        21      // SCCB I2C SDA
#define CAM_PIN_SIOC        16      // SCCB I2C SCL
#define CAM_PIN_VSYNC       6
#define CAM_PIN_HREF        4
#define CAM_PIN_PCLK        9
#define CAM_PIN_D0          12      // Y2
#define CAM_PIN_D1          13      // Y3
#define CAM_PIN_D2          15      // Y4
#define CAM_PIN_D3          11      // Y5
#define CAM_PIN_D4          14      // Y6
#define CAM_PIN_D5          10      // Y7
#define CAM_PIN_D6          7       // Y8
#define CAM_PIN_D7          2       // Y9
#define CAM_PIN_PWDN        17      // Power down control
#define CAM_PIN_RESET       -1      // Software reset (no dedicated pin)

// Camera clock frequency
#define CAM_XCLK_FREQ       20000000    // 20MHz

// ─── Shared SPI bus (LCD + SD Card) ─────────────────────────────────────────

#define SPI_SHARED_SCLK     39
#define SPI_SHARED_MOSI     38
#define SPI_SHARED_MISO     40

// ─── TFT LCD (ST7789T3, SPI — shares bus above) ─────────────────────────────

#define TFT_PIN_SCLK        SPI_SHARED_SCLK
#define TFT_PIN_MOSI        SPI_SHARED_MOSI
#define TFT_PIN_MISO        SPI_SHARED_MISO
#define TFT_PIN_CS          45
#define TFT_PIN_DC          42
#define TFT_PIN_RST         -1      // Software reset (no dedicated pin)
#define TFT_PIN_BL          1       // Backlight (PWM)

// TFT display parameters
#define TFT_WIDTH           240
#define TFT_HEIGHT          320
#define TFT_SPI_FREQ        40000000   // 40MHz SPI clock
#define TFT_BL_PWM_CH       0          // LEDC channel for backlight

// ─── SD Card (SPI — shares bus with LCD) ────────────────────────────────────

#define SD_PIN_SCLK         SPI_SHARED_SCLK
#define SD_PIN_MOSI         SPI_SHARED_MOSI
#define SD_PIN_MISO         SPI_SHARED_MISO
#define SD_PIN_CS           41

// SD SPI frequency
#define SD_SPI_FREQ         20000000   // 20MHz

// ─── Shared I2C bus (Touch + IMU) ───────────────────────────────────────────

#define I2C_SHARED_SDA      48
#define I2C_SHARED_SCL      47
#define I2C_FREQ            400000      // 400kHz

// ─── Touch CST816D (I2C — shares bus above) ─────────────────────────────────

#define TOUCH_PIN_SDA       I2C_SHARED_SDA
#define TOUCH_PIN_SCL       I2C_SHARED_SCL
#define TOUCH_PIN_INT       -1      // Not used (polled over I2C)
#define TOUCH_PIN_RST       -1      // No dedicated reset pin
#define TOUCH_I2C_ADDR      0x15    // CST816_ADDR
#define TOUCH_CHIP_ID       0xB6    // Expected CST816 chip ID

// ─── IMU QMI8658 (I2C — shares bus with Touch) ──────────────────────────────

#define IMU_PIN_SDA         I2C_SHARED_SDA
#define IMU_PIN_SCL         I2C_SHARED_SCL
#define IMU_I2C_ADDR        0x6B

// ─── Battery ────────────────────────────────────────────────────────────────

#define BAT_ADC_PIN         5       // Battery voltage ADC

// ─── Reserved (DO NOT USE) ──────────────────────────────────────────────────

// GPIO19, GPIO20 — USB OTG (D-, D+)
// GPIO26-32      — PSRAM (Octal SPI, internal to ESP32-S3R8 module)
// GPIO43, GPIO44 — UART0 TXD/RXD (Serial debug)

#endif /* PINOUT_H */
