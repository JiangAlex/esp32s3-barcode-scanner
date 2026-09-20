# 移植記錄：ESP32-S3-Touch-LCD-2（微雪）

> 日期：2026-08-24
> 目標：將 esp32s3-barcode-scanner 從 ESP32-S3-DevKitC-1 (N16R8) 分體式硬體移植到微雪 ESP32-S3-Touch-LCD-2 一體板

## 新硬體規格

| 項目 | 規格 |
|------|------|
| MCU | ESP32-S3R8（Dual-core LX7, 240MHz）|
| Flash | 16MB |
| PSRAM | 8MB (Octal SPI) |
| Display | 2" IPS 240×320, ST7789T3, SPI |
| Touch | CST816D 電容觸控 (I2C) |
| Camera | OV5640（24Pin FPC，相容 OV2640）|
| SD Card | TF 卡槽 (SPI) |
| IMU | QMI8658 6-axis (3-axis accel + 3-axis gyro) |
| Battery | 3.7V MX1.25 鋰電池充放電 |
| USB | Type-C（電源/下載/除錯）|
| Wi-Fi | 802.11 b/g/n |
| BLE | Bluetooth 5.0 |
| GPIO 可用 | 22 pins |

## 新硬體 GPIO Pinout

### Camera (OV5640/OV2640) — 從官方 10_camera_web demo 取得

| Signal | GPIO | Notes |
|--------|------|-------|
| PWDN | 17 | Power down control |
| RESET | -1 | Software reset |
| XCLK | 8 | Camera clock output |
| SIOD (SDA) | 21 | SCCB I2C data |
| SIOC (SCL) | 16 | SCCB I2C clock |
| D0 (Y2) | 12 | |
| D1 (Y3) | 13 | |
| D2 (Y4) | 15 | |
| D3 (Y5) | 11 | |
| D4 (Y6) | 14 | |
| D5 (Y7) | 10 | |
| D6 (Y8) | 7 | |
| D7 (Y9) | 2 | |
| VSYNC | 6 | |
| HREF | 4 | |
| PCLK | 9 | |

### LCD (ST7789T3, SPI) — ✅ 已從官方 Demo 原始碼確認

> 來源：`Arduino/examples/02_gfx_helloworld/02_gfx_helloworld.ino`、`01_factory/bsp_lv_port.h`、`06_lvgl_battery`、`07_lvgl_brightness`（多個 demo 交叉驗證一致）

| Signal | GPIO | Notes |
|--------|------|-------|
| DC | 42 | EXAMPLE_PIN_NUM_LCD_DC |
| CS | 45 | EXAMPLE_PIN_NUM_LCD_CS |
| SCLK | 39 | EXAMPLE_PIN_NUM_LCD_SCLK（與 SD 共用）|
| MOSI | 38 | EXAMPLE_PIN_NUM_LCD_MOSI（與 SD 共用）|
| MISO | 40 | EXAMPLE_PIN_NUM_LCD_MISO（與 SD 共用）|
| RST | -1 | EXAMPLE_PIN_NUM_LCD_RST（軟體 reset，無獨立腳）|
| BL | 1 | EXAMPLE_PIN_NUM_LCD_BL（背光 PWM，LEDC 5kHz / 10-bit）|

- Driver: `Arduino_ST7789`，`IPS = true`，`rotation = 1`（landscape）或 `0`（portrait，factory 用 0）
- 解析度：H_RES=240、V_RES=320

> ⚠️ **重要：LCD 與 SD Card 共用同一組 SPI bus**（SCLK=39 / MOSI=38 / MISO=40），僅 CS 分開（LCD CS=45、SD CS=41）。
> 與原始 DevKitC-1 版本「TFT 和 SD 各用獨立 SPI bus」不同，移植時必須做 CS 分時管理，
> 或使用同一個 SPI host 依 CS 切換裝置。LovyanGFX 與 SD 庫需共用 bus 設定。

### Touch (CST816D, I2C) — ✅ 已確認

> 來源：`01_factory/bsp_i2c.h`、`06_lvgl_battery`、`07_lvgl_brightness`、`libraries/bsp_cst816/bsp_cst816.h`

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA | 48 | EXAMPLE_PIN_NUM_I2C_SDA / TP_SDA（與 IMU 共用 I2C bus）|
| SCL | 47 | EXAMPLE_PIN_NUM_I2C_SCL / TP_SCL（與 IMU 共用 I2C bus）|
| INT | -1 | 官方未使用中斷腳，採 I2C 輪詢讀取觸控座標 |
| RST | -1 | EXAMPLE_PIN_NUM_TP_RST（無獨立 reset 腳）|

- Library: `bsp_cst816`（微雪提供離線安裝）
- I2C 位址：`0x15`（CST816_ADDR）

### SD Card (TF, SPI) — ✅ 已確認

