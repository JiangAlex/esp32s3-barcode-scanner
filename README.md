# esp32s3-barcode-scanner

ESP32-S3 多功能條碼掃描器韌體 — 掃描查詢倉管 / BLE 無線條碼槍 / 盤點 / 拍照 AI 辨識

## Features

- **OV5640 攝影機** — 即時掃描 QR Code（quirc）與 1D 條碼 EAN-13 / UPC-A / Code128（自製 line-scan 解碼器）。低解析度標籤自動切 SVGA 800×600 單張精解提升成功率；OV5640 AF 版鏡頭支援單次對焦
- **LVGL 圖形介面** — 2.8" TFT 240×320，BOOT 單鍵操作（觸控硬體不通），中英文 i18n 切換
- **掃描查詢模式** — 掃描條碼透過 MQTT 查詢後端倉管資料庫，顯示完整品項資訊
- **BLE HID 無線條碼槍** — NimBLE 模擬藍牙鍵盤，掃描值直接輸出到任何電腦/手機的游標位置（US layout + Enter）。INPUT 模式按需啟動 BLE；單一 2.4GHz radio 下 BLE 期間暫停 WiFi/MQTT
- **MQTT 輸入模式** — 推送掃描值至 Web UI 欄位
- **盤點模式** — 連續掃描累積清單，批次上傳 server
- **拍照 AI 辨識** — 拍照傳至 server，AI OCR 自動填入品項單據
- **離線暫存** — WiFi 斷線時自動暫存 SD 卡，恢復連線後自動同步
- **工作日誌** — 每次掃描依模式（查詢/輸入/進貨/出貨/盤點）記錄到 SD 卡（每日 `/log_YYYY-MM-DD.json`，NTP 時間戳），LOG 頁可逐日瀏覽
- **SD 卡存取** — 離線暫存 + 工作日誌 + UI 圖片資源

## Hardware

| 元件 | 規格 |
|------|------|
| 開發板 | 微雪 Waveshare ESP32-S3-Touch-LCD-2 一體板 (ESP32-S3R8, 16MB Flash, 8MB Octal PSRAM) |
| Camera | OV5640 5MP (RGB565 for preview/scan, SVGA 800×600 for hi-res decode；AF 版支援 VCM 對焦) |
| Display | 2.8" TFT LCD 240×320 (ST7789T3, SPI — 與 SD 卡共用 SPI bus) |
| Touch | CST816D 電容觸控 (I2C，與 IMU 共用 bus)；⚠️ 目前板上硬體不通，暫以 BOOT 鍵操作 |
| Input | BOOT button (GPIO0) 單鍵操作 |
| IMU | QMI8658 六軸 (I2C @0x6B) |
| Storage | MicroSD Card (SPI，與 LCD 共用 bus) |
| Battery | 鋰電池充放電管理 + ADC 電量偵測 (GPIO5) |
| Wireless | WiFi (2.4GHz) + BLE 5.0 (HID Keyboard，NimBLE)；⚠️ 單一 radio，INPUT/BLE 模式會暫停 WiFi |

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

## Pin Map (Waveshare ESP32-S3-Touch-LCD-2, ESP32-S3R8)

> 來源：`src/config/pinout.h`（對照官方 Waveshare demo 驗證）

| Module | Signal | GPIO |
|--------|--------|------|
| **Camera (OV5640)** | XCLK / SIOD / SIOC | 8 / 21 / 16 |
| | VSYNC / HREF / PCLK | 6 / 4 / 9 |
| | D0-D7 | 12, 13, 15, 11, 14, 10, 7, 2 |
| | PWDN | 17 |
| **Shared SPI (LCD + SD)** | SCLK / MOSI / MISO | 39 / 38 / 40 |
| **TFT LCD (ST7789T3)** | CS / DC / BL | 45 / 42 / 1 |
| **SD Card** | CS | 41 |
| **Shared I2C (Touch + IMU)** | SDA / SCL | 48 / 47 |
| **Touch (CST816D)** | I2C addr | 0x15 |
| **IMU (QMI8658)** | I2C addr | 0x6B |
| **Battery** | ADC | 5 |
| **Misc** | BOOT button | 0 |

