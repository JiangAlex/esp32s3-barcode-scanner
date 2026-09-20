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

## Board Status

- **ESP32-S3** (Waveshare ESP32-S3-Touch-LCD-2, N8R8 label)
- **Flash**: Winbond W25Q128JVSIQ 16MB
- **PSRAM**: NOT present (hardware absent, not software disabled)
- **I2C devices**: 0x6B (QMI8658 IMU), 0x7E (unknown — GT911 secondary addr?)
- **Touch**: No CST816 @ 0x15, no GT911 readable @ 0x5D or 0x7E

## Hardware Findings

- PSRAM allocation fails → DRAM fallback works
  - `buf1=0x3fcb2420 buf2=0x3fcbba30` (38.4KB each)
- BOOT button (GPIO0) always reads HIGH (1), never becomes LOW (0)
  - Hardware/board-level issue, possibly different wiring on Waveshare board
  - Serial 't' command used as workaround for toggle testing

## Key Files

- `src/main.cpp` — current working LVGL + LovyanGFX integration
  - `update_btn_ui()` — delete/recreate button + direct `tft.fillRect()`
  - `flush_cb()` — writes pixels to TFT via LovyanGFX SPI
  - `lvgl_task()` — runs on Core 1, processes `g_ui_update_pending`
- `lv_conf.h` — LVGL 8.4.0 config
- `platformio.ini` — flash mode qio, 115200 monitor

## Working Solution (Button Color Update)

```c
// In update_btn_ui() — delete old, recreate new, draw directly
lv_obj_del(g_scan_btn);
g_scan_btn = lv_btn_create(lv_scr_act());
// ... set up button styles and label ...
uint16_t col = g_scanning ? 0xF800 : 0x07E0;
tft.fillRect(20, 130, 200, 60, col);  // Direct LovyanGFX draw
```

## Pending

1. Verify `tft.fillRect()` works reliably on hardware
2. Identify 0x7E I2C device — likely GT911 needing reset sequence
3. Integrate TouchLib GT911 driver
4. Full barcode scanner UI
5. Camera OV5640 integration
