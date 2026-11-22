/**
 * @file pinout.h
 * @brief Board GPIO pin definitions + board capability flags.
 *
 * Select the board with a build flag (see platformio.ini):
 *   (default)       Waveshare ESP32-S3-Touch-LCD-2   [env:esp32s3]
 *   -D BOARD_LCD28  Waveshare ESP32-S3-LCD-2.8       [env:lcd28]
 *
 * Capability flags (0/1), defined per board below:
 *   HAS_CAMERA        on-board OV5640 camera + live-preview decoding
 *   HAS_TOUCH         CST816 capacitive touch on the shared I2C bus
 *   HAS_UART_SCANNER  external QL3301L scan module on a UART (decoded text)
 *   SD_USE_MMC        TF card on SD_MMC 1-bit (else SPI, shared with LCD)
 *   HAS_PWR_LATCH     battery power-hold GPIO must be driven HIGH at boot
 *   TFT_BUS_SHARED    LCD SPI bus shared with the SD card
 */

#ifndef PINOUT_H
#define PINOUT_H

#if defined(BOARD_LCD28)
// ═══════════════════════════════════════════════════════════════════════════
// Waveshare ESP32-S3-LCD-2.8 (ESP32-S3R8, 16MB flash, 8MB OPI PSRAM)
// Source: official demo ESP32-S3-LCD-2.8-Demo.zip, Arduino/examples/
//         LVGL_Arduino (Display_ST7789.h, I2C_Driver.h, SD_Card.h, PWR_Key.h,
//         BAT_Driver.h, Audio_PCM5101.h), verified 2026-11-22.
// No camera, no touch panel driver in the demo. Scanning comes from an external
// QL3301L module over UART (docs/memory.md "掃描來源：QL3301L").
// ═══════════════════════════════════════════════════════════════════════════

#define BOARD_HW_NAME       "ESP32-S3-LCD-2.8"

#define HAS_CAMERA          0
#define HAS_TOUCH           0
#define HAS_UART_SCANNER    1
#define SD_USE_MMC          1
#define HAS_PWR_LATCH       1
#define TFT_BUS_SHARED      false   // LCD has its own SPI; SD is on SD_MMC

// ─── Board ──────────────────────────────────────────────────────────────────

#define PIN_BOOT_BTN        0       // BOOT button (active LOW)
#define PIN_PWR_KEY         6       // PWR button input (active LOW), not used for nav
#define PIN_PWR_LATCH       7       // Battery power hold: HIGH = stay on

// ─── TFT LCD (ST7789, SPI — dedicated bus) ──────────────────────────────────

#define TFT_PIN_SCLK        40
#define TFT_PIN_MOSI        45
#define TFT_PIN_MISO        -1      // write-only
#define TFT_PIN_CS          42
#define TFT_PIN_DC          41
#define TFT_PIN_RST         39
#define TFT_PIN_BL          5       // Backlight (PWM)

#define TFT_WIDTH           240
#define TFT_HEIGHT          320
#define TFT_SPI_FREQ        40000000   // demo uses 80MHz; 40MHz is the safe default
#define TFT_BL_PWM_CH       0

// ─── SD Card (TF, SD_MMC 1-bit) ─────────────────────────────────────────────

#define SD_PIN_CLK          14
#define SD_PIN_CMD          17
#define SD_PIN_D0           16
#define SD_PIN_D3           21      // driven HIGH before mount (demo SD_D3_EN)

// ─── I2C bus (IMU QMI8658 + RTC PCF85063) ───────────────────────────────────

#define I2C_SHARED_SDA      11
#define I2C_SHARED_SCL      10
#define I2C_FREQ            100000

#define IMU_I2C_ADDR        0x6B

// ─── QL3301L scan module (UART on the 12PIN header) ─────────────────────────
// Module PIN5 UART_TXD -> ESP32 RX, PIN4 UART_RXD <- ESP32 TX,
// PIN12 TRIG <- ESP32 GPIO (active LOW; module default = manual, level trigger).
// The module must first be switched from its factory USB-HID output to
// TTL-232 serial output (config manual 2.1.1 setting code).
// WARNING: ESP32-S3 GPIOs are NOT 5V tolerant. If the module runs at 5V and its
// TX idles at 5V, add a level shifter / divider on QR_UART_RX.

#define QR_UART_RX          44      // 12PIN header RXD (GPIO44)
#define QR_UART_TX          43      // 12PIN header TXD (GPIO43)
#define QR_PIN_TRIG         18      // 12PIN header IO18
#define QR_UART_BAUD        9600    // module default 9600 8N1

// ─── Battery ────────────────────────────────────────────────────────────────

#define BAT_ADC_PIN         8

// ─── Reserved (DO NOT USE) ──────────────────────────────────────────────────
// GPIO19/20 USB D-/D+; GPIO26-32 PSRAM; GPIO38/47/48 PCM5101 I2S (LRC/DOUT/BCLK)

#else
// ═══════════════════════════════════════════════════════════════════════════
// Waveshare ESP32-S3-Touch-LCD-2 (default board, original pinout below)
// ═══════════════════════════════════════════════════════════════════════════

#define BOARD_HW_NAME       "ESP32-S3-Touch-LCD-2"

#define HAS_CAMERA          1
#define HAS_TOUCH           1
#define HAS_UART_SCANNER    1       // QL3301L on the header (see QR_* below)
#define SD_USE_MMC          0
#define HAS_PWR_LATCH       0
#define TFT_BUS_SHARED      true    // LCD shares SPI bus with SD card

// ─── QL3301L scan module (UART on the 28-pin header) ────────────────────────
// Header (Waveshare "Interface Introduction"): pin 3 TXD = GPIO43,
// pin 4 RXD = GPIO44, pin 23 = GPIO18, pin 15 = 5V, pin 16 / 13 / 2 = GND.
// Module PIN5 UART_TXD -> GPIO44, PIN4 UART_RXD <- GPIO43, PIN12 TRIG <- GPIO18.
// Serial (log) is USB-CDC, so UART0's pins are free for Serial1.
// WARNING: ESP32-S3 GPIOs are NOT 5V tolerant; check the module TX level.

#define QR_UART_RX          44
#define QR_UART_TX          43
#define QR_PIN_TRIG         18
#define QR_UART_BAUD        9600

/*
 * Original header for this board:
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
#define SD_SPI_FREQ         4000000    // 4MHz — 20MHz caused writes to not commit
                                       // (print returns N bytes but file stays
                                       // 0; shared-bus signal integrity). 4MHz
                                       // is the safe/standard SD-over-SPI rate.

// ─── Shared I2C bus (Touch + IMU) ───────────────────────────────────────────

#define I2C_SHARED_SDA      48
#define I2C_SHARED_SCL      47
#define I2C_FREQ            100000      // 100kHz (shared bus: CST816 + QMI8658; 400k was unreliable)

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
// GPIO43, GPIO44 — UART0 TXD/RXD: used by the QL3301L (Serial1); log is USB-CDC

#endif /* BOARD_LCD28 */

#endif /* PINOUT_H */
