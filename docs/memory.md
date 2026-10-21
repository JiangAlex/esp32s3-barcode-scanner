# Memory — ESP32-S3 LVGL Debug Session

## Root Causes Found

### 1. LVGL `LoadProhibited` crash at `lv_indev_read_timer_cb`
- EXCVADDR: `0xb3400013` — unmapped PSRAM address
- Input device function pointer was NULL/invalid when lv_indev timer fired
- **Fix**: Comment out `lv_indev_drv_register()` — only register display driver

### 2. LVGL button color NOT updating on physical LCD
- **Symptom**: `update_btn_ui()` runs correctly (serial shows `bg=0x07E0`), but LCD stays red
- **Root cause**: LVGL invalidation functions (`lv_obj_invalidate`, `lv_obj_invalidate_area`, `lv_disp_invalidate_area`) do NOT trigger `flush_cb`
- Only 4 initial flushes occur (`[FLUSH] #0-#3`), then `flush_cb` is never called again
- `lv_obj_invalidate(lv_scr_act())` causes `LoadProhibited` crash — cannot use screen-level invalidation
- **Tested approaches** (all failed):
  - `lv_obj_set_style_bg_color()` + `lv_obj_invalidate_area()` — style updated, no flush
  - `lv_obj_invalidate(lv_scr_act())` — crashes
  - Delete + recreate button object — `flush_cb` still not called
  - Direct `tft.fillRect()` via LovyanGFX — **WORKING** (LCD updates correctly)

### 3. Build was broken at HEAD (found 2026-09-21)
Two pre-existing compile errors, unrelated to the GPIO0 issue:

- `display_get_tft` was not declared. `main.cpp:55` does
  `static lgfx::LGFX_Device& tft = display_get_tft();` to share the LGFX
  instance, but `display.cpp` only exposed `display_get_disp()` and kept
  `static LGFX tft` private.
  - **Fix**: declared `lgfx::LGFX_Device& display_get_tft(void)` in `display.h`
    (plus `#include <LovyanGFX.hpp>`) and defined it in `display.cpp`.
- `lv_tick_inc` was not declared. `lv_conf.h` had `LV_TICK_CUSTOM 1`, which
  compiles `lv_tick_inc()` out, yet `main.cpp` drives the tick via `esp_timer`
  calling `lv_tick_inc(2)` — the two files contradicted each other.
  - **Fix**: set `LV_TICK_CUSTOM 0` in `lv_conf.h`, matching `main.cpp`'s
    esp_timer design (and the factory `bsp_lv_port.cpp` approach).

Build now succeeds: RAM 37.9% (124304 / 327680), Flash 21.0% (659893 / 3145728).

## Board Status

- **ESP32-S3** (Waveshare ESP32-S3-Touch-LCD-2, N8R8 label)
- **Flash**: Winbond W25Q128JVSIQ 16MB
- **PSRAM**: ⚠️ 先前記為「NOT present」是**錯誤**結論。實為 8MB Octal (OPI)，
  只是未在 `platformio.ini` 啟用。詳見下方 "Session 2026-09-22" 根因 #1。
- **I2C devices**: 0x6B (QMI8658 IMU), 0x7E (unknown — GT911 secondary addr?)
- **Touch**: No CST816 @ 0x15, no GT911 readable @ 0x5D or 0x7E

## Hardware Findings

- PSRAM allocation fails → DRAM fallback works
  - `buf1=0x3fcb2420 buf2=0x3fcbba30` (38.4KB each)
- BOOT button (GPIO0) always reads HIGH (1), never becomes LOW (0)
  - **Root cause (firmware, found 2026-09-21)**: `pinMode(PIN_BOOT_BTN, INPUT_PULLUP)`
    was never called. `setup()` only configured GPIO48; `loop()` called
    `digitalRead(PIN_BOOT_BTN)` on an uninitialized pin.
  - GPIO0 is a strapping pin — its pull state after boot is not guaranteed,
    so reads latch HIGH without explicit `pinMode()`.
  - **Fix applied**: added `pinMode(PIN_BOOT_BTN, INPUT_PULLUP)` in `setup()`
  - Previous conclusion ("hardware/board-level wiring issue") was premature —
    it was drawn before the pin was ever initialized.
  - **Still to verify on hardware**: if the button remains HIGH when pressed
    *after* this fix, then it is genuinely a board wiring difference and the
    Waveshare schematic must be checked.
  - Serial 't' command retained as a workaround for toggle testing

## Key Files

- `src/main.cpp` — current working LVGL + LovyanGFX integration
  - `update_btn_ui()` — delete/recreate button + direct `tft.fillRect()`
  - `flush_cb()` — writes pixels to TFT via LovyanGFX SPI
  - `lvgl_task()` — runs on Core 1, processes `g_ui_update_pending`
- `lv_conf.h` — LVGL 8.4.0 config (`LV_TICK_CUSTOM 0` — required by main.cpp)
- `platformio.ini` — flash mode qio, 115200 monitor

## Build Environment

Host: Ubuntu 26.04.1 LTS (Resolute Raccoon), system Python 3.14.

PlatformIO Core was not installed (only toolchain packages existed under
`~/.platformio/packages`). Python 3.14 is PEP 668 externally-managed, so it was
installed into the canonical venv location:

```bash
/usr/bin/python3.14 -m venv ~/.platformio/penv
~/.platformio/penv/bin/python -m pip install platformio   # 6.2.0

# put CLI on PATH (~/.local/bin is already on PATH via ~/.profile)
ln -sf ~/.platformio/penv/bin/pio         ~/.local/bin/pio
ln -sf ~/.platformio/penv/bin/platformio  ~/.local/bin/platformio
ln -sf ~/.platformio/penv/bin/piodebuggdb ~/.local/bin/piodebuggdb

pio run -e esp32s3          # build
pio run -e esp32s3 -t upload # flash
```

### Serial port access (required before flashing)

Board enumerates as `/dev/ttyACM0` — Espressif USB JTAG/serial debug unit,
VID:PID `303A:1001`. Default perms are `root:dialout 0660` and this user is
**not** in `dialout`, so the port is unreadable until the udev rules are added.
ModemManager is also active and can grab the port mid-flash.

The bundled rules file covers `303a:1001` with `MODE="0666"` plus
`ID_MM_DEVICE_IGNORE`, which fixes both problems:

```bash
sudo cp ~/.platformio/penv/lib/python3.14/site-packages/platformio/assets/system/99-platformio-udev.rules \
        /etc/udev/rules.d/99-platformio-udev.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
# then unplug and replug the board
```

## Working Solution (Button Color Update)

```c
// In update_btn_ui() — delete old, recreate new, draw directly
lv_obj_del(g_scan_btn);
g_scan_btn = lv_btn_create(lv_scr_act());
// ... set up button styles and label ...
uint16_t col = g_scanning ? 0xF800 : 0x07E0;
tft.fillRect(20, 130, 200, 60, col);  // Direct LovyanGFX draw
```

## Phase 0~10 Implementation Summary (2026-09-21)

All phases completed and verified via `pio run` build success.

| Phase | Feature | Status |
|-------|---------|--------|
| 0 | Infrastructure — LVGL + LovyanGFX + PlatformIO | ✅ |
| 1 | Scanner power control (GPIO48) | ✅ |
| 2 | LVGL scan result display with direct `tft.fillRect()` | ✅ |
| 3 | Flashlight control | ✅ |
| 4 | Main menu / back navigation | ✅ |
| 5 | Scan history | ✅ |
| 6 | BOOT button interrupt (GPIO0, INPUT_PULLUP) | ✅ |
| 7 | Offline log — daily files + pending queue | ✅ |
| 8 | Photo capture — JPEG SVGA mode + HTTP POST upload | ✅ |
| 9 | Integration — scan modes, offline→online sync, inventory upload | ✅ |
| 10 | Optimization — OTA update, NVS settings, low power, LVGL animations | ✅ |

**Final build**: RAM 37.9% (124304 / 327680), Flash 21.5% (676905 / 3145728)

**Phase 8 新增檔案：**
- `src/network/http_upload.cpp/.h` — WiFiClient HTTP POST multipart upload
- `src/camera/camera.cpp` — `camera_set_jpeg_mode()`, `camera_capture_jpeg()`
- `src/ui/ui_main.cpp` — UI_PAGE_PHOTO 頁面

**Phase 9 新增整合：**
- 掃描模式切換（查詢/輸入/盤點）
- WiFi 重連時自動同步待上傳日誌
- 盤點批次上傳 MQTT

> ⚠️ **更正（2026-10-04，代碼實證）**：上面「Phase 9 整合」的網路部分**標記不實**。
> 實際盤點後代碼確認：
> - `src/network/{wifi_manager,mqtt_client}.cpp` 有實作，但 **`main.cpp` 從未 include/init
>   這些模組** → WiFi/MQTT **從未在韌體中運行**。
> - **BLE HID 完全不存在**（無 `ble_hid.*` 檔案，僅 README/config 宣稱）。
> - `ui_inventory_upload()` 內是 `// TODO: implement upload logic` 空殼 → 盤點上傳未實作。
> - 狀態列 `[W]`/`[M]` 圖示與 `ui_update_status()` 已畫，但無人呼叫。
> 真正可運行：顯示、相機、QR 掃描、SD、NVS、低功耗、OTA、拍照 HTTP 上傳。
> 「掃描查詢(MQTT)/BLE 條碼槍/盤點上傳」三大 README 賣點**尚未接通**。下一步：接 WiFi+MQTT 到 main。

**Phase 10 新增檔案：**
- `src/ota_update.cpp/.h` — OTA 韌體更新（ESP32 ArduinoOTA）
- `src/storage/nvs_settings.cpp/.h` — NVS 設定持久化（Preferences）
- `src/power.cpp/.h` — 低功耗模式（idle 熄螢幕/降亮度）

## 待驗證

1. BOOT 按鈕按住時序列埠輸出 `[DBG] GPIO0=0`
2. `tft.fillRect()` 在硬體上穩定運作
3. 0x7E I2C 裝置識別（GT911 可能需重置序列）
4. TouchLib GT911 驅動整合
5. ~~完整條碼掃描 UI~~ ✅ 已完成（見 Session 2026-09-22）
6. ~~OV5640 相機整合~~ ✅ 已完成（RGB565，見 Session 2026-09-22 根因 #3）


---

# Session 2026-09-22 — 完整條碼掃描 UI 打通（預覽 → 解碼 → 顯示）

實機驗證成功：對準二維碼解出 `[DECODE] QR: TEST123`。以下依序記錄本次解決的 8 個根因，
每項皆以實機 serial 數據定位並驗證。

## ⚠️ 修正先前錯誤結論：PSRAM 其實存在

先前 memory.md「Board Status」寫 **PSRAM: NOT present (hardware absent)** 是**錯的**。
真相：PSRAM 是 8MB Octal (OPI)，只是 `platformio.ini` 從未啟用，故 `heap_caps_malloc(SPIRAM)`
回 NULL，被誤判為「硬體不存在」。啟用後 PSRAM 位址 `0x3d80xxxx` 正常配置。
> 板子實為 **N16R8**（16MB QIO Flash + 8MB OPI PSRAM），對齊官方 demo `CONFIG_SPIRAM_MODE_OCT=y`。

## 根因與修正（依發現順序）

### 1. PSRAM 未啟用（根因中的根因）
- 症狀：`[LVGL] PSRAM alloc failed`、`Failed to resize quirc buffer`、`FAILED to allocate shared framebuffer`
  ——大塊記憶體全擠爆 ~320KB DRAM。
- 修正（`platformio.ini`）：
  - `board_build.arduino.memory_type = qio_opi`
  - `build_flags` 加 `-D BOARD_HAS_PSRAM`
- 驗證：`[LVGL] buf1=0x3d80a01c`（PSRAM 段）、`Quirc QR decoder initialized`、`[PREVIEW] started`。

### 2. capture task stack 過小（兩階段）
- `preview_cap` task 加了 `barcode_decode()` 後爆 stack。
- quirc `identify`（`quirc_end`）需 ~16KB；`quirc_decode`（Reed-Solomon + ~8KB `quirc_data`）需更多。
- 症狀演進：4KB→掃描即 overflow；16KB→**掃到 QR 進入 decode 才** overflow（`Stack canary ... preview_cap`）。
- 修正：`xTaskCreatePinnedToCore(capture_task, "preview_cap", 32768, ...)`（`scan_preview.cpp`）。

### 3. OV5640 grayscale 不可用
- esp32-camera 對 OV5640 `PIXFORMAT_GRAYSCALE` 支援不穩 → 畫面認不出。
- 官方 Arduino demo（`reference/.../09_lvgl_camera`）用 `PIXFORMAT_RGB565`。相機 GPIO 與本專案完全一致。
- 修正：`camera.cpp` 改 `PIXFORMAT_RGB565`、`fb_location=PSRAM`、`fb_count=2`；
  `scan_preview.cpp` 預覽降採樣改處理 RGB565（2 byte/px）。

### 4. SPI 跨 task race（crash）
- 症狀：`assert failed: xQueueGenericSend ... spiEndTransaction`，backtrace 在 LVGL `flush_cb` 與
  Core 0 quirc 之間。
- 根因：`render_task` 的 overlay（角標/掃描線）繪圖在 `s_spi_mutex` 釋放**之後**，與 LVGL flush 的
  SPI transaction 交錯。
- 修正：VF 影像 + 所有 overlay 全包進**單一 `s_spi_mutex` + 單一 `startWrite/endWrite`**。
- 註：`display.cpp` 的 `lvgl_flush_cb` 已取同一 `s_spi_mutex`（`lv_conf.h` `LV_COLOR_16_SWAP=1`）。

### 5. RGB565 byte order（顏色錯）
- 來回試錯後用**診斷法定案**：在取景框頂端畫三條已知純色（fillRect 保證邏輯色正確），
  對照相機影像。
- 實機結果：參考條「紅/綠/藍」正確（面板 RGB 非 BGR）；相機影像用 **no-swap**（`writePixels` 不帶 swap，
  原生 uint16 讀取）時顏色正確。
- 結論：相機 RGB565 byte 排列與面板一致，**不需 swap**。手動組 big-endian、`swap=true` 都是錯的。

