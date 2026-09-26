# TODO — 開發待辦事項

## 決策記錄：掃描鏈路除錯 (2026-09-22)

完整條碼掃描 UI（預覽 → 解碼 → 顯示）打通，實機驗證 `[DECODE] QR: TEST123` 成功。
排除的根因依序如下（每項皆實機驗證）：

1. **PSRAM 未啟用** — `heap_caps_malloc(SPIRAM)` 回傳 NULL，LVGL/quirc/VF 緩衝全擠爆 DRAM。
   修正：`platformio.ini` 加 `board_build.arduino.memory_type = qio_opi` + `-D BOARD_HAS_PSRAM`（對齊官方 `CONFIG_SPIRAM_MODE_OCT`）。
2. **capture task stack 過小** — quirc identify 需 16KB、`quirc_decode`（Reed-Solomon + ~8KB quirc_data）需更多。
   修正：preview_cap stack → 32768。
3. **OV5640 grayscale 不可用** — esp32-camera 對 OV5640 grayscale 支援不穩，畫面雜訊。
   修正：改 `PIXFORMAT_RGB565`（官方 demo 驗證過的格式）。
4. **SPI 跨 task race** — VF overlay 繪圖在 SPI mutex 外，與 LVGL flush 交錯觸發 `xQueueGenericSend` assert。
   修正：VF 影像+overlay 全包進單一 `s_spi_mutex` + 單一 `startWrite/endWrite`。
5. **byte order** — 實機診斷（純色參考條）確認相機 RGB565 與面板一致，`writePixels` 不需 swap。
6. **解碼未接 RGB565** — `barcode_decode()` 加 RGB565 → luma(Rec.601) 轉換餵 quirc。
7. **ECC failure（quirc size=21 穩定但資料糾錯失敗）** — 幾何/格式全對，卡在模組黑白取樣。
   - 中間發現：`sharpness=2` 過度銳化在模組邊界產生振鈴，反而惡化 → 改 `sharpness=0` + `contrast=1`。
   - **根本解法**：在餵 quirc 前做 **Otsu 自適應二值化**（`barcode_decoder.cpp`）——從 luma 直方圖算最佳閾值，
     硬壓成純黑/純白，quirc 取樣不再受灰階邊界干擾。實機：對準後幾乎立即解出 `TEST123`，穩定。
   - 成本：偵測到 QR 時多三趟全幀處理，fps 由 7.5 降至 ~2.5（僅解碼期間；無 QR 時維持 7.5）。可再優化為只處理取景框中央區域。
   - 掃描頁另停用省電變暗，避免掃描中背光降低。

> 注意：目前僅支援 **QR Code（quirc）**。1D 條碼（Code128/EAN/UPC）仍為 TODO。

## 待實作：提升 QR 解碼成功率 — SVGA 偵測→抓拍方案 1(b)

> 現況：QVGA 320×240 下 v1 QR（21×21）每模組像素偏少，加定焦鏡頭，成功率仍偏低。
> 已定案採方案 1(b)：**預覽維持 QVGA 流暢，偵測到 QR 候選時切 SVGA 800×600 抓單張再解碼**。
> 高解析度已定 **SVGA 800×600**（成功率↑、凍結約 0.5-1s，UXGA 太慢不採）。

實作步驟（分步 build 驗證，注意 crash 風險）：

1. **decoder 支援動態尺寸** — `barcode_decode()` 已用 `fb->width/height` 適配；需在幀尺寸與 quirc 當前尺寸
   不符時自動 `quirc_resize(w,h)`。已確認 `quirc_resize` 可重複呼叫（內部 calloc 重配，失敗不改狀態）。
   ⚠️ quirc buffer 用 `calloc`（DRAM）；SVGA 需 ~470KB，確認走 PSRAM 或 DRAM 夠（目前 DRAM 較緊，考慮改 ps_malloc）。
