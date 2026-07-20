# esp32s3-barcode-scanner

ESP32-S3 多功能條碼掃描器韌體 — 掃描條碼查詢倉管資料庫 / 生成 QR Code 顯示

## Features

- **OV2640 攝影機** — 即時掃描 QR Code、DataMatrix、Code128、EAN/UPC 等多種條碼格式
- **LVGL 圖形介面** — 240x320 TFT LCD，多頁面 UI（首頁/掃描/生成/歷史/設定）
- **QR Code 生成** — 輸入文字即時生成 QR Code 並顯示
- **MQTT 通訊** — 掃描條碼後透過 MQTT 查詢後端倉管資料庫
- **SD 卡存取** — 儲存掃描紀錄（JSON 格式）+ UI 圖片資源
- **倉管整合** — 支援查詢品項（品名/規格/數量/儲位/供應商）& 建立新品項

## Hardware

| 元件 | 規格 |
|------|------|
| MCU | ESP32-S3-DevKitC-1 (N16R8, 16MB Flash, 8MB PSRAM) |
| Camera | OV2640 2MP (QVGA Grayscale for scanning) |
| Display | 2.4" TFT LCD 240x320 (ILI9341/ST7789, SPI) |
| Storage | MicroSD Card Module (SPI) |
| Audio | Buzzer (scan success beep) |

## System Architecture

```
┌──────────────┐       MQTT        ┌──────────────────────┐       SQL       ┌────────────┐
│  ESP32-S3    │ ──────────────►  │  barcode-warehouse   │ ──────────────► │ PostgreSQL │
│  This Device │ ◄──────────────  │  -server (FastAPI)   │ ◄────────────── │  warehouse │
└──────────────┘   Mosquitto       └──────────────────────┘                 └────────────┘
       │
       ├── OV2640 Camera (scan)
       ├── TFT LCD + LVGL (display)
       ├── SD Card (log + resources)
       └── Buzzer (feedback)
```

## Pin Map (ESP32-S3-DevKitC-1 N16R8)

| Module | Signal | GPIO |
|--------|--------|------|
| **Camera** | XCLK / SDA / SCL | 15 / 4 / 5 |
| | VSYNC / HREF / PCLK | 6 / 7 / 13 |
| | D0-D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| **TFT SPI** | MOSI / SCLK / CS / DC / RST / BL | 35 / 36 / 37 / 38 / 39 / 40 |
| **SD Card SPI** | MISO / MOSI / SCLK / CS | 41 / 42 / 2 / 1 |
| **Misc** | Buzzer / LED / Button | 14 / 48 / 0 |

> GPIO26-32 為 PSRAM 佔用，GPIO19/20 為 USB OTG，GPIO43/44 為 Serial debug

## Project Structure

```
esp32s3-barcode-scanner/
├── platformio.ini          ← PlatformIO 設定
├── partitions.csv          ← Flash 分區表 (16MB)
├── lv_conf.h               ← LVGL 配置
├── src/
│   ├── main.cpp            ← 主程式進入點
│   ├── config/
│   │   ├── pinout.h        ← GPIO 定義
│   │   └── config.h        ← 系統設定 (WiFi/MQTT/掃描參數)
│   ├── display/
│   │   ├── display.h/.cpp  ← TFT + LVGL 驅動 (LovyanGFX)
│   ├── camera/
│   │   ├── camera.h/.cpp   ← OV2640 攝影機驅動
│   ├── storage/
│   │   ├── sd_card.h/.cpp  ← SD 卡 SPI 驅動
│   │   └── scan_log.h/.cpp ← 掃描紀錄 (JSON)
│   ├── decoder/
│   │   ├── barcode_decoder.h/.cpp ← 條碼解碼 (quirc + ZBar)
│   ├── generator/
│   │   ├── barcode_generator.h/.cpp ← QR Code 生成
│   ├── network/
│   │   ├── wifi_manager.h/.cpp   ← WiFi 連線管理
│   │   └── mqtt_client.h/.cpp    ← MQTT 收發
│   └── ui/
│       └── ui_main.h/.cpp  ← LVGL 多頁面 UI
├── README.md
└── memory.md               ← 硬體記憶文件
```

## Build & Upload

```bash
# Build
pio run

# Upload to ESP32-S3
pio run -t upload

# Serial monitor
pio device monitor
```

## Configuration

編輯 `src/config/config.h`：

```c
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define MQTT_BROKER     "192.168.1.100"
#define MQTT_PORT       1883
#define DEVICE_ID       "esp32-001"
```

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `warehouse/query` | ESP32 → Server | 條碼查詢請求 |
| `warehouse/create` | ESP32 → Server | 新品項建立請求 |
| `warehouse/response/esp32-001` | Server → ESP32 | 回傳查詢結果 |

## Dependencies

- [LVGL v8.4](https://github.com/lvgl/lvgl) — GUI framework
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — TFT display driver
- [ArduinoJson v7](https://github.com/bblanchon/ArduinoJson) — JSON serialization
- [QRCode](https://github.com/ricmoo/QRCode) — QR code generation
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT client
- [esp32-camera](https://github.com/espressif/esp32-camera) — Camera driver
- [quirc](https://github.com/dlbeer/quirc) — QR code decoder

## Related Projects

- **[barcode-warehouse-server](https://github.com/JiangAlex/barcode-warehouse-server)** — 後端服務（FastAPI + PostgreSQL + MQTT）

## License

MIT