### 6. 解碼未接 RGB565
- `barcode_decode()` 原假設 grayscale（`memcpy`）。加 RGB565 → luma（Rec.601：`(r*77+g*150+b*29)>>8`），
  byte order 與顯示端一致（原生讀取）。
- 解碼 gate 改為接受 `PIXFORMAT_RGB565`。

### 7. ECC failure（最難，quirc size=21 穩定但資料糾錯失敗）
- 診斷輸出 `count=1 size=21 err=ECC failure`：quirc 幾何/格式全對（穩定辨識 v1 QR），
  純卡在**模組黑白取樣**。
- 誤區：先加 `sharpness=2`「增強對比」→ **反而惡化**（邊界振鈴/過衝破壞取樣）。改回 `sharpness=0`+`contrast=1`
  後偶爾成功但**不穩**。
- **根本解法：Otsu 自適應二值化**（`barcode_decoder.cpp`）——luma 直方圖算最佳閾值，硬壓純黑/白(0/255)
  再餵 quirc。實機：對準幾乎立即解出，穩定。
- 成本：偵測到 QR 時多三趟全幀處理，fps 7.5 → ~2.5（僅解碼期間）。

### 8. 掃描頁省電變暗干擾
- `main.cpp` loop：`if (ui_get_current_page()==UI_PAGE_SCAN) power_update_idle_time();`
  掃描頁保持背光全亮。

## 掃描 UI 互動（已實作）
- 單鍵（BOOT）：SCAN 頁短按=切模式（QUERY/INPUT/INVENTORY）、長按=確認（盤點跳清單頁）。
- 取景框 200×150 置中，四角括號 + 綠掃描線；1.6× 固定點降採樣。
- 解碼在 Core 0 capture task 連續嘗試，`SCAN_COOLDOWN_MS` 去重；結果經 pending 緩衝 marshaled 到
  Core 1 LVGL（`ui_process_pending_scan`）。

## 修改檔案（本次）
- `platformio.ini` — PSRAM 啟用（qio_opi + BOARD_HAS_PSRAM）
- `src/camera/camera.cpp` — RGB565、PSRAM fb、QR sensor tuning（sharpness=0/contrast=1）
- `src/scan_preview.cpp` — RGB565 降採樣、單一 SPI mutex、32KB stack、解碼鉤子、取景框放大
- `src/decoder/barcode_decoder.cpp` — RGB565→luma + Otsu 二值化 + 動態解碼
- `src/ui/ui_main.cpp` / `.h` — 掃描結果 pending 緩衝、模式互動、per-mode 回饋
- `src/main.cpp` — LVGL task 呼叫 `ui_process_pending_scan`、掃描頁保持背光
- `docs/TODO.md` — 決策記錄 + 1(b) SVGA 方案

**最終 build**：RAM 41.6% (136372 / 327680)、Flash 23.9% (752301 / 3145728)。

## 已知特性（非缺陷）
- 解碼連續嘗試，需對準 1-3 秒；定焦 + 手持正常行為。
- 偵測到 QR 時 fps 降至 ~2.5（Otsu 三趟全幀）；無 QR 時 7.5。
- 僅支援 QR Code（quirc）；1D 條碼仍為 TODO。

## 下一步（已定案待實作，詳見 TODO.md）
- **方案 1(b) 提升成功率**：QVGA 輕量偵測 → 偵測到切 **SVGA 800×600** 抓單張 → Otsu+解碼 → 切回 QVGA。
  quirc 需動態 resize（已確認可重複呼叫；SVGA buffer ~470KB 注意記憶體）。
- 其他：MQTT 查詢分派、BLE HID 輸出、1D 條碼解碼。


---

# Session 2026-09-26 — 掃描成功率提升（QR 多閾值/ROI + SVGA 精解 + 1D 條碼 + OV5640 AF）

對應 Redmine issue #61（project `xq_xscriqf`）。目標：提升「產品包裝小標籤」掃描成功率，
支援 QR + 1D 條碼（EAN-13/UPC-A/Code128）。分 7 個 task 實作，全部通過 host 測試與 `pio run` build。

## 新增 1D 條碼解碼器（`lib/barcode1d/`）

純 C、無 heap、line-scan。選型理由：ZXing/ZBar 對 ESP32 太重，`esp_code_scanner`（ESP-IDF
component）與 Arduino framework 整合有風險。故自製，與 vendored quirc 架構一致。

- **EAN-13 / UPC-A**：用 **module-grid 取樣**（非逐 digit run 切割）。關鍵根因：G-code digit
  首模組是 bar，會與相鄰元素 run 合併，逐 digit 切割必然失敗。grid 取樣定位 start/center/end
  guard → 推導模組節距 → 對 95 模組中心取樣，對 run 合併免疫（商用掃描器標準做法）。
  L-code 首模組=space、G-code 首模組=bar；checksum 偶 index 權重 1、奇 index 權重 3；
  leading digit 由 6 個左 digit 的 L/G parity 反查 PARITY 表。UPC-A = EAN-13 lead=0 去開頭 0。
- **Code128**：run-length 寬度模式比對（元素本身 bar/space 交替，無合併問題）。107-entry
  pattern 表，Code A/B/C，checksum = (start + Σ pos×val) mod 103。Code C 成對數字，Code B ASCII v+32。
- **`bc1d_decode_image()`**：多掃描線 wrapper，取 N 條等距水平線（偏中央），任一成功即回傳。
- **測試**：`test/test_barcode1d.c` host 端（gcc）合成理想掃描線，**24/24 通過**，`-Wall` 無警告。
  編譯：`gcc -Ilib/barcode1d test/test_barcode1d.c lib/barcode1d/barcode1d.c -o /tmp/bc1dtest`

## 解碼管線重構（`barcode_decoder.cpp`）

- luma 單次產生到 `s_luma`（PSRAM），QR 與 1D 共用，消除先前重複的 RGB565→luma 轉換。
- **ROI Otsu**（`otsu_threshold_roi`）：閾值只從中央 70% 區域算，排除背景（桌面/手指/反光）污染直方圖。
- **QR 多閾值重試**：base、±20、±40 共 5 組閾值，解決 v1 QR 邊界模組翻轉的 ECC failure。
  找不到 QR 的幀第一次 identify count=0 即快速跳過，只有真有 QR 但邊界模糊才多跑。
- 抽出 `decode_core(luma,w,h)`，`barcode_decode()`（QVGA）與 `barcode_decode_luma()`
  （任意尺寸，quirc_resize 後還原 QVGA）共用。

## SVGA 高解析度單張精解（`scan_preview.cpp`）

QVGA 常駐偵測，連續 40 幀未解出且過 4s cooldown → 自動切 SVGA 800×600 抓單張精解 → 切回。
自動觸發（非按鍵，因 SCAN 頁短按=切模式、長按=確認已佔用）。`s_hires_luma` PSRAM buffer(480KB)。
記憶體：SVGA fb + luma + quirc resize ≈ 3.8MB < 8MB PSRAM。切換期間 render_task sem timeout 續跑不崩。

## OV5640 自動對焦（`camera.cpp` + vendored `ov5640_af_firmware.h`）

- 根因：esp32-camera 的 `ov5640_af.c` 被 `CONFIG_CAMERA_AF_SUPPORT`（IDF menuconfig）gate 掉，
  Arduino build 不啟用，且函式在 private_include。故改用**公開 `sensor_t` set_reg/get_reg 自行實作**，
  vendor AF firmware blob（~4KB）到 `src/camera/ov5640_af_firmware.h`。
- `camera_af_probe()`：reset MCU(0x3000=0x20) → 寫 blob 到 0x8000 → start(0x3000=0x00) →
  等 FW_STATUS(0x3029)==0x70 IDLE → 送 0x3022=0x03 單次對焦 → 輪詢直到 0x10 FOCUSED 或 timeout。
  開機在 `camera_init()` 尾端呼叫，serial 印 `[AF] RESULT`。
- AF 觸發**資料驅動**：`camera_af_is_available()` 回報探測是否觀察到 FOCUSED；hi-res 路徑據此
  自動啟用/跳過 AF，無需硬編。

## ⚠️ 待實機驗證（無法在開發環境確認）

1. **鏡頭是否 AF 版**：開機看 serial `[AF] RESULT: lens FOCUSED (0x10)` = AF 版；
   `no FOCUSED state` = 定焦。決定 hi-res 是否觸發對焦。
2. **1D 條碼實機解碼**：拿實際產品 EAN-13/Code128 標籤測試（host 測試僅證明演算法正確）。
3. **QR ROI+多閾值 成功率提升**：對比對準所需時間是否縮短。
4. **SVGA 精解**：小標籤成功率提升、切換延遲、PSRAM 用量（監控 heap_caps free）。

## 最終 build

RAM 42.2% (138444 / 327680)、Flash 24.3% (764389 / 3145728)。


---

# Session 2026-09-27 — 實機除錯：掃描成功率瓶頸 = 定焦鏡頭

Redmine #61 的實機除錯，依序解掉多個問題後，定位到**根本瓶頸是硬體對焦**。

## 實機除錯歷程（每步以 serial 數據推進）

1. **task_wdt abort**（Core 0 preview_cap，quirc_end jiggle_perspective）
   → QR 多閾值太貪心。修：時間預算 350ms + 無候選早退 + 每閾值喂狗。
2. **fps 7.5→3**：QVGA decode 恢復 70ms 但 fps 仍低
   → 自動 hi-res 每 4s 觸發、每次 ~1s 拖累。修：改使用者短按觸發。
3. **hi-res: no code**：`fb 800x600 但 len=153600`
   → esp32-camera framebuffer 依 init frame_size 固定配置，runtime set_framesize
   無法擴大（issue #433/#635）。修：**常駐 SVGA 初始化**（config.h
   CAM_FRAME_SIZE=FRAMESIZE_SVGA），預覽 4x 降採樣，短按解當前 SVGA 幀。
4. **SVGA 仍解不出**：診斷 `luma spread=255`（高對比非模糊/雜訊）但
   `capstone candidates=0~1`（quirc 找不到 3 個 finder pattern）。
   → QR 降採樣改 box averaging（抑制摩爾紋）幫助有限。
5. **確認根因**：能偶爾解出 TEST123（管線正確），但成功率低且不穩。

## 根本原因：定焦鏡頭

- 相機 = **WS-OV5640CSP 定焦版**（AF probe 回 `no FOCUSED — fixed-focus`）。
- Waveshare OV5640 定焦（Board A/CSP）：焦距 2.8mm、F2.8，對焦在遠處，
  **近距離掃碼嚴重失焦**。手持晃動使各幀銳利度不一 → 多數幀 candidates=0，
  偶爾一幀夠銳利才解出。
- **軟體已正確且盡力**（多幀嘗試 + box average + 多閾值），但無法根治模糊影像。
- 測試媒介是**手機螢幕**（有摩爾紋+反光），非實際的紙本標籤 —— 待補紙本 A/B 測試。

## 硬體解法：換 AF 版鏡頭

| 項目 | 資訊 |
|------|------|
| 產品 | **OV5640 Camera Board (C)** — Auto Focusing, Onboard Flash |
| SKU | **13802** |
| 價格 | US $26.99 |
| 鏡頭 | 焦距 3.37mm(可調)、F2.8、對角 67.4°、板載 flash LED |
| sensor | OV5640 5MP（與現定焦版**同一顆**）|
| 尺寸 | 35.7×23.9mm，DVP 8-bit |
| 產品頁 | waveshare.com/ov5640-camera-board-c.htm |

**韌體已就緒**：AF 觸發資料驅動（`camera_af_is_available()`）。換上 AF 版後，
開機探測偵測到 `[AF] RESULT: FOCUSED`，`hi-res AF disabled` → `enabled`，
SVGA 擷取前自動觸發對焦，**不需改程式碼**。

**⚠️ 採購前確認機構相容**：本板為微雪 ESP32-S3-Touch-LCD-2 一體板，相機經 FPC
排線連接。需確認 (1) 板上相機是可插拔 FPC 模組（非焊死）、(2) Board (C) 排線
規格（pin 數/間距/方向）與板子相機座相容。建議直接詢問微雪客服。

## 目前互動設計（SCAN 頁）

- 常駐 SVGA 800×600，預覽 4x 降採樣到 200×150 取景框（~4.9 fps）
- 短按 = 多幀嘗試掃描（最多 8 幀 / 2 秒，任一成功即停）；QR 降採樣到 QVGA 解、
  1D 全 SVGA 解
- 長按 = 切模式（QUERY/INPUT/INVENTORY）
- 長按住 >2s = 回首頁
- 盤點清單頁入口待重新加入（原長按進入已改為切模式）


---

# Session 2026-10-03 — 換 AF 鏡頭後仍掃不出 → 真正根因是 luma byte order

對應 Redmine #61 延續。換上 AF 鏡頭模組 **FD5640 500W-v11**（OV5640 + VCM），但掃描仍
`window expired, no code`。歷經數輪實機除錯，**最終根因與 AF 完全無關**，是 RGB565→luma
轉換的 byte order 錯誤。以下按實機數據推進的順序記錄。

## 除錯歷程（每步以 serial 數據定位）

1. **AF 從沒被觸發**：掃描 attempt 直接進解碼，從未呼叫 `camera_af_trigger_oneshot()`。
   補上。memory 先前寫「換 AF 版免改碼」是錯的——該路徑從沒接上。
2. **AF 完成判據錯**：原等 `fw_status==0x10 FOCUSED`，但 single-shot (0x03) 完成是靠
   `CMD_ACK (0x3023)` 清零，不是 fw_status。改用 ACK 判據。
3. **AF 握手不完整**：對照官方 `esp32-camera/sensors/ov5640_af.c` 的 `ov5640_af_start`，
   single focus 前需先 `MAIN=0x01; MAIN=0x08; 等 ack-clear` 前置握手。補上後 preamble ack
   成功，但 `MAIN=0x03` single focus 仍卡 `fw_status=0x00` 不收斂。
4. **改 continuous AF (0x04)**：也卡 0x00 不到 0x10。
5. **暫存器級診斷**：firmware readback MATCH、MCU 到 IDLE(0x70)、preamble ack-clear=1，
   但任何對焦指令一下就卡 S_FOCUSING(0x00)。→ 軟體層全正常，疑 VCM 未驅動。
