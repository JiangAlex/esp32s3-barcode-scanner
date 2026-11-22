# esp32s3-barcode-scanner

ESP32-S3 多功能條碼掃描器韌體 — 掃描查詢倉管 / BLE 無線條碼槍 / 盤點 / 拍照 AI 辨識

## Features

- **QL3301L 二維碼掃描模組** — 外接 UART 掃描頭（TTL-232，9600 8N1），模組內部解 1D/2D 條碼後輸出字串；SCAN 頁短按觸發。兩個硬體版本都使用
- **OV5640 攝影機**（僅 Touch-LCD-2） — 即時掃描 QR Code（quirc）與 1D 條碼 EAN-13 / UPC-A / Code128（自製 line-scan 解碼器）。低解析度標籤自動切 SVGA 800×600 單張精解提升成功率；OV5640 AF 版鏡頭支援單次對焦
- **LVGL 圖形介面** — 240×320 TFT（Touch-LCD-2 為 2"、LCD-2.8 為 2.8"），BOOT 單鍵操作（觸控硬體不通），中英文 i18n 切換
- **掃描查詢模式** — 掃描條碼透過 MQTT 查詢後端倉管資料庫，顯示完整品項資訊
- **BLE HID 無線條碼槍** — NimBLE 模擬藍牙鍵盤，掃描值直接輸出到任何電腦/手機的游標位置（US layout + Enter）。INPUT 模式按需啟動 BLE；單一 2.4GHz radio 下 BLE 期間暫停 WiFi/MQTT
- **MQTT 輸入模式** — 推送掃描值至 Web UI 欄位
- **盤點模式** — 連續掃描累積清單，批次上傳 server
- **拍照 AI 辨識** — 拍照傳至 server，AI OCR 自動填入品項單據
- **離線暫存** — WiFi 斷線時自動暫存 SD 卡，恢復連線後自動同步
- **工作日誌** — 每次掃描依模式（查詢/輸入/進貨/出貨/盤點）記錄到 SD 卡（每日 `/log_YYYY-MM-DD.json`，NTP 時間戳），LOG 頁可逐日瀏覽
- **SD 卡存取** — 離線暫存 + 工作日誌 + UI 圖片資源

## Hardware

