# esp32s3-barcode-scanner

ESP32-S3 多功能條碼掃描器韌體 — 掃描查詢倉管 / BLE 無線條碼槍 / 盤點 / 拍照 AI 辨識

## Features

- **OV5640 攝影機** — 即時掃描 QR Code（quirc）與 1D 條碼 EAN-13 / UPC-A / Code128（自製 line-scan 解碼器）。低解析度標籤自動切 SVGA 800×600 單張精解提升成功率；OV5640 AF 版鏡頭支援單次對焦
- **LVGL 圖形介面** — 2.8" TFT 240×320，搖桿導航，中英文 i18n 切換
- **掃描查詢模式** — 掃描條碼透過 MQTT 查詢後端倉管資料庫，顯示完整品項資訊
- **BLE HID 無線條碼槍** — 模擬藍牙鍵盤，掃描值直接輸出到任何電腦/手機的游標位置
- **MQTT 輸入模式** — 推送掃描值至 Web UI 欄位
- **盤點模式** — 連續掃描累積清單，批次上傳 server
- **拍照 AI 辨識** — 拍照傳至 server，AI OCR 自動填入品項單據
- **離線暫存** — WiFi 斷線時自動暫存 SD 卡，恢復連線後自動同步
- **工作日誌** — 自動記錄所有掃描/盤點/拍照操作至 SD 卡
- **SD 卡存取** — 離線暫存 + 工作日誌 + UI 圖片資源

## Hardware

| 元件 | 規格 |
|------|------|
| MCU | ESP32-S3-DevKitC-1 (N16R8, 16MB Flash, 8MB PSRAM) |
| Camera | OV2640 2MP (QVGA Grayscale for scan, SVGA/UXGA JPEG for photo) |
| Display | 2.8" TFT LCD 240×RGB×320 (ILI9341, SPI) + SD Card slot |
| Input | 5-way Joystick (digital) + BOOT button |
| Storage | MicroSD Card (整合於 LCD 模組背面) |
| Audio | Buzzer (scan success beep) |
| Wireless | WiFi + BLE 5.0 (HID Keyboard) |

## System Architecture

```
┌──────────────┐    BLE HID     ┌──────────────────┐
│  ESP32-S3    │ ──────────────► │  PC / Phone      │  ← 模擬鍵盤輸入
│  (SoftSnail) │                 │  任何輸入框       │
└──────┬───────┘                 └──────────────────┘
       │
       │  MQTT / HTTP
       ▼
┌──────────────────────┐       SQL       ┌────────────┐
│  barcode-warehouse   │ ──────────────► │ PostgreSQL │
│  -server (FastAPI)   │ ◄────────────── │  warehouse │
└──────────────────────┘                 └────────────┘
       │
       ├── MQTT: 條碼查詢 / 掃描輸入推送 / 盤點上傳
       └── HTTP: 拍照圖片上傳 → AI OCR
```

## Scan Modes

| 模式 | 功能 | 掃描後行為 |
|------|------|-----------|
| 🔍 查詢 | 查詢倉管資料庫 | 顯示品項完整資訊 |
| 📤 輸入 (BLE HID) | 無線條碼槍 | 模擬鍵盤打字至游標位置 |
| 📤 輸入 (MQTT) | 推送至 Web UI | 自動填入 Web 表單欄位 |
| 📋 盤點 | 連續掃描累積 | 批次上傳 server |

## Pin Map (ESP32-S3-DevKitC-1 N16R8)

| Module | Signal | GPIO |
|--------|--------|------|
| **Camera** | XCLK / SDA / SCL | 15 / 4 / 5 |
| | VSYNC / HREF / PCLK | 6 / 7 / 13 |
| | D0-D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| **TFT SPI** | MOSI / SCLK / CS / DC / RST / BL | 35 / 36 / 37 / 38 / 39 / 40 |
| **SD Card SPI** | MISO / MOSI / SCLK / CS | 41 / 42 / 2 / 1 |
| **Joystick** | UP / DOWN / LEFT / RIGHT / PRESS | 3 / 21 / 33 / 34 / 47 |
| **Misc** | Buzzer / LED / Button(ESC) | 14 / 48 / 0 |

> 詳細 GPIO 分配請見 [docs/gpio-map.md](docs/gpio-map.md)

## UI Pages

| 頁面 | 功能 |
|------|------|
| 首頁 | 掃描 / 拍照辨識 / 設定（列表選單） |
| 掃描 | Camera 預覽 + 模式切換（查詢/輸入/盤點）|
| 查詢結果 | 品名/規格/數量/儲位/供應商/入庫日 |
| 盤點清單 | 累積品項列表 + 上傳 |
| 拍照辨識 | 拍照 → 上傳 → AI OCR |
| 設定 | WiFi / MQTT / BLE / 語言 / 亮度 / 音量 |
| 虛擬鍵盤 | 英數輸入（設定用）|
| 工作日誌 | 每日操作紀錄瀏覽 |