6. **設 VCM control `0x3600=0x08`/`0x3601=0x33`**（OmniVision init 值，esp32-camera 預設
   不設）：回讀正確，但對焦仍卡 0x00。
7. **手動 VCM 掃描測試**（繞過演算法，直接寫 `0x3022` manual mode + `0x3023/0x3024` 位置）：
   **實機肉眼確認焦距有變化** → VCM 馬達是好的，純粹 AF 演算法不收斂（此模組特性）。
8. **結論：放棄演算法 AF，改手動固定近焦**。但換成固定對焦後**仍掃不出**。
9. **QR 解碼診斷**：印 luma 統計 + quirc identify count。`luma min=0 max=255`（對比完美）
   但 `count=0`（finder pattern 偵測不到）。先試「中央 ROI 裁切再降採樣」（怕小 QR 降採樣後
   模組 <1px）——仍 count=0。
10. **GitHub 研究定案**：對照官方 `espressif/qrcode-demo` 的 `rgb565_to_grayscale`，發現它
    從 camera buf 讀 RGB565 時 **先 `__builtin_bswap16`** 才拆 R/G/B。我們的 luma 轉換是
    **原生讀取不 swap**（顯示路徑 no-swap 正確，但 luma 沿用是錯的）。byte order 錯 → 綠色
    分量（跨兩 byte、占 luma 權重 59%）錯位 → QR 黑白關係破壞 → count=0。另有 esp32.com
    中文帖回報同症狀（畫面正常、count 恆 0）。

## 根本原因

**`rgb565_to_luma` 系列函式原生讀取 RGB565，未 byte-swap。** 顯示路徑原生讀取是對的（顏色
正確，見 Session 2026-09-22 根因 #5），但 luma 轉換必須 byte-swap 才能正確還原灰階。先前
QR 偶爾能解出 TEST123（Session 2026-09-22/27），是因為當時用的影像夠大/夠正、錯誤的 luma
仍勉強可辨；小 QR + AF 鏡頭下錯誤被放大，徹底解不出。

## 修正（已實機驗證解出 `TEST123`，多次穩定）

- `scan_preview.cpp` `rgb565_to_luma_local`：加 `__builtin_bswap16`（對齊官方 demo）。**關鍵修正**。
- `barcode_decoder.cpp` `qr_try_threshold`：加 `quirc_flip` 重試（相機有 hmirror/vflip，QR 可能鏡像）。
- `barcode_decode_luma` QR 路徑：改中央 62.5% ROI 裁切再降採樣（小 QR 保留模組解析度）。
- **AF：此 FD5640 模組演算法 AF 不收斂，改用手動固定近焦**。`camera_af_set_manual()` 直接寫
  VCM 位置（`0x3022` manual + `0x3023/0x3024`），開機套用 `VCM=240`。probe 僅載入 firmware
  （保留以供手動 VCM 控制），不再跑注定失敗的對焦握手。`camera_af_trigger_oneshot()` 改為套用
  固定近焦（瞬間，`AF locked (4ms)`）。

## 待辦 / 可調

1. **VCM=240 是初始值,未精調**。可用實際標籤距離微調最清楚的 VCM 值（曾加 serial +/- 微調,
   驗證後已移除;需要再調可還原）。
2. 實機實測 `TEST123` 已穩定解出;1D 條碼（EAN-13/Code128）尚未在此 byte-order 修正後重測。
3. 診斷 log 已清理;保留 `[AF] firmware loaded; using manual fixed-focus` 一行說明。

## 最終 build

RAM 42.2% (138316 / 327680)、Flash 24.3% (763681 / 3145728)。


---

# Session 2026-10-03 (續) — 1D 條碼：ZXing 比對 + 自動對焦 + UXGA，最終判定為鏡頭光學極限

接續上一段(QR byte-order 修好後)。目標:讓 1D 條碼(Code128)在實機解出。結論:**1D 在此
FD5640 500W 模組(鏡頭光學 + 定焦)下無法用純軟體解碼,受鏡頭 MTF 限制,非軟體問題。** QR 可用,
1D 演算法就緒但受硬體限。

## 除錯歷程(每步以實機數據推進)

1. **1D 無任何 [DECODE] 輸出**:加診斷。發現對比完美(min=0 max=255)、25 條掃描線全過對比門檻,
   但解碼全失敗。
2. **二值化**:全域 min/max 中點 → 光照不均把 narrow bar 判成白。改**局部自適應(moving-average)**。
   dark 比例從 ~30% 升到 ~40%,但仍解不出。
3. **span 污染**:解碼器用「第一個/最後一個暗像素」當條碼範圍,被背景雜訊撐大。
4. **Code128 多起點**:原從第一個 bar 硬解,前導雜訊即錯位。改成嘗試多個 run 起點。仍失敗。
5. **GitHub 研究(ZXing Code128Reader)**:根因是我們**整數量化**(run/module 四捨五入)太脆弱。
   改用 ZXing 的**原始 counter 歸一化 variance 比對**(MAX_AVG_VARIANCE=0.25)。host 24/24,
   但實機仍失敗。
6. **run-length dump**:直接印中央掃描線 run 序列。發現最佳對焦下 `nruns` 只有 ~19(應 ~50)、
   出現 `333` 超大 run → narrow bar 糊成大塊,run 結構塌縮。
7. **自動對焦掃描**(sharpness = 中央 ROI 水平梯度能量):掃 VCM 80..600 找最銳利點。
   - 對準時 sharpness 峰值 ~85000-92000,峰值在近焦 **VCM≈120**。
   - 但**曲線平坦無尖峰**,且最佳焦下 run 仍塌縮 → 鏡頭 MTF 在 narrow bar 空間頻率已衰減。
8. **UXGA 1600×1200 嘗試**:OV5640 是 5MP,之前只用 SVGA。想用更高解析度救 narrow bar。
   - **UXGA RGB565 常駐 → 卡死**(每幀 3.84MB,DVP/DMA 頻寬爆,esp_camera_fb_get hang)。已 revert。
   - 正確路線應為 UXGA **JPEG**(壓縮後 ~100-300KB,頻寬可行),但工程量大且仍受鏡頭光學限,未做。

## 根本原因(完整證據鏈)

| 環節 | 狀態 |
|------|------|
| 二值化 | 健康(自適應,dark 比例合理) |
| 解碼演算法 | ZXing 業界標準 variance 比對,host 24/24 |
| 對焦 | 客觀 sharpness 掃描,峰值 VCM≈120,但**曲線平坦無尖峰** |
| 條碼尺寸 | 大條碼已測 |
| 解析度 | SVGA 已是 RGB565 可用上限(UXGA RGB565 頻寬卡死) |
| **結論** | **鏡頭 MTF 不足以解析 Code128 narrow bar = 光學物理極限** |

**GitHub 業界佐證**:ESP32 純軟體(quirc)專案清一色**只做 QR**(espressif/qrcode-demo、
ESP32QRCodeReader 等)。要做 1D 的都用**專用解碼晶片**(如 M5Stack Unit QRCode STM32F030,
otitbridge/m5stick-s3-qr-phomemo),ESP32 只讀結果。→ ESP32+相機純軟體 1D 是公認困難,我們的
演算法已達業界水準,瓶頸在光學。

**QR vs 1D 差異**:QR 只需偵測 3 個粗大 finder pattern(容錯高);1D 需精確還原每根 narrow bar
(要求高一個量級)。故同樣模糊影像 QR 能解、1D 不能。

## 最終狀態(本段收尾)

- **QR**:穩定可用(byte-order 修正)。純軟體 quirc,與業界成熟做法同級。
- **1D**:演算法就緒(ZXing variance,host 24/24),保留於碼內;受此鏡頭光學限,實機無法解。
  未來若需 1D,建議:(a) 換更好光學的 AF 鏡頭模組,或 (b) 加專用解碼晶片(業界做法),
  或 (c) 做 UXGA JPEG 單張精解(未驗證,仍可能受光學限)。
- **對焦**:AF 演算法不收斂,改固定手動近焦 **VCM=120**(sharpness 掃描近焦峰值)。
  保留 serial `+`/`-`/數字/`p` 校正指令(`send_on_enter` filter 下可用)。
- **已移除**:autofocus_sweep(11s、拖垮 fps、對 1D 無效、QR 不需)、所有 QR/1D diag log、
  UXGA 實驗(已 revert)。

## 最終 build

RAM 43.5% (142412 / 327680)、Flash 24.3% (763941 / 3145728)。host 1D 測試 24/24。


## 1D 補救方案 — 專用解碼模組(建議,業界做法)

純軟體 1D 受鏡頭光學限,業界標準是加「掃描頭+解碼晶片」模組,ESP32 只讀結果字串。候選:

| 模組 | 晶片 | 介面 | 碼別 | 備註 |
|------|------|------|------|------|
| **M5Stack Unit QRCode**(U173) | STM32F030 + 640×480 CMOS | I2C / UART(側邊開關切) | 1D(EAN/UPC/Code128/Code39)+2D(QR/DM) | 官方 lib `M5Unit-QRCode`;部分版本僅 UART |
| M5Stack Atomic QRCode2 Base | 同系 | 僅 UART | 同上 | 新一代 |
| GM65 / GM805(淘寶通用) | — | UART | 1D/2D | 便宜,協定簡單,無品牌 lib |

- 文件:docs.m5stack.com/en/unit/Unit-QRCode;datasheet mouser.com/datasheet/2/1117/Unit_QRCode-3693695.pdf
- 參考實作:GitHub `otitbridge/m5stick-s3-qr-phomemo`(ESP32-S3 + Unit QRCode,TRIG 觸發讀結果)
- **整合建議**:用 **UART**(僅佔 2 支 GPIO,避開已被 OV5640 相機/SD/觸控佔用的 I2C bus:
  0x6B QMI8658、GT911 等)。讀回字串直接餵現有 `scan_preview_set_decode_cb`。
- 架構:主 OV5640 維持 QR + 拍照 AI;Unit QRCode 專職 1D(及高可靠度 2D)。

---

# Session 2026-10-04 — 觸控除錯：判定 CST816 硬體不通（+ 修正 I2C/IMU bug）

目標：啟用電容觸控（開機一直 `[TOUCH] CST816 I2C read failed`）。結論：**CST816 觸控晶片在
此板的共用 I2C bus 上完全無回應，判定硬體（晶片/FPC）故障**。過程中順帶修了兩個真實 bug。

## 過程（每步實機數據）

1. 觸控晶片確認：官方 wiki + reference `bsp_cst816` 證實板子用 **CST816D @ 0x15**，I2C bus
   SDA=48/SCL=47，驅動 register map/讀法與我們**逐字相同**。
2. `touch_init` 原本讀 ID 失敗就 return false → LVGL indev 從沒註冊。改為容錯仍註冊。
3. I2C 降速 400k→100k：無改善。
4. **真 bug #1**：`setup()` 開頭「CPU alive」把 **GPIO48 設 OUTPUT 拉 HIGH**，但 GPIO48 =
   I2C SDA！移除（雖非觸控根因，但確實破壞 bus 的隱患）。
5. **真 bug #2（診斷錯誤）**：我讀 QMI8658 WHO_AM_I 用 reg **0x0F**（錯），正確是 **0x00**。
   修正後 IMU ID=0x05 正常 → **證明 I2C bus 完全正常**。
6. 詳細診斷：IMU @0x6B register read OK（endTx=0, got=1, val=0x05）；但觸控 0x15/0x7E
   register read 全 FAIL。`0x7E` 掃描 ACK 但 register read 失敗 = 非真實暫存器裝置/幻影 ACK。
7. loop 心跳確認 loop() 有跑；touch_read 直接 poll——probe 仍全 FAIL。
8. 仿官方 `while` 無限重試邏輯做**有上限 2 秒 ID 握手重試**（跨 0x15/0x7E）：數十次全 FAIL id=0x00。
   之後 40 秒持續 poll 仍全 FAIL。

## 根本原因（完整證據鏈）

| 驗證 | 結果 |
|------|------|
| loop 執行 | ✅ |
| I2C bus/pull-up/速度 | ✅（QMI8658 @0x6B 讀到 0x05）|
| 讀取方法 | ✅ 與官方 bsp_cst816 逐字相同 |
| ID 握手 2s 數十次重試 | ❌ 0x15/0x7E 全 FAIL |
| 官方初始化漏步驟 | ❌ 無 |
| **結論** | **CST816 硬體不通（晶片/FPC 故障）** |

## 已修 / 已清理

- 修正：QMI8658 WHO_AM_I reg 0x0F→0x00（IMU 現在讀到 0x05）。
- 修正：移除 setup 對 GPIO48(SDA) 的 OUTPUT 驅動。
- I2C 降 100kHz + 內部 pull-up（無害，保留）。
- touch_init 改容錯 + 2s ID 握手重試（不洗版，印最終結果）；驅動碼完整，硬體修好即可用。
- 清理所有觸控/loop/I2C 診斷洗版 log。

## 待辦

- **LVGL indev 路徑待整理**：main.cpp 有雙 disp 註冊（display_init 一個 + main 自己一個），
  touch indev 段被註解（舊 LoadProhibited crash）。即使觸控硬體修好，indev 這條路也要先理清
  才能讓 LVGL 收到觸控事件。目前 `touch_read` 其實沒被 LVGL 呼叫。
- 觸控硬體：可燒官方 factory.bin 做最終硬體確認（offset 0x10000）。
- 暫以 BOOT 單鍵操作。

## build

RAM 43.5% (142412)、Flash 24.3% (764065)。

---

# Session 2026-10-04 (網路) — 接通 WiFi/MQTT + 移植 captive-portal 配網

延續「文件更正→接通網路」計畫。盤點發現 Phase 9「✅」不實：wifi/mqtt 模組有實作但 main 從未
init、BLE HID 檔案不存在、ui_inventory_upload 是 TODO 空殼。本 session 把網路接通並升級配網。

## 完成項（均 pio build 通過；實機連線待驗證）

