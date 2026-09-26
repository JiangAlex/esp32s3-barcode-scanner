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