> 來源：`Arduino/examples/03_sd_card_test/03_sd_card_test.ino`（REASSIGN_PINS 區塊）

| Signal | GPIO | Notes |
|--------|------|-------|
| MISO | 40 | 與 LCD 共用 |
| MOSI | 38 | 與 LCD 共用 |
| SCLK | 39 | 與 LCD 共用 |
| CS | 41 | SD 專用 CS |

- 初始化：`SPI.begin(39, 40, 38, 41)` → `SD.begin(41)`
- FAT32 格式

### IMU (QMI8658, I2C) — ✅ 已確認

> 來源：`04_qmi8658_output.ino`、`01_factory/src/app/app_qmi8658.cpp`

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA | 48 | 與觸控共用 I2C bus |
| SCL | 47 | 與觸控共用 I2C bus |

- Library: `FastIMU`
- I2C 位址：`0x6B`（IMU_ADDRESS）
- 本專案為條碼掃描器，IMU 非必要功能，可暫不啟用

### 其他 (Battery / BOOT)

| Signal | GPIO | Notes |
|--------|------|-------|
| Battery ADC | 5 | EXAMPLE_PIN_NUM_BAT（06_lvgl_battery，電池電壓量測）|
| BOOT button | 0 | ESP32-S3 晶片級固定腳，factory demo：單擊=下、雙擊=上、長按=確認 |

## 與原始設計的差異比較

### 硬體差異

| 項目 | 原始 (DevKitC-1 分體式) | 新 (微雪一體板) |
|------|------------------------|----------------|
| Display IC | ILI9341 | ST7789T3 |
| Display Size | 2.8" | 2" |
| Resolution | 240×320 | 240×320 ✅ 相同 |
| Camera | OV2640 | OV5640（可降級用 OV2640）|
| Touch | 無（XPT2046 未啟用）| CST816D 電容觸控 |
| Input | 5-way Joystick (GPIO) | 觸控螢幕 |
| SD Bus | SPI3 (VSPI) 獨立 | ✅ 與 LCD 共用同一 SPI bus（CS 分開）|
| Buzzer | GPIO14, PWM | 無板載，需外接 |
| LED | GPIO48 | 無板載（可用空閒 GPIO）|
| Battery | 無 | 3.7V 鋰電池充放電 |
| IMU | 無 | QMI8658 |

### GPIO 分配差異

| 功能 | 原 GPIO | 新 GPIO |
|------|---------|---------|
| CAM XCLK | 15 | 8 |
| CAM SIOD | 4 | 21 |
| CAM SIOC | 5 | 16 |
| CAM VSYNC | 6 | 6 (同) |
| CAM HREF | 7 | 4 |
| CAM PCLK | 13 | 9 |
| CAM D0 | 11 | 12 |
| CAM D1 | 9 | 13 |
| CAM D2 | 8 | 15 |
| CAM D3 | 10 | 11 |
| CAM D4 | 12 | 14 |
| CAM D5 | 18 | 10 |
| CAM D6 | 17 | 7 |
| CAM D7 | 16 | 2 |
| CAM PWDN | -1 | 17 |
| TFT SCLK/MOSI/MISO | 36/35/- | 39/38/40（與 SD 共用）|
| TFT DC/CS/RST/BL | 38/37/39/40 | 42/45/-1/1 |
| SD SCLK/MOSI/MISO/CS | 2/42/41/1 | 39/38/40/41（前三與 LCD 共用）|
| Touch/IMU I2C SDA/SCL | 無 | 48/47（共用）|
| Battery ADC | 無 | 5 |
| Joystick | 3,21,33,34,47 | 移除（改用觸控）|
| Buzzer | 14 | 移除或外接 |

## 程式碼修改清單

### 必須修改的檔案

| 檔案 | 修改內容 |
|------|----------|
| `platformio.ini` | board 設定不變，新增 GFX_Library_for_Arduino 依賴，移除搖桿相關 |
| `src/config/pinout.h` | 完全重寫所有 GPIO 定義 |
| `src/config/config.h` | Camera 改用 OV5640 設定 |
| `src/display/display.cpp` | ILI9341→ST7789T3，LovyanGFX→Arduino_GFX 或修改 LovyanGFX config |
| `src/camera/camera.cpp` | 更新所有 GPIO pin，OV5640 sensor 設定 |
| `src/storage/sd_card.cpp` | 更新 SPI GPIO |
| `src/ui/ui_main.cpp` | 觸控操作適配（tabview 本身支援觸控滑動）|

### 新增的檔案

| 檔案 | 用途 |
|------|------|
| `src/input/touch.cpp/.h` | CST816D 觸控驅動 + LVGL indev |

### 移除/廢棄的檔案

| 檔案 | 原因 |
|------|------|
| `src/input/joystick.cpp/.h` | 搖桿硬體移除，改用觸控 |