1. **WiFi/MQTT 接進 main**：
   - setup 原本漏接 → 加 wifi init + `mqtt_client_init()` + response callback。
   - loop 加 `mqtt_client_loop()` keep-alive + 狀態列 [W]/[M] 更新。
   - QUERY 掃描接 `mqtt_query_barcode()`（原 TODO 空殼）；回應 `mqtt_on_response()` 格式化
     品項（name/spec/qty/location）→ `ui_show_scan_result()`。
   - `ui_inventory_upload()` 接 `mqtt_publish_inventory_batch()`（原空殼）。

2. **captive-portal 配網（移植自 WT02 專案）**：`src/network/wifi_portal/`
   - WiFiManager class：WebServer+DNSServer captive portal、EEPROM 持久化、掃描/選取 WiFi 網頁、
     狀態機、onConnected callback。依賴全為 ESP32 Arduino 內建 + ArduinoJson。
   - 開機從 EEPROM 自動連；無設定/失敗 → 開 AP「SoftSnail-Scanner-Setup / 12345678」網頁配網。
   - include guard 從 WIFI_MANAGER_H → WIFI_PORTAL_H（與舊 wifi_manager.h 撞名修正）。
   - 舊 `wifi_manager.cpp` 保留（其 wifi_is_connected() 等 C 查詢函式仍被 mqtt_client 用）。

3. **MQTT broker 納入 portal 設定頁**：
   - WiFiConfig 加 mqtt_broker[64]/mqtt_port，EEPROM broker@130/port@194 持久化。
   - 設定頁加兩個選填輸入框；`mqtt_set_broker()` runtime 覆蓋 config.h 預設。
   - setup 順序：portal begin → 讀 broker → mqtt_set_broker → mqtt_client_init（portal 設定優先）。

## 參考

- WiFi 連線設計參考 WT02 專案 `ProPrj_PLT251129004125-esp32_WT02`
  （`src/App/Utils/WiFiManager/`）——事件驅動 + captive portal + EEPROM，比寫死 SSID 高明。

## 相關 commit

- `ae753f4` 接 WiFi+MQTT 到 main
- `ae7d149` 移植 captive-portal WiFiManager
- `d6c2a78` MQTT broker 納入 portal

## 待辦（下次繼續）

1. **實機驗證**：portal 配網 → WiFi 連線 → MQTT 查詢/盤點整條鏈（需真實 AP + barcode-warehouse-server broker）。
2. config.h 的 MQTT_BROKER 預設仍是 192.168.1.100；WIFI_SSID 佔位值（現可由 portal 覆蓋，但預設值可更新）。
3. 查詢結果頁完整欄位 UI（目前塞一行 label）。
4. BLE HID（README 賣點，完全未實作）→ INPUT 模式無線條碼槍。
5. 修 README 硬體表（過時 OV2640/ILI9341/搖桿）。
6. 觸控硬體不通（上一 session 結論）；LVGL 雙 disp/indev 待整理。

## build

RAM 50.5% (165416)、Flash 37.9% (1193145)。

---

# Session 2026-10-05 — MQTT broker 修復 + 端到端查詢打通 + 契約落差盤點

對應 Redmine issue #79（project `xq_xscriqf`，http://blog.softsnail.com:2024/issues/79）。
目標：驗證 ESP32↔server 的 MQTT 鏈路。結論：**「查詢」整條鏈實測打通**；但盤點/入出庫/
scan-input 的 topic/payload 契約與 server 不一致，接 ESP32 前需處理。本次在 server 與 broker
端用 `mosquitto_pub/sub` 模擬 ESP32 驗證，未動硬體。

## 部署拓撲（本次釐清）

- **B400A** 一台同時跑 **mosquitto broker** + **barcode-warehouse-server**（FastAPI uvicorn :8040）。
  對外網域 `blog.softsnail.com`（公網 1.34.59.237，NAT 進內網 192.168.2.223）。
- 另一台 PC 當測試客戶端（mosquitto_pub/sub）。

## Broker 修復（根因：listener 綁死內網 IP）

1. mosquitto.service 開機後 `failed` → log `Opening ipv4 listen socket on port 1883. Error:
   Cannot assign requested address`。根因：`/etc/mosquitto/mosquitto.conf` `listener 1883
   192.168.2.223`，開機時 USB 網卡(DHCP)還沒配上該 IP → 綁定失敗。
2. 同一個綁死 IP 還導致 **server 用 `localhost` 連 broker 時 `[Errno 111] Connection refused`**
   （broker 只聽 192.168.2.223，不聽 127.0.0.1）。
3. **修正**：`listener 1883 0.0.0.0`（同時涵蓋 localhost + 對外 IP），重啟 mosquitto。
   一次解掉「開機時序綁定失敗」與「server localhost 連不上」兩個問題。
4. 之後 server log 出現 `MQTT connected to 192.168.2.223:1883` +
   `Subscribed to: warehouse/query, warehouse/create, warehouse/stock/in, warehouse/stock/out,
   warehouse/stocktake/*`。

## 端到端查詢驗證（實測通過 ✅）

測試客戶端（先 sub 後 pub）：
```
mosquitto_sub -h blog.softsnail.com -t 'warehouse/response/#' -v &
mosquitto_pub  -h blog.softsnail.com -t 'warehouse/query' -m '{"barcode":"TEST123","device_id":"local-test"}'
```
收到：
```
warehouse/response/local-test {"status": "not_found", "barcode": "TEST123", "message": "Item not found in database"}
```
→ 完整往返通：client → broker → server 查 DB → `warehouse/response/{device_id}` → broker → client。
`not_found` 為正確回應（DB 無 TEST123）。證實 server 靠 payload 的 `device_id` 組回應 topic。

## ⚠️ MQTT 契約落差盤點（韌體 vs server，兩端程式碼比對）

| 功能 | 韌體送出 (mqtt_client.cpp / config.h) | server 期望 (mqtt_handler.py) | 一致? |
|------|------|------|------|
| 查詢 query | `warehouse/query` `{barcode, device_id}` | 同左 | ✅ 已實測 |
| 回應 response | 訂閱 `warehouse/response/esp32-001` | 發 `warehouse/response/{device_id}` | ✅（DEVICE_ID=esp32-001）|
| 建立 create | `warehouse/create` `{device_id,barcode,name,spec,quantity,location,supplier}` | 同左 | ✅ |
| 入庫 stock/in | ❌ 韌體**無 publish 函式** | 訂閱並完整實作 `warehouse/stock/in` | ⚠️ server 有、韌體沒送 |
| 出庫 stock/out | ❌ 韌體**無 publish 函式** | 訂閱並完整實作 `warehouse/stock/out` | ⚠️ server 有、韌體沒送 |
| **盤點上傳** | `mqtt_publish_inventory_batch()` 發到 **`warehouse/create`**，payload `{device_id,count,items}`（items 是字串化 JSON）| `_handle_create` 期望單品 `{barcode,name,...}` | ❌ **會回 Invalid payload 錯誤，盤點完全不運作** |
| 盤點 stocktake | ❌ 韌體無 start/scan/end | 訂閱 `warehouse/stocktake/start|scan|end`（有 session 管理 + 差異計算）| ❌ 兩端設計不同 |

## 為何盤點對齊 = 實作功能，非單純改 topic（決策記錄）

採方向 2（改韌體對齊 server 既有 stocktake 設計），但盤點對齊有兩個障礙，決定**本次先擱置**：

1. **語意落差**：server `stocktake/scan` 需 `actual_qty`（實際盤點數量），韌體盤點目前只存
   「掃到哪些 barcode」的 DecodeResult 清單，沒有數量語意。
2. **非同步 session 障礙**：server 盤點是有狀態 session —— 須先 `stocktake/start` **非同步**拿到
   `session_id`，之後每筆 `scan` 都要帶該 id，最後 `end`。韌體現在 `mqtt_publish_inventory_batch()`
   是**同步**批次模型，接不上「發 start→等 callback 回 session_id→逐筆 scan→end」的狀態機。
   正確做要改成狀態機，並補完盤點 UI（memory.md 既有待辦，標為半成品）。

→ 結論：盤點正確對齊屬「實作一個功能」，待查詢鏈實機驗證過、actual_qty 語意定案後再做。

## 下一步（優先序）

1. **先接 ESP32 實機驗證「查詢」鏈**（原待辦第 1 項）：portal 配網 → WiFi → `mqtt_query_barcode()`
   → 收 `warehouse/response/esp32-001` → `mqtt_on_response()` 顯示品項。這條 server 端已實測 OK。
   - 韌體 config.h `MQTT_BROKER` 預設仍 `192.168.1.100`；實測需改成 `blog.softsnail.com` 或由
     portal 設定覆蓋。
2. 盤點：改韌體 stocktake 狀態機 + 盤點 UI 記錄每 barcode 數量（actual_qty）。見 Redmine 新 issue。
3. 入出庫：韌體新增 `warehouse/stock/in`、`warehouse/stock/out` publish 函式。
4. scan-input（INPUT 模式推送 Web UI）：README 宣稱 `warehouse/scan-input/{device_id}`，但
   **server 與韌體兩端皆未實作**。

## ⚠️ 安全債（broker 公網暴露，正式上線前必處理）

- broker 現 `listener 0.0.0.0` + `allow_anonymous true`，且 **1883 對公網 OPEN**
  （`blog.softsnail.com:1883` 外部可達）。mosquitto log 曾有境外連線（165.227.44.79、
  88.210.63.16 等掃描流量）。
- 正式用前：`allow_anonymous false` + `mosquitto_passwd` 建帳密（server `.env` 與韌體 config.h
  `MQTT_USER/MQTT_PASSWORD` 兩端都要填，韌體 `mqtt_reconnect()` 已支援帶帳密）；有條件再加 TLS
  （8883，可用 softsnail.com 域名簽 Let's Encrypt）。
- broker 開機自啟：改 0.0.0.0 後綁定時序問題應緩解，仍建議確認重開機後 mosquitto 自動起來
  （或 systemd override 加 `After/Wants=network-online.target`）。

---

# Memory — MQTT Contract Alignment + WiFi/Portal Session (2026-10-06..07)

Context: Redmine #79 (ESP32↔server MQTT contract). Implemented firmware P1–P3,
fixed a captive-portal crash, then chased a WiFi connectivity failure to an
AP-side WPA3/PMF root cause.

## MQTT contract alignment with barcode-warehouse-server (P1–P3)

### P1 — query chain + broker default
- `config.h`: `MQTT_BROKER` / `DEFAULT_MQTT_BROKER` → `blog.softsnail.com`.
- `mqtt_client.cpp`: `mqtt_reconnect()` log now uses runtime `s_broker/s_port`.
- **Bug fixed in `main.cpp` `mqtt_on_response()`**: it only handled
  `status=="ok"` and read fields from the payload top level. The server
  actually replies `status=="found"` with fields nested under `data`. Query
  results were never displayed before. Rewrote to parse `found`/`not_found` +
  `data`, and added stock/stocktake ack branches.

### P2 — stock in/out
- `config.h`: added `warehouse/stock/in|out` + stocktake topic constants.
- `mqtt_client`: `mqtt_stock_in()/mqtt_stock_out()`, payload
  `{device_id, barcode, quantity}` (server `_handle_stock_in/out`).
- `ui_main`: added `SCAN_MODE_STOCK_IN` / `SCAN_MODE_STOCK_OUT`.

### P3 — stocktake async session state machine
- `mqtt_client`: `mqtt_stocktake_start/scan/end`. Old
  `mqtt_publish_inventory_batch()` (published a `{device_id,count,items}`
  envelope to `warehouse/create`, rejected by server as "Invalid payload") is
  now a deprecated no-op.
- `ui_main`: `g_st_entries[]` keeps per-barcode accumulated `actual_qty`;
  `g_st_session_id/g_st_active/g_st_pending_start` drive the flow: enter
  STOCKTAKE mode → publish start → server ack `stocktake_started` sets
  session_id → each scan bumps actual_qty and publishes scan → INVENTORY-page
  long-press publishes end.

Build: `pio run` SUCCESS, RAM 50.3% / Flash 38.0%.

## Captive-portal WebServer crash (LoadProhibited) — FIXED

- Symptom: after WiFi connect fails, portal save → `LoadProhibited` in
  `WebServer::sendContent` ← `_finalizeResponse` ← `handleClient` ←
  `WiFiManager::loop()`. EXCVADDR=0x1c.
- **Root cause**: `handleWiFiSave()` called `stopConfigPortal()` (`delete
  server`) + reconnect from *inside* the WebServer request handler. When
  `handleClient()` returned to `_finalizeResponse()`, the WebServer object was
  already freed → use-after-free.
- **Fix**: handler only sets `pendingApply=true`; `loop()` performs the
  stop+reconnect after `handleClient()` returns (and pumps the client ~1.5s so
  the success page is delivered first). Files: `wifi_portal.h/.cpp`.

## WiFi connectivity: AP-side WPA3/PMF, NOT a firmware bug

### Evidence
- Target AP `ACC_TPE_WIFI_8F_2G`: SSID+password confirmed correct, iPhone
  connects fine, but ESP32-S3 never associates. `WiFi.status()` diagnostics
  showed random drift across `1 (NO_SSID_AVAIL)` / `4 (CONNECT_FAILED)` /
  `6 (DISCONNECTED)` — the classic signature of WPA3 / PMF-required
  association failure.
- WPA2 hotspot `D500MA`: connected on first try
  (`st=6→0→3`, IP 10.42.0.240) and MQTT reached
  `blog.softsnail.com:1883` → `Connected!`. This confirms the root cause is the
  AP's encryption/PMF policy, plus validates P1's broker default and the portal
  crash fix end-to-end.

### WT02 counter-evidence → more precise conclusion
- `ProPrj_PLT251129004125-esp32_WT02` reportedly connects to the same
  `ACC_TPE_WIFI_8F_2G`. Its WiFi code is essentially identical to ours
  (plain `WiFi.mode(WIFI_STA); WiFi.begin(ssid,pwd)`, no PMF handling).
- The real difference is the **chip**: WT02 is `board=esp32dev` (classic
  ESP32 / WROOM); this project is `board=esp32-s3-devkitc-1` (ESP32-S3).