硬體有兩個版本，以 `platformio.ini` 的 env 區分（見 [Build & Upload](#build--upload)）：

| 元件 | `lcd2`：ESP32-S3-Touch-LCD-2（預設） | `lcd28`：ESP32-S3-LCD-2.8 |
|------|--------------------------------------|---------------------------|
| 開發板 | 微雪 Waveshare 一體板，ESP32-S3R8，16MB Flash，8MB Octal PSRAM | 同左 |
| 掃描 | **QL3301L**（UART）+ OV5640 相機即時 QR | **QL3301L**（UART） |
| Camera | OV5640 5MP（RGB565 預覽 / 掃描；AF 版支援 VCM 對焦） | 無 |
| Display | 2" IPS 240×320，ST7789T3，SPI（與 SD 卡共用 bus） | 2.8" 240×320，ST7789，SPI（獨立 bus） |
| Touch | CST816D（I2C）；⚠️ 板上硬體不通 | 無（官方範例無觸控驅動） |
| Input | BOOT button (GPIO0) 單鍵操作 | 同左 |
| IMU / RTC | QMI8658（I2C @0x6B） | QMI8658（@0x6B）+ PCF85063 RTC（@0x51） |
| Storage | MicroSD（SPI，與 LCD 共用 bus） | MicroSD（SD_MMC 1-bit） |
| Battery | 鋰電池充放電（ADC GPIO5，韌體未讀取） | 鋰電池充放電（ADC GPIO8，韌體未讀取）；電源鎖存 GPIO7 |
| Wireless | WiFi (2.4GHz) + BLE 5.0 (HID Keyboard，NimBLE)；⚠️ 單一 radio，INPUT/BLE 模式會暫停 WiFi | 同左 |

### QL3301L 掃描模組

| 項目 | 內容 |
|------|------|
| 介面 | TTL-232 UART，**9600 8N1**，掃碼資料以 CR（0x0D）結尾 |
| 觸發 | TRIG 腳低電平（出廠為手動、電平觸發），單次讀碼最長 5 s |
| 供電 | 本專案接 **5V**（⚠️ 原廠手冊寫 VCC +3.3V；ESP32-S3 GPIO 不耐 5V，接 RX 前先量模組 TX 電壓） |
| 首次設定 | ⚠️ 出廠為 **USB-HID 輸出**，須先掃配置手冊 2.1.1「TTL-232 串口模式」設定碼（第 40 頁） |
| 文件 | `QL3301L集成手册.pdf`、`小模块配置手册_V2.0.3.pdf`；規格、腳位、串口指令整理於 [docs/memory.md](docs/memory.md) |

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

## Pin Map

### QL3301L 接線（兩個版本相同）

| QL3301L（12PIN FPC 0.5 mm） | ESP32-S3 | Touch-LCD-2 排針 | LCD-2.8 12PIN 介面 |
|------------------------------|----------|------------------|--------------------|
| PIN5 UART_TXD | GPIO44（RX） | pin 4 RXD | RXD |
| PIN4 UART_RXD | GPIO43（TX） | pin 3 TXD | TXD |
| PIN12 TRIG | GPIO18 | pin 23 | IO18 |
| PIN2 VCC | 5V | pin 15 | VBus |
| PIN3 / PIN8 GND | GND | pin 2 / 13 / 16 | GND |

> 序列埠 log 走 USB-CDC，所以 GPIO43/44（UART0）可給模組用（`Serial1`）。

### Waveshare ESP32-S3-Touch-LCD-2（`lcd2`）

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
| **QL3301L** | RX / TX / TRIG | 44 / 43 / 18 |

> ⚠️ LCD 與 SD 卡共用 SPI bus（僅 CS 不同）；Touch 與 IMU 共用 I2C bus。
> GPIO19/20 保留給 USB OTG，GPIO26-32 保留給 Octal PSRAM。
>

### Waveshare ESP32-S3-LCD-2.8（`lcd28`）

> 來源：`src/config/pinout.h`（對照官方 `ESP32-S3-LCD-2.8-Demo.zip` Arduino 範例）

| Module | Signal | GPIO |
|--------|--------|------|
| **TFT LCD (ST7789)** | SCLK / MOSI / CS / DC / RST / BL | 40 / 45 / 42 / 41 / 39 / 5 |
| **SD Card (SD_MMC 1-bit)** | CLK / CMD / D0 / D3 | 14 / 17 / 16 / 21 |
| **I2C (IMU + RTC)** | SDA / SCL | 11 / 10 |
| **Power** | PWR 鍵 / 電源鎖存 | 6 / 7 |
| **Battery** | ADC | 8 |
| **Audio (PCM5101，未使用)** | DOUT / BCLK / LRC | 47 / 48 / 38 |
| **Misc** | BOOT button | 0 |
| **QL3301L** | RX / TX / TRIG | 44 / 43 / 18 |

> ⚠️ LCD-2.8 的 USB 供電也經過 GPIO7 電源鎖存：板上是 Waveshare 出廠範例時，要**按住 BOOT 再插 USB** 才能燒錄；燒入本韌體後開機會自動鎖住電源。
>
> [docs/gpio-map.md](docs/gpio-map.md) 是最早 ESP32-S3-DevKitC-1 分體式硬體的分配表，**不適用**目前兩個版本。

## UI Pages

| 頁面 | 功能 |
|------|------|
| 首頁 | 掃描 / 盤點 / 設定 / 測試 / 關於（列表選單） |
| 掃描 | 短按觸發 QL3301L；長按切換模式（查詢/輸入/進貨/出貨/盤點）；Touch-LCD-2 另有相機預覽 |
| 查詢結果 | 品名/規格/數量/儲位/供應商/入庫日 |
| 盤點清單 | 累積品項列表 + 上傳 |
| 拍照辨識 | 拍照 → 上傳 → AI OCR |
| 設定 | WiFi / MQTT / BLE / 語言 / 亮度 / 音量 |
| 虛擬鍵盤 | 英數輸入（設定用）|
| 工作日誌 | 每日操作紀錄瀏覽 |
| 測試 | 掃描測試：顯示每筆收到的條碼（內容 / 類型 / 長度 / 筆數 + 最近 3 筆），不記錄、不查詢、不送出；短按觸發 QL3301L、長按返回 |

> 詳細 UI 設計請見 [docs/UI-design-zh.md](docs/UI-design-zh.md) / [docs/UI-design-en.md](docs/UI-design-en.md)

## Project Structure

```
esp32s3-barcode-scanner/
├── platformio.ini          ← PlatformIO 設定（env: lcd2 / lcd28）
├── partitions.csv          ← Flash 分區表 (16MB)
├── lv_conf.h               ← LVGL 配置
├── src/
│   ├── main.cpp            ← 主程式進入點
│   ├── config/
│   │   ├── pinout.h        ← GPIO 定義 + 功能旗標（依 BOARD_LCD28 分兩版）
│   │   └── config.h        ← 系統設定 (WiFi/MQTT/BLE)
│   ├── display/
│   │   └── display.h/.cpp  ← TFT + LVGL 驅動 (LovyanGFX)
│   ├── camera/
│   │   └── camera.h/.cpp   ← OV5640 攝影機驅動 (RGB565 + VCM 對焦)
│   ├── storage/
│   │   ├── sd_card.h/.cpp  ← SD 卡驅動（lcd2: SPI / lcd28: SD_MMC，`SD_FS` 別名）
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
│   │   ├── qr_scanner.h/.cpp     ← QL3301L 掃描模組 (UART 讀取 + TRIG)
│   │   └── touch.h/.cpp          ← CST816D 觸控輸入 (⚠ 硬體不通；lcd28 不編譯)
│   └── ui/
│       ├── ui_main.h/.cpp        ← LVGL 頁面管理
│       └── i18n.h/.cpp           ← 多語系字串表
├── docs/
│   ├── UI-design-zh.md     ← UI 設計文件（中文）
│   ├── UI-design-en.md     ← UI 設計文件（英文）
│   ├── gpio-map.md         ← GPIO 分配表（⚠ 舊 DevKitC 硬體）
│   └── memory.md           ← 專案記憶（規格 / 硬體 / session 紀錄）
├── README.md
└── TODO.md                 ← 開發待辦事項
```

## Build & Upload

硬體有兩個版本，由 `platformio.ini` 的 env 區分（腳位與功能旗標在 `src/config/pinout.h`，以 `-D BOARD_LCD28` 切換）：

| env | 硬體 | 掃碼來源 |
|-----|------|----------|
| `lcd2`（預設） | Waveshare ESP32-S3-Touch-LCD-2 | QL3301L（UART）+ 相機即時 QR |
| `lcd28` | Waveshare ESP32-S3-LCD-2.8 | QL3301L（UART），無相機 / 觸控 |

QL3301L 接線兩版相同：模組 TXD → GPIO44、RXD ← GPIO43、TRIG ← GPIO18（詳見 [docs/memory.md](docs/memory.md)）。

```bash
# Build（不帶 -e 時只編預設的 lcd2）
pio run -e lcd2         # ESP32-S3-Touch-LCD-2
pio run -e lcd28        # ESP32-S3-LCD-2.8

# Upload
pio run -e lcd2 -t upload
pio run -e lcd28 -t upload

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
- **2026-11-22**：掃描來源改為外接 **QL3301L 二維碼掃描模組**（TTL-232 UART，模組內部解碼），兩個硬體版本共用同一組 GPIO（RX 44 / TX 43 / TRIG 18）。原因：相機 1D 解碼受定焦 / 光學限制（見 2026-09-27、2026-10-03 session）。新增第二個硬體版本 **ESP32-S3-LCD-2.8**（`env:lcd28`，無相機 / 觸控，SD 改 SD_MMC）；原 Touch-LCD-2 env 由 `esp32s3` 改名 `lcd2`。兩版皆已實機開機驗證，掃碼端到端待接上模組驗證（Redmine #141）。詳見 [docs/memory.md](docs/memory.md) Session 2026-11-22。

## License

MIT