2. **camera 乾淨切換函式** — 加「切 SVGA RGB565 / 切回 QVGA RGB565」（現有 `camera_set_jpeg_mode(false)`
   誤切回 GRAYSCALE，需修為 RGB565）。切換後丟棄 2-3 過渡幀再抓穩定幀。
3. **capture task 狀態機**（`scan_preview.cpp`）：
   - 平常 QVGA：只跑 `quirc_end`+`quirc_count`（輕量偵測，不做完整 decode）以保 fps
   - `quirc_count>0` → 觸發一次性高解析抓拍：set SVGA → 丟過渡幀 → 抓 1 幀 → quirc_resize(800,600)
     → Otsu + 完整 decode → set QVGA → quirc_resize(320,240) 恢復預覽
   - 抓拍期間預覽凍結（正常）
   - 注意：SVGA 幀的解碼在 Core 0，stack 已 32KB；Otsu 三趟 480000 px 較慢，確認可接受

風險點：framesize 切換過渡幀、quirc 兩尺寸 resize 的記憶體、抓拍期間與 render task 的同步。


## Phase 0: 硬體移植 — 微雪 ESP32-S3-Touch-LCD-2

> 從 ESP32-S3-DevKitC-1 分體式移植到微雪一體板
> 詳細移植計畫見 [PORTING.md](PORTING.md)

- [x] 研究新硬體 GPIO pinout 和規格
- [x] 確認 Camera GPIO（從官方 demo 取得）
- [x] 下載官方 Demo，確認 LCD/SD/Touch 的確切 GPIO
- [x] 更新 `platformio.ini`（新增 `-Isrc`，LovyanGFX 保留）
- [x] 重寫 `src/config/pinout.h`（全部 GPIO 重新分配）
- [x] 更新 `src/config/config.h`（OV5640、ST7789T3、共用 bus）
- [x] 改寫 `src/display/display.cpp`（ILI9341→ST7789T3，invert/bus_shared + 觸控 indev）
- [x] 改寫 `src/camera/camera.cpp`（新 GPIO + OV5640 自動辨識）
- [x] 改寫 `src/storage/sd_card.cpp`（與 LCD 共用 FSPI bus）
- [x] 新增 `src/input/touch.cpp/.h`（CST816D 觸控 + LVGL indev）
- [x] 移除搖桿相關程式碼（原本無 joystick 檔案）
- [x] 更新 UI 觸控適配（純 widget，indev 於 display 層註冊）
- [x] Build 驗證通過（pio run SUCCESS：RAM 46.1% / Flash 36.0%）
- [x] LVGL LoadProhibited crash — 移除 `lv_indev_drv_register()` 修復
- [x] 按鈕顏色更新失效 — 使用 `tft.fillRect()` 直接 LovyanGFX 繪圖修復
- [ ] 實機測試（顯示方向/色彩反相、觸控對位、SPI 共用穩定性）

## Phase 1: 硬體驅動層 ✅ (已完成 — 原始 DevKitC-1 版本)

- [x] TFT LCD (ILI9341) SPI 驅動 — LovyanGFX
- [x] OV2640 Camera 驅動 — esp32-camera
- [x] SD Card SPI 驅動
- [x] Buzzer PWM 驅動
- [x] LVGL 初始化 + draw buffer (PSRAM)

## Phase 2: 輸入系統

- [ ] 搖桿 5-way 硬體接線 (GPIO 3/21/33/34/47)
- [ ] 搖桿驅動 + debounce (20ms)
- [ ] LVGL indev 註冊 (LV_INDEV_TYPE_KEYPAD)
- [ ] BOOT 按鍵 (GPIO 0) → LV_KEY_ESC
- [ ] 長按偵測（BOOT 長按 = 回首頁）

## Phase 3: UI 頁面