- Corrected conclusion: it is **not** "ESP32-S3 cannot do WPA3". Both chips
  support WPA3-Personal. The classic ESP32's mature WiFi supplicant negotiates
  this particular WPA2/WPA3-mixed + PMF AP successfully, while the ESP32-S3's
  WiFi driver/blob (arduino-esp32 framework `4.20017.260907`) fails to
  negotiate with *this specific AP*. It is a per-AP interop issue, not a blanket
  capability gap.

### Harmful experiment reverted
- An aggressive teardown (`WiFi.mode(WIFI_OFF)` toggling between attempts)
  corrupted the net stack: `netstack cb reg failed 12308` and WiFi scans
  returning empty (`Scan result length: 15` == `{"networks":[]}`). Reverted to
  a clean minimal STA sequence (`persistent(false)` + `WIFI_STA` +
  `setSleep(false)` + `begin`). Recovery required **reflash + full power-cycle**
  (soft reset was insufficient); scans then returned to normal (`length=359`).
- The cleartext password `[DEBUG]` serial log used during diagnosis was removed.

### Portal UX note
- The config page (Scan/SSID/Password/MQTT fields) is complete and renders in a
  real browser at `http://192.168.4.1/`. Apple captive popups
  (`GET /hotspot-detect.html`) are a restricted mini-browser with limited JS —
  use a normal browser to operate the portal.

## Options to try WPA3 from firmware (experimental, not guaranteed)
- Explicitly enable PMF after `WiFi.begin()`:
  `wifi_config_t.sta.pmf_cfg.capable=true` (+ `required` depending on AP), via
  `esp_wifi_set_config(WIFI_IF_STA, &conf)`.
- Or bump/change the `espressif32` platform version (newer S3 WiFi blob often
  improves mixed/PMF interop).

## Deployment recommendation
- Give the scanner a **WPA2-PSK (AES) 2.4GHz SSID**, or set the target AP's PMF
  to optional. This is the reliable, standard IoT approach.

---

# Memory — End-to-End MQTT Verification + CJK Font + QUERY UX (2026-10-07)

Follow-on to Redmine #79. With a WPA2 hotspot (D500MA) the MQTT chain works, so
P1–P3 were verified against the live server and the QUERY display UX was fixed.

## End-to-end verification (live server blog.softsnail.com:8040 / broker :1883)
- Test item (server DB): barcode `RAC-28VP` ("冷氣", spec 2.8KW).
- **P2 stock-out VERIFIED**: scanning in STOCK_OUT mode →
  `[MQTT] warehouse/stock/out: RAC-28VP x1` → server `stock_out_ok`; REST API
  confirmed `quantity` 3 → 2 and `updated_at` bumped. Real production DB write.
- **P1 query VERIFIED**: `[MQTT] Query sent: RAC-28VP (ok=1)` → `status=found`;
  item details display on screen.
- P3 stocktake: not yet exercised on hardware. NOTE its acks
  (ui_stocktake_on_started/scanned/ended) still touch LVGL from the MQTT
  context — same cross-task bug fixed for query below; fix when verifying P3.

## Bug: MQTT response never updated the LVGL display (cross-task)
- Symptom: server replied `found` / `stock_out_ok` correctly, but the screen
  never showed the item.
- Root cause: `mqtt_on_response()` runs in the MQTT/loop() context, not the
  LVGL task, yet it called `ui_show_scan_result()` directly. LVGL widgets may
  only be updated from the LVGL task (holding lvgl_lock) — same reason scans go
  through a pending buffer + `ui_process_pending_scan()`.
- Fix: added a cross-task result channel `ui_post_result(label, content)`
  (portMUX-protected buffer) drained in `ui_process_pending_scan()` on the LVGL
  task. `mqtt_on_response` now posts via `ui_post_result` for found/not_found/
  stock/fallback.

## Bug: item detail flashed then was overwritten
- Root cause 1: `ui_on_scan()` unconditionally called
  `ui_show_scan_result(type, content)` at entry, writing the raw barcode into
  the same label the server item detail uses → race/overwrite. Fixed: QUERY
  branch no longer double-writes; the detail line is reserved for the server
  result.
- Root cause 2: `SCAN_COOLDOWN_MS` was defined but NEVER applied in
  scan_preview.cpp, so continuous scanning re-fired the decode callback every
  frame. Fixed: added dedup/cooldown in `decode_current_frame()` (Core 0) —
  same content within `SCAN_COOLDOWN_MS` is suppressed. Cooldown now 10000 ms.
  CAVEAT: this cooldown is GLOBAL across modes, so STOCK_IN/OUT/STOCKTAKE cannot
  re-scan the SAME barcode within 10 s. If per-item repeat scanning is needed
  for P2/P3, make the cooldown per-mode (long for QUERY, short ~1.5 s for the
  rest).

## CJK (Traditional Chinese) font — subset compiled into flash
- UI used Montserrat only (Latin) → Chinese item names ("冷氣") didn't render.
- Toolchain: node via nvm (user-local, no sudo) + `lv_font_conv` 1.5.3.
  `NotoSansCJK-Regular.ttc` is a collection (lv_font_conv rejects `ttcf`), so
  fonttools (venv) extracted face index 3 = "Noto Sans CJK TC" to a standalone
  OTF.
- Subset: Big5 level-1 ~5401 common hanzi + ASCII, 16px bpp2 →
  `src/ui/fonts/lv_font_cjk_tc_16.c` (2.7 MB C). Declared via LV_FONT_DECLARE;
  scan result labels (`g_scan_lbl_barcode`, `g_scan_lbl_status`) switched to it.
- Flash: 38.0% → 50.2% (~+386 KB). Acceptable. CAVEAT: only Big5-L1 chars; rare/
  variant chars in item names won't render — regenerate the subset with those
  chars added (ideally auto-collect from the server DB item names).

## QUERY one-shot UX (operator presses key to scan)
- After a query hit: stop the live preview (`ui_preview_stop()` — preview is
  drawn by LovyanGFX outside LVGL, otherwise it paints under the result),
  invalidate the scan PAGE object (NOT lv_scr_act(), which crashes) to repaint
  a clean background, show only the item detail line (no "Item OK"), set
  `g_query_result_shown`.
- Short press when a result is shown → return HOME (instead of scanning again).
  Flag reset on entering the SCAN page.
- Verified on hardware: clean black screen + "冷氣 2.8KW x2 @", key returns home.

## Env/tooling notes
- node/npm via nvm: `export NVM_DIR="$HOME/.nvm"; . "$NVM_DIR/nvm.sh"`.
- fonttools venv at /tmp/fontvenv (ephemeral). Regen font:
  extract TC face → lv_font_conv --size 16 --bpp 2 --format lvgl with ASCII
  range + CJK symbols.

---

# Memory — P3 盤點顯示 + UI 跨 task 修復 + Serial 注入（2026-10-08）

延續 Redmine #79。P1/P2 先前已實機驗證；本輪聚焦 P3 盤點顯示與一系列
cross-task LVGL 更新問題，並加 serial barcode 注入便於測試。

## 核心主題：所有「非 LVGL task 更新畫面」都要走 cross-task 機制
LVGL widget 只能在 LVGL task（Core 1，持 lvgl_lock）更新。從 loop()/MQTT
context 直接呼叫 lv_* 會「設了值但不刷新」。本輪把殘留的這類點全部修正。

## P3 盤點顯示（兩層問題）
1. cross-task：ui_stocktake_on_started/scanned/ended + ui_stocktake_begin/end
   原本在 MQTT/nav context 直接改 label。改走 ui_post_result("STInfo"/"STEnd")
   → LVGL task drain 時套用。
2. 被預覽蓋住：盤點計數原寫在 g_scan_lbl_info(screen y~119)，但相機 viewfinder
   由 LovyanGFX 直接畫在 screen y=85~235，每幀蓋掉文字 → 看起來「沒顯示」。
   修法：新增 g_scan_lbl_stocktake，放在**預覽框下方**(page y=215 → screen
   y~239，在 VF 下緣 235 外)，CJK 字型。盤點所有回饋改寫此 label。
   實機驗證：serial 注入 RAC-28VP，預覽框下方正確顯示「冷氣 sys=2 act=3 d=+1」。

## 狀態列 WiFi/MQTT 圖標一直紅色
- 根因同上：ui_update_status() 從 loop() 直接 lv_obj_set_style_text_color，
  cross-task 不刷新 → [W]/[M] 停在初始紅色（連上也不變綠）。
  （wifi_is_connected()/mqtt_is_connected() 其實正確。）
- 修法：ui_update_status() 改成只存旗標(portMUX)；新增 ui_apply_status()
  由 LVGL task 在 ui_process_pending_scan() 開頭套色 + lv_obj_invalidate。
  實機驗證：連上 WPA2+MQTT 後 [W]/[M] 變綠。✅

## Serial barcode 注入（測試用）
- main.cpp loop() 的 serial parser 改為**整行緩衝**：純數字行→設 VCM；單一
  +/=/-/p→焦點微調；其他文字(含 '-'/字母的 barcode)→ ui_inject_scan()。
- ui_inject_scan(type, content) 走與真實解碼相同的 cross-task pending-scan
  buffer，繞過 Core 0 的 dedup cooldown（測試時可連續注入同碼）。
- 用法：serial 打 RAC-28VP⏎ 即模擬掃描。限 SCAN 頁 + 對應模式有效。
  實機驗證：[SERIAL] inject barcode: RAC-28VP → stocktake/scan 正常。

## 重入 SCAN 頁
- 清殘留文字：進 SCAN 頁清空 barcode/status/stocktake labels（但**不重置**
  g_st_* session 狀態，open session 可跨頁存活）。
- 重建 session：若在 COUNT 模式但無 active session（離開再回來、session 已失效），
  show_page SCAN 分支自動補發 ui_stocktake_begin()。修「No session」問題。

## 狀態（build 皆 SUCCESS，RAM ~50% / Flash ~50%）
- 實機已驗證：serial 注入、盤點計數顯示（預覽框下方）、WiFi/MQTT 圖標變綠。
- 待實機驗證：重入清畫面、COUNT 重入自動重建 session（本輪最後兩項，使用者
  exit 前未測）。

## 修改檔案
- src/ui/ui_main.cpp / src/ui/ui_main.h（cross-task result/status、盤點 label、
  ui_inject_scan、重入清畫面+重建 session）
- src/main.cpp（serial 整行解析 + barcode 注入）


---

# Session 2026-10-19 — LVGL 雙 disp/indev 整理（單一真相源）+ 觸控 indev 正確掛上

對應上一個 touch session 的待辦：「LVGL indev 路徑待整理：main.cpp 有雙 disp 註冊
（display_init 一個 + main 自己一個），touch indev 段被註解，touch_read 其實沒被 LVGL 呼叫」。
本次把 LVGL 設定統一到 `display.cpp`，移除 `main.cpp` 的重複設定。純軟體整理 + `pio run`
驗證；實機未測（硬體不在手邊）。

## 根因：兩套 LVGL 設定互相打架

`main.cpp setup()` 先呼叫 `display_init()`（display.cpp），**之後又自己做了一整套 LVGL 設定**：

| 項目 | display.cpp display_init()（正確但被蓋掉） | main.cpp setup()（實際生效的第二套） |
|------|------|------|
| lv_init | ✅ | ⚠️ **重複呼叫** |
| draw buf | PSRAM `buf1/buf2`（40 行/buf） | 另一組 `buf1/buf2`（1/4 螢幕） |
| disp 註冊 | `lvgl_flush_cb`（**帶 s_spi_mutex**） | `flush_cb`（**忽略 mutex** + `[FLUSH]` debug 印） |
| touch indev | ✅ 註冊 `lvgl_touch_cb`→`touch_read` | ❌ 整段註解（舊 LoadProhibited 遺留） |
| esp_timer tick | ❌ 無 | ✅ 2ms |

LVGL 以**最後註冊**的 disp 為 default → 實際跑的是 main.cpp 那套。後果：
1. **touch indev 從沒被掛上** → `touch_read` 永遠不會被 LVGL 呼叫（就算硬體修好也收不到事件）。
2. **作用中的 flush 忽略 `s_spi_mutex`** → 這是 latent SPI race：`scan_preview` 的 render task
   (Core 1) 會 `xSemaphoreTake(s_spi_mutex)`，`scan_preview_start()` 也 `display_set_spi_mutex()`
   把 mutex 設進 display.cpp 的全域，**但作用中的 main.cpp flush 根本不讀它** → 正是
   Session 2026-09-22 根因 #4 打過的那種跨 task SPI 交錯，只是被第二套 disp 重新引入。

## 修正

**display.cpp = 單一真相源**（display_init 現含全部）：
- 既有：`lv_init` + PSRAM draw buf + disp(`lvgl_flush_cb` 帶 `s_spi_mutex`) + touch indev(`lvgl_touch_cb`)。
- **新增**：`#include <esp_timer.h>`、`lvgl_tick_cb`（`lv_tick_inc(2)`）、在 `display_init()` 尾端
  `esp_timer_create/start_periodic(2000us)`。tick 從 main.cpp 搬過來（`lv_conf.h` `LV_TICK_CUSTOM 0`
  所以 tick 必要）。
- touch indev 註解補一段：`touch_init()` 永遠回 true（容錯），CST816 硬體死，`lvgl_touch_cb`
  每次回 RELEASED（無害）；indev 掛好，硬體修/換好後**免改碼**即可用。

**main.cpp 移除第二套**：
- 刪 `buf1/buf2/draw_buf` 靜態、`flush_cb`、`lvgl_tick_cb`。
- setup() 刪整段 buffer alloc + `lv_init` + `lv_disp_draw_buf_init` + 第二個 `lv_disp_drv_register`
  + 註解掉的 indev + 第二個 esp_timer tick。
- **保留** `g_lvgl_mux = xSemaphoreCreateRecursiveMutex()`（`lvgl_task()` 的 `lvgl_lock` 用，與
  display 無關）。
- **保留** line 64 `static lgfx::LGFX_Device& tft = display_get_tft();`（`update_btn_ui` 等直接
  LovyanGFX 繪圖仍用）。
- `#include <esp_timer.h>` 留著（檔頭註解仍述 tick 設計，無害；已無 esp_timer_ 呼叫）。

