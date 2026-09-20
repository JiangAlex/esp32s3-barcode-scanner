# TODO — 開發待辦事項

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

- [ ] 狀態列（WiFi / MQTT / BLE 圖標 + 時間）
- [ ] 首頁 — 3 項列表選單
- [ ] 掃描頁面 — Camera 預覽 + 模式切換
- [ ] 查詢結果頁面 — 品項完整資訊顯示
- [ ] 盤點模式頁面 — 連續掃描 + 累積清單
- [ ] 盤點清單頁面 — 列表 + 上傳按鈕
- [ ] 拍照辨識頁面 — 預覽 + 拍照 + 上傳
- [ ] 設定頁面 — WiFi / MQTT / BLE / 語言 / 亮度 / 音量
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

- [ ] quirc (QR Code) 整合
- [ ] ZBar (1D barcode) 整合
- [ ] 單次掃描模式
- [ ] 連續掃描模式
- [ ] 掃描成功蜂鳴器回饋

## Phase 7: 儲存與日誌

- [ ] SD 卡目錄結構建立 (/sd/logs/, /sd/cache/)
- [ ] 工作日誌寫入（JSON Lines 格式）
- [ ] 工作日誌讀取（按日期）
- [ ] 離線暫存佇列（掃描結果存 SD）
- [ ] 恢復連線自動同步

## Phase 8: 拍照辨識

- [ ] OV2640 切換至 JPEG SVGA/UXGA 模式
- [ ] 拍照預覽（靜態照片顯示）
- [ ] HTTP POST multipart 圖片上傳
- [ ] 上傳狀態回饋 UI

## Phase 9: 整合測試

- [ ] 所有模式切換流程測試
- [ ] BLE HID + WiFi MQTT 同時運作
- [ ] 離線 → 上線同步測試
- [ ] 盤點批次上傳測試
- [ ] 長時間運作穩定性
- [ ] 記憶體使用監控 (heap free)

## Phase 10: 優化

- [ ] LVGL 動畫與過渡效果
- [ ] 低功耗模式（閒置時降亮度/關背光）
- [ ] OTA 韌體更新
- [ ] 錯誤處理與重試機制
- [ ] 設定值 NVS 持久化