> 詳細 UI 設計請見 [docs/UI-design-zh.md](docs/UI-design-zh.md) / [docs/UI-design-en.md](docs/UI-design-en.md)

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
│   │   └── config.h        ← 系統設定 (WiFi/MQTT/BLE)
│   ├── display/
│   │   └── display.h/.cpp  ← TFT + LVGL 驅動 (LovyanGFX)
│   ├── camera/
│   │   └── camera.h/.cpp   ← OV2640 攝影機驅動
│   ├── storage/
│   │   ├── sd_card.h/.cpp  ← SD 卡 SPI 驅動
│   │   └── scan_log.h/.cpp ← 工作日誌 + 離線暫存
│   ├── decoder/
│   │   └── barcode_decoder.h/.cpp ← 條碼解碼 (quirc + ZBar)
│   ├── network/
│   │   ├── wifi_manager.h/.cpp   ← WiFi 連線管理
│   │   ├── mqtt_client.h/.cpp    ← MQTT 收發
│   │   ├── ble_hid.h/.cpp        ← BLE HID 鍵盤模擬
│   │   └── http_upload.h/.cpp    ← HTTP POST 圖片上傳
│   ├── input/
│   │   └── joystick.h/.cpp       ← 搖桿輸入 + debounce
│   └── ui/
│       ├── ui_main.h/.cpp        ← LVGL 頁面管理
│       └── i18n.h/.cpp           ← 多語系字串表
├── docs/
│   ├── UI-design-zh.md     ← UI 設計文件（中文）
│   ├── UI-design-en.md     ← UI 設計文件（英文）
│   └── gpio-map.md         ← GPIO 完整分配表
├── README.md
├── TODO.md                 ← 開發待辦事項
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
#define BLE_DEVICE_NAME "SoftSnail"
```

## MQTT Topics

| Topic | Direction | Description |
|-------|-----------|-------------|
| `warehouse/query` | ESP32 → Server | 條碼查詢請求 |
| `warehouse/scan-input/{device_id}` | ESP32 → Server | 掃描輸入推送至 Web UI |
| `warehouse/inventory/{device_id}` | ESP32 → Server | 盤點清單上傳 |
| `warehouse/response/{device_id}` | Server → ESP32 | 回傳查詢結果 |

## HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/photo-recognize` | POST | 上傳圖片 → AI OCR 辨識 |

## Dependencies

- [LVGL v8.4](https://github.com/lvgl/lvgl) — GUI framework
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) — TFT display driver
- [ArduinoJson v7](https://github.com/bblanchon/ArduinoJson) — JSON serialization
- [PubSubClient](https://github.com/knolleary/pubsubclient) — MQTT client
- [esp32-camera](https://github.com/espressif/esp32-camera) — Camera driver
- [quirc](https://github.com/dlbeer/quirc) — QR code decoder
- [ESP32-BLE-Keyboard](https://github.com/T-vK/ESP32-BLE-Keyboard) — BLE HID keyboard

## Related Projects

- **[barcode-warehouse-server](https://github.com/JiangAlex/barcode-warehouse-server)** — 後端服務（FastAPI + PostgreSQL + MQTT + AI OCR）

## Communication

- **技術解釋**使用「繁體中文」
- **變數名稱**、**函數名稱**與**代碼註釋**必須保持英文

## 決策記錄

- **2026-08-24**：硬體平台從 ESP32-S3-DevKitC-1 (N16R8) 分體式組裝，改為微雪 ESP32-S3-Touch-LCD-2 一體板。原因：整合度高（螢幕+觸控+相機介面+SD卡+電池充放電全板載），免杜邦線，解析度相同 240×320，適合產品原型。
- **硬體變更**：ILI9341→ST7789T3、搖桿→電容觸控 (CST816D)、OV2640→OV5640
- **移植文件**：[PORTING.md](PORTING.md)
- **2026-09-26**（Redmine #61）：掃描成功率提升。新增自製 1D line-scan 解碼器（`lib/barcode1d/`，EAN-13/UPC-A 用 module-grid 取樣、Code128 用寬度模式，host 測試 24/24）；解碼管線改 ROI Otsu + QR 多閾值重試；低解析度標籤自動切 SVGA 800×600 單張精解；OV5640 AF 透過公開 `sensor_t` set_reg/get_reg 自行實作（firmware vendored），AF 觸發資料驅動（開機探測 `[AF] RESULT` 決定啟用）。詳見 [docs/memory.md](docs/memory.md) Session 2026-09-26。

## License

MIT