## 觸控結論（不變）

CST816D 硬體不通（Session 2026-10-04 定案：I2C bus 正常、IMU 0x6B 可讀、驅動與官方逐字相同，
但觸控晶片全無 ACK/ID → 晶片/FPC 故障）。驅動碼完整保留，indev 現正確掛上，**硬體修好即可用**。
操作暫維持 BOOT 單鍵 + serial 注入。

## build

`pio run` SUCCESS：RAM 50.4% (165144 / 327680)、Flash 50.2% (1580301 / 3145728)。
（先前 50.5%/165416 → 略降，少一組重複 disp 驅動結構 + 單一 draw buf 配置路徑。）

## 待實機驗證

1. 畫面正常刷新（現在走 display.cpp 的 mutex-aware `lvgl_flush_cb`，非 main.cpp 舊 flush）。
2. 掃描預覽期間不再有 SPI race（flush 現真正尊重 `s_spi_mutex`）——Session 2026-09-22 根因 #4
   的根治從此對作用中的 flush 生效。
3. 觸控硬體若更換/修復，indev 應立即收到事件（無需改碼）。

## 修改檔案

- src/display/display.cpp（+esp_timer.h、+lvgl_tick_cb、display_init 尾端起 tick、touch indev 註解補充）
- src/main.cpp（移除第二套 LVGL 設定：buf/flush_cb/tick_cb/第二 disp/註解 indev；保留 g_lvgl_mux、tft）


---

# Session 2026-10-20 — Phase A：韌體 GUI 頁面結構對齊 web-sim 模擬器

使用者要求「先 A 再 C」：A = 讓 PlatformIO 實機韌體的 LVGL GUI 頁面結構對齊 web-sim
（web-sim/index.html，純前端模擬器，當 UI 規格藍本）；C = 之後補後端讓功能真的能動。
本 session 完成 Phase A（純 UI 骨架對齊，不碰 BLE/網路後端）。每步 pio run 驗證，最後 clean
full rebuild SUCCESS。實機未測（硬體不在手邊）。

## 差異盤點（web-sim vs 韌體 before）

web-sim 有 9 頁（home/scan/result/photo/settings/inventory/inventory-list/log/about），韌體只有
5 頁（HOME/SCAN/SETTINGS/INVENTORY/PHOTO）。HOME 選單 web-sim 4 項、韌體 3 項。查詢結果 web-sim
有獨立 6 欄位頁、韌體只塞一行 label。設定頁 web-sim 7 項清單、韌體只有單一亮度 slider。

## 單鍵導航模型（關鍵適配）

web-sim 靠滑鼠點按/方向鍵；韌體只有 BOOT 單鍵（觸控硬體不通）：
短按=NAV_NEXT(移焦點/下一項)、長按=NAV_CONFIRM(確認)、長按住>2s=NAV_HOME(回首頁，全域攔截)。
所以 web-sim 的多按鈕頁在韌體改成「焦點環 index + NAV_NEXT 循環 + NAV_CONFIRM 執行」。

## 實作（7 步，只動 ui_main.h/.cpp + main.cpp mqtt_on_response）

- Step 1：ui_page_t 新增 UI_PAGE_RESULT/INVENTORY_LIST/LOG/ABOUT。HOME_ITEM 3→4
  (SCAN/INVENTORY/SETTINGS/ABOUT)，labels 改中文「掃描/盤點/設定/關於」+ 按鈕字型改
  lv_font_cjk_tc_16。4 項仍在既有垂直置中演算法內(4*50+3*8=224<296)。
- Step 2：ABOUT 獨立頁（BOARD_NAME/vFW_VERSION/HW ESP32-S3/Display ST7789T3/Camera OV5640/ID）。
  on_home_select(ABOUT) 改導 UI_PAGE_ABOUT，移除舊「借 scan 頁顯示版本」。nav CONFIRM 回 HOME。
- Step 3+4：RESULT 獨立頁（標題+barcode+6 欄位 品名/規格/數量/儲位/供應商/入庫日+兩鈕 重掃/回主頁，
  g_result_focus 焦點環）。接真實查詢：新增結構化跨 task buffer g_item_*（portMUX，比照既有
  ui_post_result 模式）+ ui_post_result_item()；main.cpp mqtt_on_response found 分支改呼叫它（補齊
  supplier/date_in，barcode 取 data["barcode"]）；ui_process_pending_scan() 開頭 drain g_item_pending
  → ui_preview_stop + result_populate + show_page(RESULT)。
  **移除**舊 label=="Item" 分支與 g_query_result_shown 整套（宣告+reset+SCAN NEXT 判斷）；SCAN NEXT
  現永遠 scan_preview_request_hires()，QUERY 命中改由 RESULT 頁承接。
- Step 5：INVENTORY_LIST 頁（標題+共N筆+可捲動 flex column 清單，invlist_render 讀真實
  g_st_entries[]/g_st_entry_count，空=「尚無資料」+返回/上傳全部 g_invlist_focus）。INVENTORY 頁
  NAV_NEXT 改 render+show INVENTORY_LIST（對齊 web-sim「查看盤點清單▶」），CONFIRM 仍結束 session。
- Step 6：LOG 頁 placeholder（標題+日期 label g_log_day_offset 0..-6 循環「今日/N天前」+body
  「尚無紀錄(待Phase C接SD卡)」+hint）。nav NEXT 換日、CONFIRM 回 SETTINGS。**不接 SD。**
- Step 7：SETTINGS 由單 slider 擴充為 7 列可聚焦清單(WiFi/MQTT/BLE/亮度/音量/語言/工作日誌)。
  flex column 可捲動；settings_refresh() 讀 wifi_is_connected()/mqtt_is_connected() 唯讀顯示；
  BLE「未配對」、音量「60%」stub；語言 g_settings_lang_zh 切換；LOG「檢視▶」。update_settings_focus()
  邊框+底色 COL_FOCUS + scroll_to_view。nav NEXT 移焦點(亮度列特例：短按+40 wrap 套
  display_set_brightness)；CONFIRM 依列 套亮度/切語言/進 LOG/唯讀。

## Phase A stub（UI 做出、資料佔位，留給 Phase C）

- LOG 清單：placeholder，不讀 scan_log/SD。
- SETTINGS BLE「未配對」、音量「60%」：純顯示。WiFi/MQTT 唯讀狀態（無配網/broker 設定子頁）。
- INVENTORY_LIST「上傳全部」= ui_stocktake_end() 批次 commit（每筆 scan 本就已 publish）。
- 語言切換目前只改 settings 顯示字，未接全域 i18n。

## 留給 Phase C（本次完全不碰）

BLE HID 實際鍵盤輸出；工作日誌真實 SD 讀寫；WiFi/MQTT/BLE 設定子頁；音量實際輸出；
web-sim 的 INPUT 子通道(BLE/MQTT 切換)、offline queue sync icon 等後端相依互動；
PHOTO 頁互動流程（web-sim 有拍照/上傳按鈕，韌體 PHOTO 頁仍為簡版，Phase A 未納入對齊範圍）。

## build

每步 pio run SUCCESS。最終 clean full rebuild：RAM 50.6% (165704 / 327680)、
Flash 50.4% (1584917 / 3145728)。4 新頁僅 +~4.5KB Flash。

## 待實機驗證

1. HOME 4 項中文顯示 + 單鍵焦點循環 + 長按進入。
2. QUERY 命中自動跳 RESULT 頁、6 欄正確、相機殘影被覆蓋、重掃回 SCAN、回主頁回 HOME。
   ⚠️ RESULT 頁 show_page 會 anim slide-in；需確認 ui_preview_stop 後相機不再蓋頁（RESULT 頁
   進入已 ui_preview_stop）。
3. 盤點→INVENTORY→短按進 INVENTORY_LIST 顯示真實筆數/數量。
4. SETTINGS 清單焦點循環+自動捲動、亮度單鍵調整、語言切換、工作日誌入口進 LOG。
5. ABOUT/LOG 導航返回正確。

## 修改檔案

- src/ui/ui_main.h（ui_page_t +4、ui_post_result_item 宣告）
- src/ui/ui_main.cpp（4 新頁 create+nav+show_page+hide_all、HOME 4 項、結構化 result buffer、
  SETTINGS 清單化、共用 helpers create_page_container/create_action_btn/set_btn_focused）
- src/main.cpp（mqtt_on_response found → ui_post_result_item 6 欄）


---

# Session 2026-10-20 (續) — Phase C 第一項：接通 SD 工作日誌

承 Phase A（GUI 骨架對齊，LOG 頁為 placeholder）。本輪做 Phase C 第一項：把 SD 工作日誌從
dead code 接成真的會記錄、LOG 頁讀真實資料。純軟體接線 + pio run 驗證；實機未測。

## 現況盤點（又一個「後端完整但從未接線」）

scan_log.cpp / sd_card.cpp 後端實作完整（date-based JSON Lines + pending 離線佇列 + sync），
但 grep 證實 **sd_card_init() / scan_log_init() / scan_log_add() 從未被 main.cpp 或掃描路徑呼叫**
→ SD 卡從未掛載、從未記錄任何東西。同 memory.md 2026-10-04 揭露的「Phase 標✅但沒接線」模式。
另一個隱患：**全韌體無 NTP 時間同步**，time(nullptr) 停在 1970 epoch → 日期檔名與時間戳全錯
（所有紀錄會落在 1970-01-01.json）。

## 接線（5 步，每步 pio run）

- Step 1：main.cpp setup() 在 display_init 之後（LCD 先占 FSPI bus）、nvs_init 之後掛載
  sd_card_init()（成功印 used/total MB + scan_log_init()；無卡 graceful 失敗印「work log
  disabled」）。加 include storage/sd_card.h + scan_log.h。
- Step 2：scan_log 新增字串版入口 scan_log_add_entry(type, content)，供 UI 層用（免依賴 decoder
  的 DecodeResult）。重構出共用 scan_log_write(type, content) 建 JSON {ts,type,content} 寫當日檔
  +pending；scan_log_add(DecodeResult&) 與 scan_log_add_entry 都走它。
- Step 3：ui_main.cpp ui_on_scan() 開頭用 mode_tags[g_scan_mode]（QUERY/INPUT/IN/OUT/COUNT）呼叫
  scan_log_add_entry(tag, content)，每次掃描都記錄。在 LVGL task（Core 1）執行，與 LCD flush 同核
  序列化（SD 走共用 FSPI bus）。
- Step 4：main.cpp on_wifi_connected() 加 configTzTime("CST-8", pool.ntp.org, time.google.com,
  time.cloudflare.com)。解決 1970 epoch → 日期正確。TZ=台灣 CST-8（無 DST）。
- Step 5：LOG 頁接真實資料（取代 Phase A placeholder）。scan_log 加
  scan_log_date_for_offset(day_offset, buf, len)（time(nullptr)+offset*86400 strftime）。ui_main.cpp
  引入 ArduinoJson；log_render() 讀 scan_log_read(date,0,50)→StaticJsonDocument<4096> 解析 JSON
  array→逐筆「HH:MM:SS type content」（ts 取 'T' 後的時間），空=「尚無紀錄」。日期 label 改真實
  「◀ YYYY-MM-DD ▶」。show_page LOG 進入 reset offset=0 + render；LOG NAV_NEXT 換日（0..-6 循環）
  + re-render。

## ⚠️ SPI 共用 bus 風險（待實機驗證）

SD 與 LCD 共用 FSPI (SPI2)，LovyanGFX 以 bus_shared(use_lock=true) 跑。但 SD 走 Arduino SD lib
的獨立 SPIClass(FSPI)，**不取 LVGL flush 用的 s_spi_mutex**。scan_log 寫入在 LVGL task（Core 1）
與 LCD flush 同核序列化，理論上降低衝突，但兩者是不同鎖機制——需實機確認掃描寫 SD 時不會干擾
LCD（參照 Session 2026-09-22 根因 #4 的 SPI race 教訓）。若實機有問題，應讓 SD 存取也取
s_spi_mutex。

## build

每步 pio run SUCCESS。最終 clean full rebuild：RAM 50.7% (165988 / 327680)、
Flash 52.2% (1643173 / 3145728)。較 Phase A +~58KB（ui_main 引入 ArduinoJson 解析器）。

## 待實機驗證（開發環境無法確認）

1. SD 卡實際掛載（serial「SD mounted: x/y MB」）、目錄 /sd/logs 建立。
2. 掃描後 scan_log_add_entry 真的寫入當日 JSON（serial「[LOG] #n: ...」）。
3. NTP 同步後 time 正確（日期檔名 YYYY-MM-DD 非 1970）。
4. LOG 頁顯示當日真實紀錄（HH:MM:SS 模式 內容）、換日切不同檔。
5. SD 寫入 + LCD flush 共用 FSPI 無衝突（SPI race）。

## 留給 Phase C 後續

BLE HID；WiFi/MQTT/BLE 設定子頁；音量；INPUT 子通道（BLE/MQTT 切換）；pending 佇列
WiFi 重連自動 sync（scan_log_sync_pending 已實作但確認有被呼叫）；LOG 頁 pending/離線標示。

## 修改檔案

- src/main.cpp（SD 掛載 + scan_log_init + NTP configTzTime）
- src/storage/scan_log.h / .cpp（scan_log_add_entry、scan_log_write 共用、scan_log_date_for_offset）
- src/ui/ui_main.cpp（ui_on_scan 記錄、LOG 頁 log_render 讀真實資料 + ArduinoJson）


---

# Session 2026-10-20 (續2) — SD 工作日誌實機測試：抓到並修好 2 個 bug

承上接線完 SD 工作日誌後，使用者要求「先測試再 BLE HID」。板子接在 /dev/ttyACM0
（Espressif 303a:1001 USB JTAG/serial，udev 規則已套，0666）。實機燒錄＋序列埠測試，
抓到 2 個真 bug 並修好，最終實機驗證寫入成功。

## 測試方法與 ESP32-S3 USB-CDC 的坑

- ARDUINO_USB_CDC_ON_BOOT=1：板子一 reset，USB-CDC 會 drop + 重新列舉，開著的 cat fd 會失效 →
  直接 `cat /dev/ttyACM0` 抓不到開機 banner。解法：用 pyserial 迴圈，reset(DTR/RTS 脈衝)後
  retry 重開 port 讀 ~8s，吃過重新列舉。esptool 與 cat 不能同時開 port（互搶）。
