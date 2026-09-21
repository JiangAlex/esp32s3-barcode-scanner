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
- **PSRAM**: NOT present (hardware absent, not software disabled)
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

## Pending

1. Verify `tft.fillRect()` works reliably on hardware
2. Re-test BOOT button (GPIO0) on hardware after the `INPUT_PULLUP` fix —
   confirm `[DBG] GPIO0=0` appears while the button is held
3. Identify 0x7E I2C device — likely GT911 needing reset sequence
4. Integrate TouchLib GT911 driver
5. Full barcode scanner UI
6. Camera OV5640 integration