> ⚠️ LCD 與 SD 卡共用 SPI bus（僅 CS 不同）；Touch 與 IMU 共用 I2C bus。
> GPIO19/20 保留給 USB OTG，GPIO26-32 保留給 Octal PSRAM，GPIO43/44 為 UART0 除錯埠。
>
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
│   │   └── camera.h/.cpp   ← OV5640 攝影機驅動 (RGB565 + VCM 對焦)
│   ├── storage/
│   │   ├── sd_card.h/.cpp  ← SD 卡 SPI 驅動
│   │   └── scan_log.h/.cpp ← 工作日誌 + 離線暫存
│   ├── decoder/
│   │   └── barcode_decoder.h/.cpp ← 條碼解碼 (quirc + ZBar)
│   ├── network/
│   │   ├── wifi_manager.h/.cpp   ← WiFi 連線狀態查詢
│   │   ├── wifi_portal/          ← captive-portal 配網 (WebServer + EEPROM)
│   │   ├── mqtt_client.h/.cpp    ← MQTT 收發
│   │   ├── ble_hid.h/.cpp        ← BLE HID 鍵盤 (NimBLE 薄層，INPUT 無線條碼槍)
│   │   └── http_upload.h/.cpp    ← HTTP POST 圖片上傳
│   ├── input/
│   │   └── touch.h/.cpp          ← CST816D 觸控輸入 (⚠️ 硬體不通)
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
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) — BLE stack (自寫薄層 HID keyboard)

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
- **2026-10-21**（Redmine #101）：SD 工作日誌接通 + 實機除錯。後端 `scan_log`/`sd_card` 原本完整但從未被 main 呼叫；接進 setup + `ui_on_scan` 每次掃描記錄 + WiFi 連線 NTP 校時。修 3 個韌體 bug（路徑重複 `/sd/sd/`、`SD.mkdir` 全失敗改扁平根目錄檔、`readString()` 回空改 size-based read）。根因級發現：2 張故障/山寨卡（寫入配置 cluster 但 size 恆 0），加開機寫入健檢自動偵測停用。換新 SDHC 卡實機驗證通過。詳見 [docs/memory.md](docs/memory.md) Session 2026-10-20。
- **2026-10-21**（Redmine #102）：BLE HID 無線條碼槍從零實作（README 核心賣點）。NimBLE-Arduino 自寫薄層 HID keyboard（避開 T-vK lib 對 arduino-esp32 3.x 的相容債）。依序突破 5 關：編譯、WiFi/BLE 共存 abort（`esp_wifi_deinit` 釋放 WiFi）、廣播無名（`adv->setName`）、HID 連線（Just-Works IO cap）、INPUT 模式 OOM（釋放 WiFi DRAM + framebuffer 移 PSRAM）。按需啟動（進 INPUT begin / 離開 stop），狀態列 [B] 圖示。共存壓測修 2 個 crash（onDisconnect 重廣播、刪靜態 callbacks）。手機配對實機驗證：掃描值完整打進文字框。**設計取捨**：單一 radio + DRAM 限制下，BLE 槍模式暫停 WiFi/MQTT。詳見 [docs/memory.md](docs/memory.md) Session 2026-10-21。
- **2026-10-21**：scan-input（INPUT MQTT 子通道 → Web UI 即時填欄位，跨專案）。INPUT 模式拆為 `INPUT-BLE`（無線條碼槍）/`INPUT-MQTT`（推送 Web UI）兩子通道，長按循環切換、互斥（BLE 子通道起 BLE 關 WiFi；MQTT 子通道保留 WiFi）。韌體 `mqtt_scan_input()` publish `warehouse/scan-input/{device_id}`；server（barcode-warehouse-server）以 `scan_broadcaster`（asyncio.Queue + call_soon_threadsafe）+ SSE `/web/scan-stream` 即時推送到 item 表單欄位。server 本機端到端驗證、韌體實機 publish（live broker mosquitto_sub 收到）皆通過。詳見 [docs/memory.md](docs/memory.md) Session 2026-10-21 (B)。

## License

MIT