- **序列埠測試加了 nav 指令**：原本序列只有「數字=VCM、+/-/p=焦點、其他文字=inject barcode」，
  無法用序列導航（inject 只在 SCAN 頁才會進 ui_on_scan）。新增單字元 `n`=NAV_NEXT、
  `c`=NAV_CONFIRM、`h`=NAV_HOME（main.cpp loop 序列解析），讓無實體按鍵也能驅動導航測試。
  符合既有 serial-inject 測試哲學，保留於碼內。

## Bug #1：SD 路徑重複 /sd/sd/（已修）

實機 inject 後：`fopen(/sd/sd/logs/1970-01-01.json) failed`。根因：Arduino ESP32 SD lib 內部
掛載在 SD_MOUNT_POINT="/sd" 並**自動前綴**，所以傳給 SD.open() 的路徑必須**相對卡根**。
config.h 的 SD_LOG_DIR 是 "/sd/logs" → 變成 /sd + /sd/logs = /sd/sd/logs。
修：SD_LOG_DIR "/sd/logs"→"/logs"、SD_RESOURCE_DIR 同理去 /sd 前綴。

## Bug #2：此卡 SD.mkdir 全失敗（已繞過）

修完路徑後仍 `fopen(/sd/logs/1970-01-01.json) failed`。加診斷定位：
- `[SD] Card type: SD, Size: 1910MB` + `root write test: open=1` + `exists(/sdtest.txt)=1`
  → **卡能掛載、能在根目錄寫檔**。
- `SD.mkdir(/logs)=0 exists=0`、`SD.mkdir(/wlog)=0 exists=0` → **mkdir 對任何名字全失敗**，
  但根目錄檔案寫入正常。（root 既有 `System Volume Information/`、`MAP/` 是 PC 建的。）
- 結論：此卡／lib／共用 SPI 組合**無法建立目錄**，但根目錄檔案讀寫正常。這是 ESP32 Arduino SD
  + 某些卡的已知 FAT/SPI 怪癖。
- **修法：改用扁平根目錄檔案**，不用子目錄：`/log_YYYY-MM-DD.json`（每日）、
  `/pending_queue.json`（離線佇列）。scan_log.cpp log_file_path/pending_file_path/scan_log_read
  全改根層；scan_log_init 移除 mkdir；sd_card_init 移除 mkdir + 診斷。

## 實機驗證通過 ✅

- 燒錄 → HOME→SCAN(序列 h,c) → inject `WLOG-AAA`/`WLOG-BBB`：
  `[LOG] #1: WLOG-AAA`、`[LOG] #2: WLOG-BBB` **寫入成功，無 fopen error**。
- 導航到 LOG 頁（h → n,n → c 進 SETTINGS → n×6 → c 進 LOG）：全程無 crash、無 SPI error，
  LOG 頁 render（scan_log_read + ArduinoJson 解析）執行正常（render 無序列輸出屬正常）。
- **SD 寫入 + LCD flush 共用 FSPI 無衝突**（先前擔心的 SPI race 未出現；root write test 與
  正常 logging 期間 LCD 預覽同時運作無異常）。先前根因 #4 的隱患在此 flat-file 路徑下未重現。

## 仍待確認（非阻斷）

- **NTP 未同步時日期為 1970**：測試時 WiFi AP（D500MA）不在，WiFi `st=1 NO_SSID_AVAIL`，
  configTzTime 未跑 → 檔名 `/log_1970-01-01.json`。有 WiFi 時 NTP 同步後日期會正確（Step 4 邏輯
  已驗證會呼叫，只是這次沒 AP）。待有 WPA2 AP 時補驗日期正確性。
- LCD 畫面內容未目視（無法遠端看螢幕）；以「無 crash + 寫入成功 + render 無誤」間接驗證。

## build（最終 clean rebuild）

RAM 50.7% (166008)、Flash 52.3% (1645005 / 3145728)。

## 修改檔案（本段）

- src/config/config.h（SD_LOG_DIR /sd/logs→/logs、SD_RESOURCE_DIR 去 /sd）
- src/storage/scan_log.cpp（log/pending/read 路徑改 /log_*.json、/pending_queue.json 根層；
  init 移除 mkdir）
- src/storage/sd_card.cpp（移除 mkdir + 測試診斷，註明 flat-file 原因）
- src/main.cpp（序列 nav 指令 n/c/h 供測試）


---

# Session 2026-10-20 (續3) — SD 工作日誌「尚無紀錄」實機除錯 → 根因是 SD 卡硬體故障

使用者實機看 LOG 頁顯示「尚無紀錄」。序列 dump 也回 `today's log: []`。但稍早明明看到
`[LOG] #n` 寫入成功。追查後**根因確定是 SD 卡本身無法 commit 寫入（故障/山寨卡），非韌體 bug**。
過程中順手修了 3 個真的韌體 bug，並加了開機健檢。

## 除錯歷程（每步序列數據）

1. **現象**：write 報成功（`[LOG] #1: xxx`）但同 session dump `today's log: []`，且每次開機
   `[LOG] Init: total=0`（跨重開機讀回都空）。
2. 加 dump 診斷：`dump path=/log_1970-01-01.json exists=1 rawlen=0` → 檔案**存在但 0 bytes**。
   （日期 1970 是因無 WiFi→NTP 沒同步，與此 bug 無關。）
3. 疑 readString() → 改 size-based read loop。無效。
4. 疑 SPI 共用 bus 競爭 → sd_card read/write 加 s_spi_mutex + flush。無效（離開 SCAN 頁、
   preview 停了也一樣 0 bytes）。
5. 加開機自測（bus 絕對靜止、無 preview/LVGL）：`FILE_WRITE nwrote=13 size=0 rblen=0`
   → **純 write/read round-trip 就失敗**，與 LCD/preview 無關。
6. 疑 SPI 速度 → SD_SPI_FREQ 20MHz→4MHz。無效。
7. **決定性證據**：`raw.write nwrote=13 size=0 rblen=0 used:4915200->4947968`
   → print/write 回報 13 bytes、**usedBytes 增加 32768（配置了一個 cluster）**，但**檔案 size 仍 0、
   讀不到資料**。

## 根因：SD 卡故障 / 山寨卡（硬體，非韌體）

證據鏈：
- 卡能掛載、報 size(1910MB)、exists/remove(metadata)正常。
- mkdir 對所有名字全失敗；data write 配置了 cluster(usedBytes+32KB)但 size 恆 0、讀不回。
- 20MHz 與 4MHz 行為一致；跨重開機一致。
- 32KB cluster 對 2GB FAT32 偏大 → 疑**山寨容量/異常格式化卡**。
這是典型「卡接受寫入、配置 cluster，但不 persist 資料到實體媒體」的故障卡特徵。
韌體路徑本身已證實正確（write 回成功、路徑/日期/讀取邏輯皆對）。

## 本輪順手修的 3 個真韌體 bug（在找到硬體根因前）