- [~] 狀態列（WiFi / MQTT 圖標）— WiFi/MQTT 圖標已實作；**缺 BLE 圖標與時間**
- [x] 首頁 — 3 項列表選單（單鍵導航：短按下一項、長按確認）
- [x] 掃描頁面 — Camera 預覽 + 模式切換（實機驗證，含取景框/掃描線/解碼）
- [ ] 查詢結果頁面 — 品項完整資訊顯示（掃描頁目前僅顯示條碼內容，未接完整品項欄位）
- [~] 盤點模式頁面 — 連續掃描 + 累積清單（累積邏輯已實作；清單 UI 僅顯示計數）
- [~] 盤點清單頁面 — 列表 + 上傳按鈕（計數 + 上傳觸發已接；**逐項列表 UI 未實作**，`ui_inventory_upload` 仍為 TODO）
- [ ] 拍照辨識頁面 — 預覽 + 拍照 + 上傳
- [~] 設定頁面 — 目前僅亮度 slider；**WiFi / MQTT / BLE / 語言 / 音量 未實作**
- [ ] 虛擬鍵盤 — LVGL lv_keyboard (英數)
- [ ] 工作日誌頁面 — 按日期瀏覽
- [ ] LVGL Group 焦點管理（頁面切換時切換 group）

## Phase 4: 中文字體

- [ ] 選定中文字型（Noto Sans CJK / 思源黑體）
- [ ] lv_font_conv 產生 20px / 16px / 12px 字體檔
- [ ] 整合進 LVGL，測試中文顯示
- [ ] i18n 字串表建立（中文 + 英文）
- [ ] 語言切換功能

## Phase 5: 網路與通訊

- [ ] WiFi 連線管理（自動重連）
- [ ] MQTT Client（PubSubClient）
- [ ] MQTT 查詢功能 (publish query → subscribe response)
- [ ] MQTT 掃描輸入推送 (scan-input topic)
- [ ] MQTT 盤點上傳 (inventory topic)
- [ ] HTTP POST 圖片上傳
- [ ] BLE HID 鍵盤模擬 (ESP32-BLE-Keyboard)
- [ ] BLE 廣播控制 + 配對狀態顯示
- [ ] BLE HID 後綴設定 (Enter / Tab / None)
- [ ] WiFi + BLE 共存測試

## Phase 6: 條碼解碼

- [x] quirc (QR Code) 整合 — 實機驗證 `[DECODE] QR: TEST123` 成功
- [ ] 1D barcode 整合 — 解碼器方案**未定**（ZBar 在 ESP32 整合困難，待評估；lib 目前僅有 quirc）
- [ ] 單次掃描模式 — 目前為連續掃描；單次模式尚未實作
- [x] 連續掃描模式 — capture task 每幀解碼 + SCAN_COOLDOWN_MS 去重
- [x] 掃描成功蜂鳴器回饋 — 已實作（條件編譯 `BUZZER_ENABLED`；此板無 buzzer，預設關閉）

## Phase 7: 儲存與日誌

- [ ] SD 卡目錄結構建立 (/sd/logs/, /sd/cache/)
- [ ] 工作日誌寫入（JSON Lines 格式）
- [ ] 工作日誌讀取（按日期）
- [ ] 離線暫存佇列（掃描結果存 SD）
- [ ] 恢復連線自動同步

## Phase 8: 拍照辨識 ✅

>- [x] OV2640 切換至 JPEG SVGA/UXGA 模式
>- [x] 拍照預覽（靜態照片顯示）
>- [x] HTTP POST multipart 圖片上傳
>- [x] 上傳狀態回饋 UI

## Phase 9: 整合測試 ✅

>- [x] 所有模式切換流程測試
>- [x] BLE HID + WiFi MQTT 同時運作
>- [x] 離線 → 上線同步測試
>- [x] 盤點批次上傳測試
>- [ ] 長時間運作穩定性
>- [ ] 記憶體使用監控 (heap free)

## Phase 10: 優化 ✅

>- [x] LVGL 動畫與過渡效果
>- [x] 低功耗模式（閒置時降亮度/關背光）
>- [x] OTA 韌體更新
>- [ ] 錯誤處理與重試機制
>- [x] 設定值 NVS 持久化
