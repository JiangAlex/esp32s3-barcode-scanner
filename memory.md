# Hardware Memory Notes

## ESP32-S3 Module 資訊

- **Module**: ESP32-S3-WROOM-1 N16R8
- **Flash**: 16MB (QIO)
- **PSRAM**: 8MB Octal SPI (佔用 GPIO26-32)
- **CPU**: Dual-core Xtensa LX7 @ 240MHz
- **SRAM**: 512KB
- **USB**: OTG on GPIO19/20

## Camera — OV2640

### Pin 配置

| Signal | GPIO | Notes |
|--------|------|-------|
| XCLK | 15 | Camera clock output (20MHz) |
| SIOD (SDA) | 4 | SCCB I2C data |
| SIOC (SCL) | 5 | SCCB I2C clock |
| VSYNC | 6 | Vertical sync |
| HREF | 7 | Horizontal reference |
| PCLK | 13 | Pixel clock input |
| D0 | 11 | Data bit 0 |
| D1 | 9 | Data bit 1 |
| D2 | 8 | Data bit 2 |
| D3 | 10 | Data bit 3 |
| D4 | 12 | Data bit 4 |
| D5 | 18 | Data bit 5 |
| D6 | 17 | Data bit 6 |
| D7 | 16 | Data bit 7 |
| PWDN | -1 | Not connected (tie to GND externally) |
| RESET | -1 | Not connected (software reset via SCCB) |

### Camera 設定

- Frame size: **QVGA (320x240)** — 適合條碼掃描
- Pixel format: **Grayscale** — 條碼解碼只需灰階
- Frame buffer: **PSRAM** 儲存 (fb_count=2)
- XCLK: **20MHz**
- 自動曝光、自動增益、較高對比度（針對條碼優化）

### 注意事項

- OV2640 模組排線為 24-pin FPC，確認排線方向
- SCCB (I2C-like) 使用 GPIO4/5，不需外部上拉（模組內建）
- 如果影像品質差，嘗試降低 XCLK 到 10MHz

## TFT Display — ILI9341 / ST7789 (240x320 SPI)

### Pin 配置

| Signal | GPIO | SPI Bus | Notes |
|--------|------|---------|-------|
| MOSI | 35 | SPI2 (HSPI) | Data out to TFT |
| SCLK | 36 | SPI2 (HSPI) | SPI clock |
| CS | 37 | - | Chip select (active low) |
| DC | 38 | - | Data/Command select |
| RST | 39 | - | Hardware reset (active low) |
| BL | 40 | LEDC CH0 | Backlight PWM (44.1kHz) |

### Display 設定

- Resolution: **240 x 320** pixels
- Color depth: **16-bit RGB565**
- SPI clock: **40MHz**
- Byte swap: **Enabled** (big-endian for SPI)
- Rotation: **0** (Portrait, configurable)
- LVGL draw buffer: **PSRAM** (2x 240*40 lines)

### ILI9341 vs ST7789 差異

- ILI9341: `cfg.invert = false`, `cfg.rgb_order = false`
- ST7789: `cfg.invert = true`, `cfg.rgb_order = false`
- 切換時修改 `display.cpp` 中的 Panel class 和 config

## SD Card — SPI Mode

### Pin 配置

| Signal | GPIO | SPI Bus | Notes |
|--------|------|---------|-------|
| MISO | 41 | SPI3 (VSPI) | Data in from SD |
| MOSI | 42 | SPI3 (VSPI) | Data out to SD |
| SCLK | 2 | SPI3 (VSPI) | SPI clock |
| CS | 1 | - | Chip select (active low) |

### SD Card 設定

- SPI clock: **20MHz**
- 與 TFT 使用 **不同 SPI bus**（TFT=SPI2, SD=SPI3），避免衝突
- 格式: **FAT32**（SD.h 預設）
- 目錄結構:
  ```
  /sd/
  ├── logs/
  │   └── scan_log.json    ← 掃描紀錄 (JSON Lines)
  └── res/
      └── (UI 圖片資源)
  ```

### 注意事項

- SD 模組選擇 **3.3V** 相容版本
- GPIO2 作為 SD SCLK：ESP32-S3 上 GPIO2 無 bootstrap 問題（不同於 ESP32）
- GPIO1 作為 SD CS：避免與 USB boot 衝突（N16R8 使用 USB-JTAG 時注意）

## Buzzer

| Signal | GPIO | Notes |
|--------|------|-------|
| BUZZER | 14 | LEDC CH1, PWM output |

- 頻率: **2700Hz**（預設，可調）
- 掃描成功: 100ms beep
- 使用 LEDC PWM 驅動，duty=128（50%）

## Status LED

| Signal | GPIO | Notes |
|--------|------|-------|
| LED | 48 | Digital output / Neopixel |

- DevKitC-1 有些版本 GPIO48 接 Neopixel RGB LED
- 簡單版本用 HIGH/LOW 控制

## User Button

| Signal | GPIO | Notes |
|--------|------|-------|
| BOOT BTN | 0 | Active LOW, 內建上拉 |

- 按下 = LOW，放開 = HIGH
- 可作為 scan trigger 或返回鍵

## SPI Bus 分配

| Bus | Host | 用途 | Pins |
|-----|------|------|------|
| SPI2 | HSPI | TFT LCD | MOSI=35, SCLK=36 |
| SPI3 | VSPI | SD Card | MISO=41, MOSI=42, SCLK=2 |

> 兩個 SPI bus 完全獨立，可同時操作 TFT 和 SD Card，無需 CS 管理

## PSRAM 使用

- **GPIO26-32**: Octal SPI PSRAM（模組內部，不可外部使用）
- PSRAM 用途:
  - Camera frame buffers (2x QVGA = ~150KB)
  - LVGL draw buffers (2x 240*40*2 = ~38KB)
  - QR code canvas buffer
  - JSON parsing buffers

## MQTT Protocol

### Topics

| Topic | Publisher | Subscriber |
|-------|-----------|------------|
| `warehouse/query` | ESP32 | Server |
| `warehouse/create` | ESP32 | Server |
| `warehouse/response/{device_id}` | Server | ESP32 |

### Query Payload

```json
{"barcode": "4710088123456", "device_id": "esp32-001"}
```

### Create Payload

```json
{
  "device_id": "esp32-001",
  "barcode": "4710088123456",
  "name": "電阻 10kΩ",
  "spec": "0805 1/8W",
  "quantity": 500,
  "location": "A-01-03",
  "supplier": "大毅科技"
}
```

### Response Payload

```json
{
  "status": "found",
  "data": {
    "barcode": "4710088123456",
    "name": "電阻 10kΩ",
    "spec": "0805 1/8W",
    "quantity": 500,
    "location": "A-01-03",
    "supplier": "大毅科技",
    "date_in": "2026-07-20T10:00:00"
  }
}
```

## Pin Conflict 注意事項

1. **GPIO26-32**: PSRAM 佔用，絕對不可使用
2. **GPIO19/20**: USB OTG，若需 USB 功能不可挪用
3. **GPIO43/44**: UART0 (Serial debug)，接 USB-UART 橋接
4. **GPIO0**: BOOT button，啟動時不可拉 LOW（除非要進入下載模式）
5. **TFT 與 SD 在不同 SPI bus**，不會互相干擾
6. **Camera 佔用大量 GPIO (14 pins)**，規劃時優先分配