1. **SD 路徑重複 /sd/sd/**（config.h SD_LOG_DIR /sd/logs→/logs；Arduino SD lib 自動前綴 /sd）。
2. **SD.mkdir 全失敗** → 改用扁平根目錄檔 /log_YYYY-MM-DD.json、/pending_queue.json（無子目錄）。
3. **readString() 回空** → sd_card_read_file 改 size-based read loop（available()+read()）。
這 3 個都是真實缺陷，即使換好卡也該保留這些修正。

## 保留的改善

- **開機 SD 寫入健檢**（sd_card_init 尾端）：寫一個 byte 再讀回，若 size==0 → 印
  `[SD] WARNING: write did not commit ... work log DISABLED` 並 `_mounted=false`。
  **實機驗證此卡正確觸發警告並停用 logging**（避免靜默掉資料）。換好卡後會印 `write-commit OK`。
- SD read/write 加 s_spi_mutex 守護（防未來真的共用 bus 競爭；此卡問題雖非此因，但本就該有）。
- SD_SPI_FREQ 降 4MHz（SPI SD 安全標準速率，保留）。
- 序列指令：n/c/h（導航）、d（dump 今日日誌）供測試。

## 待辦（硬體）

- **換一張正常的 microSD 卡**（建議知名品牌 ≤32GB，FAT32 正常格式化）。換卡後開機應見
  `[SD] write-commit OK` + `[MAIN] SD mounted`，工作日誌即可正常記錄/讀回。
- 換卡後補驗：掃描→[LOG] #n→LOG 頁顯示紀錄；有 WPA2 AP 時 NTP 同步→日期正確。

## build（最終 clean rebuild）

RAM ~50.7%、Flash ~52%。健檢實機觸發警告正常。

## 修改檔案（本段）

- src/config/pinout.h（SD_SPI_FREQ 20M→4M）
- src/storage/sd_card.cpp（s_spi_mutex 守護、size-based read、開機寫入健檢、bad-card 停用）
- src/storage/scan_log.cpp（扁平根目錄路徑 — 上一段已改）
- src/main.cpp（序列 d dump 指令）

## ✅ 換卡後實機驗證通過（2026-10-20 收尾）

換上新 SDHC 4GB 卡後實機全鏈驗證 OK：開機 `[SD] write-commit OK` + `SD mounted 23/3807MB` +
`[LOG] Init: total=2 pending=2`（讀回既有）；SCAN 注入 NEWCARD-1/2 → dump 讀回 4 筆完整 JSON
（ts/type/content 齊全，新舊都在）；LOG 頁導航 render 無 crash。SD 工作日誌確認可用，先前兩張卡
為故障/山寨卡。時間戳仍 1970（測試環境無 WPA2 AP，NTP 未同步；接上 WiFi 後自動轉真實時間，不影響功能）。

---

# Session 2026-10-21 — BLE HID 無線條碼槍實作 + 實機驗證（Phase C）

README 核心賣點，先前完全未實作(無 ble_hid.* 檔、INPUT 模式只印 TODO)。本 session 從零做完
並實機燒錄+手機配對驗證通過。採「NimBLE 自寫薄層 HID + 按需啟動」策略。

## 函式庫選型

- lib_deps 加 h2zero/NimBLE-Arduino@^2.2.3。**不用** T-vK/ESP32-BLE-Keyboard(對 arduino-esp32
  3.x 相容差)。自寫薄層 src/network/ble_hid.h/.cpp 只依賴 NimBLE 本體 + NimBLEHIDDevice，避開
  第三方 keyboard fork 版本相容債。
- 環境: espressif32 7.1.3 / arduino-esp32 3.x(framework 4.20017)。SDK 內建 Bluedroid 控制器
  (CONFIG_BT_BLE_ENABLED=y, 非 IDF NimBLE)；NimBLE-Arduino 跑在其上可編譯。
- config.h 加 BLE_DEVICE_NAME "SoftSnail"。

## ble_hid API

begin/stop/is_started/is_connected/type_line(str,含尾端 Enter)/set_stopped_cb。
HID_REPORT_MAP=標準 boot keyboard(report id 1)；ascii_to_hid US layout 完整對應表；
send_key 逐字 press+release 各 delay 8ms。

## 實機除錯：依序突破 5 個關卡(每個都以 serial/配對實測定位)

1. **NimBLE 能否編譯/放得下**：可。Flash 52%→58%(+182KB NimBLE)、RAM +靜態。
2. **WiFi/BLE 共存 abort**(coex_core_enable)：WiFi 開著啟用 BT 控制器就 abort。根因=單一
   2.4GHz radio。修:ble_hid_begin 先 esp_wifi_stop()+esp_wifi_deinit()(完全釋放 WiFi)。
3. **廣播掃不到名稱**(使用者找不到 SoftSnail)：NimBLE 2.x 不自動放 device name。修:明確
   adv->setName(BLE_DEVICE_NAME)+enableScanResponse(true)。
4. **配對後 HID 連線建不起來**(onConnect 從不觸發)：缺 Just-Works。修:setSecurityIOCap(
   BLE_HS_IO_NO_INPUT_OUTPUT)+setSecurityAuth(true,false,true)+setPower(ESP_PWR_LVL_P9)。
   → 實機手機配對成功、`[BLE] host connected`、送字 "SoftSnail-BLE-TEST-123" 完整打進文字框
   (英文輸入法下;中文 IME 會把字母攔截轉中文字=主機端問題非韌體)。
5. **INPUT 模式 OOM**(送字送一半 BLE_INIT Malloc failed，使用者只收到 "GUN78")：相機預覽+
   BLE 堆疊同時吃內部 DRAM，連線下只剩 3.5KB。根因=BLE 控制器需 ~89KB internal DRAM。修:
   INPUT 模式完全釋放 WiFi DRAM(esp_wifi_deinit，已在 #2)騰出~24KB;且 scan_preview
   framebuffer(60KB)移 PSRAM。→ 連線下 internal DRAM 26.7KB，實機 inject "GUN-FULL-12345"
   完整打進文字框、無 Malloc failed。
   ⚠️ 量 heap 要看 MALLOC_CAP_INTERNAL(非 total，total 含 PSRAM 會誤判)。

## UI 接線(Step 6)

- 狀態列加 [B] 圖示:灰=關/紅=等待配對/綠=已連線。ui_apply_ble_status() 每 tick 查 BLE 狀態著色
  (只在變化時 repaint)。W/M/B 右對齊 -80/-52/-24。
- 設定頁 BLE 列顯示真實 關閉/等待配對/已連線。

## 按需啟動(Step 5)

ui_cycle_scan_mode 切到 INPUT→ble_hid_begin、切離→ble_hid_stop；show_page 進 SCAN 若已 INPUT
則 begin、離開 SCAN 頁→stop。ble_hid_stop 走 s_stopped_cb 回呼讓 main.cpp 重建 WiFi
(g_wifi.begin()+mqtt_client_init())。

## 共存壓測(Step 7)抓到並修好 2 個 crash

1. **PC=0 InstrFetchProhibited**(離開 INPUT)：onDisconnect 在 teardown 中呼叫
   startAdvertising() 於半 deinit 堆疊。修:s_stopping 旗標，teardown 時 onDisconnect return；
   stop 時 s_stopping=true + delay(20) 再 deinit。
2. **assert "free target outside heap"**(NimBLEServer::~ 刪 callbacks)：s_server_cb 是 static
   全域，NimBLEDevice::deinit 會 delete 它→刪靜態物件 crash。修:改 new HidServerCallbacks()
   (NimBLE 擁有並 free)。
壓測:INPUT進→連線(27KB)→離開(無crash,DRAM 回 114KB=WiFi+BLE 全回收無洩漏)→再進→再離開，穩定。

## 設計取捨(重要)

**BLE 槍(INPUT)模式下 WiFi/MQTT/查詢不可用** —— 單一 radio + 內部 DRAM 不足以讓 WiFi 與 BLE
堆疊同時常駐，故 INPUT 模式 deinit WiFi 換 BLE，離開 INPUT 才重建 WiFi。對「無線條碼槍打字到
配對主機」的使用情境是合理的(槍模式本就不需要查後端)。

## 最終 build

clean rebuild: RAM 52.6% (172432 / 327680)、Flash 58.1% (1827485 / 3145728)。

## 實機已驗證

手機配對 SoftSnail → `[BLE] host connected` → INPUT 模式掃描(serial inject)值
"GUN-FULL-12345" 完整打進文字框含 Enter → 離開 INPUT WiFi 重建 → begin/stop 循環無 crash 無洩漏。

## 保留的 serial 測試指令(main.cpp loop)

n/c/h=導航(NEXT/CONFIRM/HOME), b/B=BLE begin/stop, t=BLE 送測試字串, s=狀態+heap(total/
internal/psram), d=dump 工作日誌, 純數字=VCM 焦點, +/=/-/p=焦點微調。

## 修改檔案

- platformio.ini(NimBLE-Arduino@^2.2.3)
- src/config/config.h(BLE_DEVICE_NAME)
- src/network/ble_hid.h/.cpp(新檔，薄層 HID keyboard)
- src/ui/ui_main.cpp(ui_on_scan INPUT 送 BLE、按需 begin/stop、狀態列[B]、設定頁 BLE 狀態)
- src/main.cpp(ble_hid_set_stopped_cb 重建 WiFi、serial b/B/t/s 指令)
- src/scan_preview.cpp(viewfinder framebuffer 移 PSRAM)

## 待辦 / 後續

- 長條碼逐字送(每字元 ~16ms)對很長的碼會略慢;目前可接受。
- INPUT↔其他模式切換時 WiFi reinit 約數秒;若需更快可評估保留 WiFi init 只 stop(但 DRAM 會更緊)。
- README 的 BLE HID 賣點現已實作;可更新 README 移除「尚未實作」註記。

---

# Session 2026-10-21 (收尾) — Phase C 兩大項完成 + Redmine 記錄 + README 更新

Phase C（接通後端讓功能真的能動）前兩大項完成並實機驗證：

| 項目 | Redmine | 狀態 |
|------|---------|------|
| SD 工作日誌（接通 + 2 張故障卡 + 3 bug + 換卡驗證） | #101 | ✔ 實機通過 |
| BLE HID 無線條碼槍（NimBLE 從零 + 5 關 + 2 crash + 手機驗證） | #102 | ✔ 實機通過 |

## Redmine issues（本次建立）

- #101 http://blog.softsnail.com:2024/issues/101 — Phase C SD 工作日誌
- #102 http://blog.softsnail.com:2024/issues/102 — Phase C BLE HID
- 用共用工具 `python3 ~/projects/scripts/redmine.py create --subject ... --tracker 3 --priority 2 --desc-file ...`（自動讀專案 .env：project xq_xscriqf / tracker 3=功能）。Textile 格式完整記錄過程。

## README 更新

- Hardware Wireless 行：BLE「尚未實作」→「NimBLE；單一 radio，INPUT/BLE 模式會暫停 WiFi」。
- Features：BLE HID 行補「NimBLE、US layout+Enter、按需啟動、BLE 期間暫停 WiFi/MQTT」；工作日誌行改「依模式記錄到 SD 每日 /log_YYYY-MM-DD.json、NTP 時間戳、LOG 頁逐日瀏覽」。
- Dependencies：ESP32-BLE-Keyboard(T-vK) → NimBLE-Arduino(h2zero，自寫薄層)。
- Project Structure：network/ 補 ble_hid.h/.cpp。
- 決策記錄：新增 #101 / #102 兩條。

## Phase C 剩餘（未做）

- WiFi/MQTT/BLE 設定子頁（配網、broker、配對）— 目前設定頁僅唯讀狀態。
- 音量實際輸出（蜂鳴器非板載）。
- INPUT 子通道（web-sim 的 BLE/MQTT 切換）；目前 INPUT 固定走 BLE HID。
- scan-input（MQTT 推送 Web UI）兩端皆未實作。
- 工作日誌 pending 佇列 WiFi 重連自動 sync（scan_log_sync_pending 已實作，確認呼叫時機）。

---

# Session 2026-10-21 (續) — 設定頁改「開/關」式動作（詳細設定在 web portal）

使用者定案：GUI 設定頁只做開/關這類動作，SSID/密碼/broker 等詳細設定都在 WiFi web 配網頁做。

## 改動

- SETTINGS CONFIRM switch（ui_main.cpp ui_nav_event）：
  * WiFi 列 → g_wifi_portal_cb()（開 web 配網 portal；value 顯示「配網中...」）。
  * BLE 列 → toggle ble_hid_begin()/ble_hid_stop() + settings_refresh()（開/關；起 BLE 會暫停 WiFi）。
  * 亮度 → 套用 display_set_brightness + 移焦點；語言 → 切換；工作日誌 → 進 LOG 頁。
  * MQTT / 音量 → 唯讀（value 顯示狀態）。
- 新增 hook ui_set_wifi_portal_cb(ui_wifi_portal_cb_t)（ui_main.h/.cpp）+ g_wifi_portal_cb static。
  讓 ui_main 不直接相依 main.cpp 的 g_wifi。
- main.cpp 註冊 ui_set_wifi_portal_cb([](){ g_wifi.startConfigPortal(); })。g_wifi.loop()(loop line 508)
  服務 portal 請求。
- settings_refresh 已顯示真實狀態（WiFi 已連線/未連線、MQTT 已連線/未連線、BLE 關閉/等待配對/
  已連線），符合開關語意。

## 實機驗證通過 ✔

- HOME→設定→BLE 列 CONFIRM：`startAdvertising ok=1 → advertising "SoftSnail"`(開) → 再 CONFIRM
  `HID keyboard stopped → re-establishing WiFi`(關)。乾淨 toggle 無 crash。
- WiFi 列 CONFIRM：`[WIFI] opening config portal (from settings)`；startConfigPortal 冪等
  (已開則 no-op)。開機無 WPA2 AP 時 WiFiManager 本就自動開 portal：
  `AP started - SSID: SoftSnail-Scanner-Setup, IP: 192.168.4.1`、`Portal started at http://192.168.4.1`。

## build

pio run SUCCESS（RAM/Flash 與 BLE session 同級，~52%/58%）。

## 修改檔案

- src/ui/ui_main.h/.cpp（SETTINGS CONFIRM WiFi 開 portal + BLE toggle、ui_set_wifi_portal_cb hook）
- src/main.cpp（註冊 wifi portal callback）

---

# Session 2026-10-21 (A) — 查詢鏈端到端實機驗證通過（WiFi D500MA + server）

使用者提供 WPA2 熱點 D500MA + server http://blog.softsnail.com:8040。完整查詢鏈實機跑通。

## 連線確認（開機）

- WiFi：`WiFiManager: Connected! IP: 10.42.0.240`（D500MA，WPA2）。
- NTP：`[TIME] NTP sync requested (TZ=CST-8)` —— 有 WiFi 時校時正常（1970 疑慮解除）。
- MQTT：`blog.softsnail.com:1883 Connected`（首次 rc=-2 重試後連上）+ `Subscribed: warehouse/response/esp32-001`。

## server 契約確認（REST /items）

OpenAPI（:8040/openapi.json）：server 已是完整 ERP（items/sales/purchases/quotations/repairs/
accounting），掃描器相關 HTTP 僅 `/items/{barcode}` CRUD；查詢/入出庫/盤點走 MQTT（非 HTTP）。
`ItemResponse` schema 欄位 name/spec/quantity/location/supplier/date_in **與韌體 RESULT 頁 6 欄位
完全對齊**。GET /items/RAC-28VP = {冷氣, 2.8KW, qty 2, supplier 日立, date_in 2026-07-28}。

## 查詢鏈實機（QUERY 模式 inject RAC-28VP）

```
[SERIAL] inject barcode: RAC-28VP
[LOG] #8: RAC-28VP                      ← 同步寫 SD 工作日誌
[MQTT] Query sent: RAC-28VP (ok=1)
[MQTT] Response: status=found
[PREVIEW] stopped                       ← 切到 RESULT 頁
```

RESULT 頁顯示 冷氣/2.8KW/qty=2/日立（CJK 中文字型正常，使用者目視確認）。整條鏈
（掃描→MQTT 查詢→server 查 DB→warehouse/response→RESULT 頁 6 欄位）運作正確。

→ A 收尾：Phase A 的 RESULT 頁 + Phase C SD 日誌 + P1 查詢鏈，在真實 server 下一次驗證全通。

## 下一步：B（scan-input 推送 Web UI）

server 原始碼在本機 ~/projects/barcode-warehouse-server。現況無 scan-input endpoint、無
WebSocket；MQTT handler 只訂 query/create/stock/stocktake。B 需 server 加 scan-input 訂閱 +
WebSocket/SSE 推 /web/items + 前端 JS，韌體加 INPUT 子通道。屬跨專案 + 需部署到 live server。

---

# Session 2026-10-21 (修) — 掃描無 RESULT 頁 → 根因是 BLE 用 esp_wifi_deinit 破壞 WiFi 恢復

使用者回報：首頁→掃描(QUERY)→掃描，沒出現 RESULT 頁。但剛 boot 後第一次查詢是正常的。

## 定位

- 新鮮開機查 RAC-28VP：`Query sent → Response: found → RESULT 頁` 正常。
- 失敗情境重現：**先在設定頁 toggle 過 BLE** 後，查詢就沒 `Query sent`、沒 RESULT 頁。
- SD 日誌 dump 確認掃描有記到(tag=QUERY，mode 正確)，但 QUERY 分支因 `!mqtt_is_connected()` 走
  「MQTT offline」(只寫 LCD label 無 serial)→靜默失敗。
- BLE 循環後 serial：`[BLE] stopped → re-establishing WiFi → WiFiManager Connecting D500MA →
  Connection failed (status=255 NO_SHIELD) → 開 config portal`。

## 根因

先前為解 BLE OOM 在 ble_hid_begin 用**原始 esp_wifi_deinit()** 釋放 DRAM。但這**繞過 Arduino WiFi
類別**，desync 其內部 init 狀態 → BLE stop 後 `WiFi.mode(WIFI_STA)/begin()` 回 status=255
(NO_SHIELD)，WiFi 永遠連不回來 → MQTT 死 → 查詢靜默失敗 → 無 RESULT 頁。試過在 stop callback
加 WIFI_OFF→STA 重 toggle 也無效(deinit 後 Arduino 層已 desync)。

## 修法（改用 Arduino 原生 WiFi.mode，不用 raw deinit）

- ble_hid_begin：`esp_wifi_stop()+esp_wifi_deinit()` → 改 **`WiFi.mode(WIFI_OFF)`**。Arduino 類別
  狀態保持同步，WIFI_OFF 一樣釋放驅動 DRAM，之後 WIFI_STA 能乾淨重 init。
- stopped callback：`WiFi.mode(WIFI_STA)` + delay + `g_wifi.begin()` + `mqtt_client_init()`。
- ble_hid.cpp include esp_wifi.h → WiFi.h。
- memory.md 2026-10-06 曾警告 WIFI_OFF 破壞 netstack，但那是「連線嘗試之間反覆 toggle WIFI_OFF」
  的情境；BLE 期間一次乾淨 OFF→(BLE)→STA bracket 是標準做法，實測恢復正常。

## 實機驗證（完整 BLE 循環 + 查詢）

```
BLE ON:  BLE started=1 connected=1 | internal=70428   ← 70KB 餘裕(WIFI_OFF 釋放足夠；
                                                          無先前 3.5KB OOM/Malloc failed)
BLE OFF: stopped → re-establishing WiFi
         WiFiManager: Connected! IP: 10.42.0.240       ← 無 NO_SHIELD，WiFi 恢復
         [MQTT] Connected!                             ← MQTT 恢復
查詢:    Query sent: RAC-28VP → Response: status=found → RESULT 頁
```

→ 「掃描無 RESULT 頁」根因解除。此修法比原 esp_wifi_deinit 更佳：釋放夠 DRAM(BLE 連線下仍
70KB) 且 WiFi 可恢復。

## build

clean rebuild：RAM 52.6% (172440)、Flash 58.1% (1827637)。

## 修改檔案

- src/network/ble_hid.cpp（esp_wifi_deinit → WiFi.mode(WIFI_OFF)；include esp_wifi.h→WiFi.h）
- src/main.cpp（stopped callback WiFi.mode(WIFI_STA)+begin）

## A 收尾釐清（2026-10-21）：RESULT 頁 OK，相機 1D 解碼率是另一議題

使用者回報真實相機掃 RAC-28VP「只有 Query ok 停 SCAN」。隔離測試：
- 真實掃描：`scan: starting multi-frame attempt` 後**無 [DECODE]** → 相機沒解出 1D 條碼。
- 序列注入 RAC-28VP：`Query sent → found → PREVIEW stopped → RESULT 頁`，使用者確認螢幕顯示正確
  （冷氣/2.8KW/qty=2/日立，CJK 正常）。
→ 查詢鏈 + RESULT 頁邏輯**完全正常**；相機掃不出 RAC-28VP(Code128) 是**已知 1D 鏡頭光學限制**
  （FD5640 定焦/VCM=120，見 Session 2026-09-27/10-03），與 GUI 無關。QR 可穩定掃。
→ A 正式收尾。驗證 GUI 查詢用序列注入或 QR Code 版條碼即可。