## 軟體依賴變更

### 新增 Libraries

| Library | 用途 | 安裝方式 |
|---------|------|----------|
| GFX_Library_for_Arduino v1.5.0 | ST7789T3 LCD 驅動 | PlatformIO lib_deps |
| bsp_cst816 | CST816D 觸控驅動 | 離線安裝（微雪提供）|
| FastIMU v1.2.6 | QMI8658 IMU（選配）| PlatformIO lib_deps |

### 可能移除的 Libraries

| Library | 原因 |
|---------|------|
| LovyanGFX | 改用 Arduino_GFX（或保留 LovyanGFX 並改 config）|

### 保留不變的 Libraries

| Library | 用途 |
|---------|------|
| lvgl v8.4 | GUI framework |
| ArduinoJson v7 | JSON |
| PubSubClient | MQTT |
| esp32-camera | Camera driver |
| quirc | QR code decoder |
| ESP32-BLE-Keyboard | BLE HID |

## 移植步驟

1. ✅ 研究微雪硬體 pinout 和 demo 原始碼
2. ✅ 下載官方 Demo，確認 LCD/SD/Touch/IMU 的確切 GPIO（見上方各表）
3. ✅ 更新 `platformio.ini`（加 `-Isrc`；LovyanGFX 保留，觸控自寫驅動）
4. ✅ 重寫 `pinout.h`（全部微雪板 GPIO，共用 SPI/I2C bus）
5. ✅ 更新 `config.h`（DISP_INVERT/DISP_BUS_SHARED、OV5640、可選 buzzer）
6. ✅ 改寫 `display.cpp`（Panel_ST7789 + invert=true + bus_shared=true + 觸控 indev）
7. ✅ 改寫 `camera.cpp`（OV5640/OV2640 自動辨識 + 新 GPIO）
8. ✅ 改寫 `sd_card.cpp`（SPIClass(FSPI) 與 LCD 共用 host）
9. ✅ 新增 `touch.cpp/.h`（CST816D I2C 輪詢驅動 + rotation 轉換 + LVGL POINTER indev）
10. ✅ 移除搖桿程式碼（原始 `src/input/` 本無 joystick 檔，無需移除）
11. ✅ 更新 UI（ui_main 純 widget，觸控 indev 於 display 層註冊，UI 邏輯無需改）
12. ✅ Build 驗證（`pio run` SUCCESS：RAM 46.1%、Flash 36.0%）
13. [ ] 更新 README 和文件

### 移植期間順手修復的既有問題（與硬體移植無關，但阻擋 build）

- `barcode_decoder.cpp`：蜂鳴器改為 `BUZZER_ENABLED`（預設 0）條件編譯，因新板無板載蜂鳴器
- `scan_log.cpp`：補上缺少的 `#include <SD.h>`
- **quirc 依賴缺失**：`lib_deps` 從未包含 QR 解碼器。已將官方 quirc 核心原始碼放入 `lib/quirc/`（quirc.h/quirc_internal.h/quirc.c/decode.c/identify.c/version_db.c + LICENSE）

### 尚待實機驗證（無實體硬體，僅完成編譯層級驗證）

- ST7789T3 顯示方向 / 色彩反相（`DISP_INVERT`）是否正確
- CST816D 觸控座標對位（rotation=0）
- LCD + SD 共用 SPI bus 實際交錯存取穩定性

## 注意事項

1. **LCD 驅動選擇**：微雪 demo 用 `GFX_Library_for_Arduino` + `Arduino_ESP32SPI`，但我們專案用 `LovyanGFX`。兩者都支援 ST7789，建議保留 LovyanGFX 只需改 Panel class 為 `Panel_ST7789` + `cfg.invert = true`。
2. **SPI bus 共用**：需確認 LCD 和 SD 是否共用 SPI bus。若共用需做 CS 管理。
3. **Camera OV5640 vs OV2640**：`esp_camera` 庫同時支援兩者，config 基本相同。OV5640 支援更高解析度但條碼掃描用 QVGA 即可。
4. **觸控 indev**：LVGL tabview 原生支援觸控滑動和點擊，UI 邏輯改動很小。
5. **Buzzer**：新板無板載蜂鳴器。可外接到空閒 GPIO 或改用軟體音效（透過 IMU 震動回饋不適用），或暫時移除。

## 參考資源

- [微雪 ESP32-S3-Touch-LCD-2 Wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2)
- [官方 Demo 下載](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2/ESP32-S3-Touch-LCD-2-Demo.zip)
- [電路圖 (SchDoc PDF)](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-2/ESP32-S3-Touch-LCD-2-SchDoc.pdf)
- [ESP32-S3 Datasheet](https://files.waveshare.com/wiki/common/Esp32-s3_datasheet_en.pdf)
