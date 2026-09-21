/**
 * @file main.cpp
 * @brief ESP32-S3 barcode scanner — factory approach
 *
 * Key fixes from factory analysis:
 * 1. Draw buffers use heap_caps_malloc(MALLOC_CAP_SPIRAM) — fallback to DRAM if PSRAM absent
 * 2. LVGL tick via esp_timer every 2ms (matching factory bsp_lv_port.cpp)
 * 3. I2C mutex + bsp_i2c style read (matching factory bsp_i2c.cpp)
 * 4. Touch init disabled — no CST816 on this board
 */

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <Wire.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config/pinout.h"
#include "config/config.h"
#include "display/display.h"
#include "ui/ui_main.h"
#include "storage/nvs_settings.h"
#include "power.h"
#include "camera/camera.h"

// ─── I2C ────────────────────────────────────────────────────────────────────

static SemaphoreHandle_t g_i2c_mux = NULL;
static bool i2c_lock(int ms) {
    if (!g_i2c_mux) return true;
    TickType_t t = (ms==-1) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
    return xSemaphoreTakeRecursive(g_i2c_mux, t) == pdTRUE;
}
static void i2c_unlock(void) { if (g_i2c_mux) xSemaphoreGiveRecursive(g_i2c_mux); }

static bool i2c_reg_read(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
    if (!i2c_lock(-1)) return false;
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) { i2c_unlock(); return false; }
    Wire.requestFrom(addr, len);
    for (size_t i = 0; i < len && Wire.available(); i++) *data++ = Wire.read();
    i2c_unlock();
    return true;
}

// ─── LVGL ───────────────────────────────────────────────────────────────────

static SemaphoreHandle_t g_lvgl_mux = NULL;
static bool lvgl_lock(int ms) {
    TickType_t t = (ms==-1) ? portMAX_DELAY : pdMS_TO_TICKS(ms);
    return xSemaphoreTakeRecursive(g_lvgl_mux, t) == pdTRUE;
}
static void lvgl_unlock(void) { xSemaphoreGiveRecursive(g_lvgl_mux); }

// ─── Display (LovyanGFX — shared instance from display.cpp) ──────────────────

// Use the shared LGFX instance from display.cpp to avoid duplicate SPI init
static lgfx::LGFX_Device& tft = display_get_tft();

// RGB565 color constants
// ARM is little-endian: lv_color_t.full is uint16_t, stored as LE bytes.
// lv_color_hex(hex) treats hex as ARGB8888 and converts to RGB565 - WRONG approach for raw RGB565.
// LVGL v8: lv_color_hex expects ARGB8888 input.
// But lv_color_t{ .full = N } directly sets the uint16_t RGB565 value.
// Use .full approach for predictable RGB565 values on ARM.
// COL_GREEN = 0x07E0 = RGB(0, 252, 0) in RGB565
// COL_RED   = 0xF800 = RGB(248, 0, 0) in RGB565
static const lv_color_t COL_GREEN    = { .full = 0x07E0 };   // green RGB565
static const lv_color_t COL_RED      = { .full = 0xF800 };   // red RGB565
static const lv_color_t COL_WHITE    = { .full = 0xFFFF };   // white RGB565

static bool g_ui_ready = false;       // set true after all widgets created
static bool g_scanning = false;
static bool g_ui_update_pending = false;  // LVGL UI needs update
static lv_obj_t* g_scan_btn = nullptr;  // LVGL button widget (updated from loop)
static lv_obj_t* g_btn_label = nullptr;  // button's label child

// Update the scan button to reflect current g_scanning state
// All LVGL calls must be made from within LVGL task (lvgl_lock held)
static void update_btn_ui(void) {
    if (!g_ui_ready) return;
    if (!g_scan_btn || !g_btn_label) {
        Serial.println("[UI] null ptr");
        return;
    }

    // Delete old button and recreate with new color
    lv_obj_del(g_scan_btn);

    // Recreate button
    g_scan_btn = lv_btn_create(lv_scr_act());
    lv_obj_set_width(g_scan_btn, 200);
    lv_obj_set_height(g_scan_btn, 60);
    lv_obj_center(g_scan_btn);
    lv_obj_add_flag(g_scan_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(g_scan_btn, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g_scan_btn, g_scanning ? COL_RED : COL_GREEN, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_scan_btn, LV_OPA_COVER, LV_PART_MAIN);

    // Recreate label
    g_btn_label = lv_label_create(g_scan_btn);
    lv_label_set_text(g_btn_label, g_scanning ? "STOP" : "SCAN");
    lv_obj_center(g_btn_label);
    lv_obj_set_style_text_color(g_btn_label, (lv_color_t){ .full = 0x0000 }, LV_PART_MAIN);
    lv_obj_set_style_text_font(g_btn_label, &lv_font_montserrat_16, LV_PART_MAIN);

    // Direct LovyanGFX draw to bypass LVGL flush pipeline
    uint16_t col = g_scanning ? 0xF800 : 0x07E0;
    tft.fillRect(20, 130, 200, 60, col);
    Serial.printf("[UI] recreated btn=%s bg=0x%04X tft_draw\n",
        g_scanning ? "STOP" : "SCAN", col);
}

// Toggle scan on/off (called from loop)
static void toggle_scan(void) {
    g_scanning = !g_scanning;
    g_ui_update_pending = true;
    Serial.printf("[SCAN] toggled: %s\n", g_scanning ? "ON" : "OFF");
}

// ─── LVGL draw buffers (factory style: MALLOC_CAP_SPIRAM) ────────────────────
// heap_caps_malloc with MALLOC_CAP_SPIRAM falls back to DRAM if PSRAM absent

static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;

// ─── LVGL display driver ─────────────────────────────────────────────────────

static void flush_cb(lv_disp_drv_t* d, const lv_area_t* a, lv_color_t* c) {
    uint32_t w=a->x2-a->x1+1, h=a->y2-a->y1+1;
    uint32_t npixels = w * h;
    static uint32_t cnt = 0;
    if (cnt < 5) {
        Serial.printf("[FLUSH] #%u %ux%u px=0x%04X\n", cnt, w, h, c[0].full);
        cnt++;
    }
    tft.startWrite();
    tft.setAddrWindow(a->x1,a->y1,w,h);
    tft.writePixels((uint16_t*)c, npixels);
    tft.endWrite();
    lv_disp_flush_ready(d);
}

// ─── LVGL tick timer (factory style: esp_timer every 2ms) ────────────────────

static void lvgl_tick_cb(void* arg) {
    lv_tick_inc(2);  // 2ms period
}

// ─── LVGL task (factory style: Core 1 + recursive mutex) ────────────────────

static void lvgl_task(void* param) {
    Serial.println("[LVGL] task started on Core 1");
    while (1) {
        if (lvgl_lock(1000)) {
            if (g_ui_update_pending) {
                g_ui_update_pending = false;
                update_btn_ui();
            }
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─── QMI8658 IMU ────────────────────────────────────────────────────────────

static void init_qmi8658(void) {
    uint8_t id = 0;
    if (i2c_reg_read(IMU_I2C_ADDR, 0x0F, &id, 1)) {
        Serial.printf("[QMI8658] ID=0x%02X\n", id);
    } else {
        Serial.println("[QMI8658] read failed");
    }
}

// ─── I2C scan ───────────────────────────────────────────────────────────────

static void i2c_scan(void) {
    Serial.println("[I2C] scan:");
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission(true) == 0) Serial.printf("  0x%02X\n", a);
    }
}

// ─── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    // Confirm CPU is running
    pinMode(48, OUTPUT);
    digitalWrite(48, HIGH);
    delay(100);
    digitalWrite(48, LOW);
    delay(100);
    digitalWrite(48, HIGH);

    // BOOT button (GPIO0, active LOW) — must enable internal pull-up explicitly.
    // GPIO0 is a strapping pin; its pull state after boot is not guaranteed,
    // so digitalRead() without pinMode() can latch HIGH permanently.
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

    Serial.begin(115200);
    delay(200);  // wait for CDC enumeration
    Serial.println("=== SETUP ===");
    Serial.flush();

    Wire.begin(I2C_SHARED_SDA, I2C_SHARED_SCL);
    Wire.setClock(400000);
    i2c_scan();

    init_qmi8658();

    display_init();

    // Skip color splash - may block SPI
    tft.fillScreen(TFT_BLACK);
    Serial.println("[DISPLAY] init done");
    Serial.flush();

    // Initialize NVS settings persistence
    nvs_init();

    // Initialize power management
    power_init();

    // LVGL draw buffers — factory style (MALLOC_CAP_SPIRAM, 1/4 screen each)
    // PSRAM: use PSRAM if available, else DRAM
    // Note: DRAM is ~328KB total, need to fit 2 buffers + LVGL heap + app heap
    size_t buf_size = (TFT_WIDTH * TFT_HEIGHT / 4) * sizeof(lv_color_t);  // 1/4 screen = ~37.5KB
    buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    buf2 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    Serial.printf("[LVGL] buf1=%p buf2=%p (PSRAM buffers, %d bytes each)\n",
                  (void*)buf1, (void*)buf2, (int)buf_size);
    if (!buf1) { free(buf2); buf2 = nullptr; }
    if (!buf1 || !buf2) {
        Serial.println("[LVGL] PSRAM alloc failed, using DRAM");
        free(buf1); free(buf2);
        buf1 = (lv_color_t*)malloc(buf_size);
        buf2 = (lv_color_t*)malloc(buf_size);
        Serial.printf("[LVGL] DRAM buf1=%p buf2=%p\n", (void*)buf1, (void*)buf2);
    }
    if (!buf1 || !buf2) {
        Serial.println("[LVGL] FATAL: buffer alloc failed");
        while(1) delay(1000);
    }

    // LVGL init
    g_lvgl_mux = xSemaphoreCreateRecursiveMutex();
    lv_init();
    size_t buf_pixels = TFT_WIDTH * TFT_HEIGHT / 4;  // 1/4 screen in pixels
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_pixels);

    // Display driver
    lv_disp_drv_t dd; lv_disp_drv_init(&dd);
    dd.hor_res = TFT_WIDTH; dd.ver_res = TFT_HEIGHT;
    dd.flush_cb = flush_cb; dd.draw_buf = &draw_buf;
    dd.full_refresh = 0;  // normal rendering
    lv_disp_t* disp = lv_disp_drv_register(&dd);

    // Touch driver (placeholder — no CST816 on this board)
    // NOTE: LVGL indev timer calls read_cb even if no touch hardware.
    // For minimal test without input device, skip indev registration entirely.
    // The read_cb lambda was causing LoadProhibited crashes.
    // Leaving this section empty until we confirm touch hardware.
    // lv_indev_drv_t id; lv_indev_drv_init(&id);
    // id.type = LV_INDEV_TYPE_POINTER;
    // id.disp = disp;
    // id.read_cb = [](lv_indev_drv_t* d, lv_indev_data_t* data) {
    //     data->state = LV_INDEV_STATE_RELEASED;
    // };
    // lv_indev_drv_register(&id);
    (void)disp;  // suppress unused variable warning

    // LVGL tick timer (factory style: esp_timer every 2ms)
    // Do NOT use LV_TICK_CUSTOM=1 — factory uses esp_timer
    const esp_timer_create_args_t timer_args = {
        .callback = &lvgl_tick_cb,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t tick_timer = nullptr;
    esp_timer_create(&timer_args, &tick_timer);
    esp_timer_start_periodic(tick_timer, 2000);  // 2ms = 2000us

    Serial.println("[LVGL] tick timer started (esp_timer 2ms)");
    Serial.flush();

    // ─── LVGL UI (single-button navigation) ────────────────────────────────────
    ui_main_init();

    // Initialize camera AFTER LVGL is set up
    if (!camera_init()) {
        Serial.println("[MAIN] Camera init FAILED");
    } else {
        Serial.println("[MAIN] Camera init OK");
    }

    Serial.println("=== READY ===");
    g_ui_ready = true;

    // Start LVGL task on Core 1 (matching factory bsp_lv_port_run)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);

    Serial.flush();
}

void loop() {
    // Poll BOOT button (active LOW, pull-up so default HIGH)
    static bool     g_btn_was_pressed = false;
    static uint32_t g_btn_press_ms   = 0;

    bool cur = digitalRead(PIN_BOOT_BTN);

    if (!cur && !g_btn_was_pressed) {
        // Pressed — record time
        g_btn_was_pressed = true;
        g_btn_press_ms = millis();
        power_update_idle_time();  // wake on button press
    } else if (cur && g_btn_was_pressed) {
        // Released — determine event type
        uint32_t held = millis() - g_btn_press_ms;
        g_btn_was_pressed = false;

        if (held < 300) {
            ui_nav_event(NAV_NEXT);
        } else {
            ui_nav_event(NAV_CONFIRM);
        }
    } else if (!cur && g_btn_was_pressed) {
        // Held — check for long-hold (> 2000ms)
        if ((millis() - g_btn_press_ms) > 2000) {
            ui_nav_event(NAV_HOME);
            // Reset so we don't fire HOME again while still holding
            g_btn_was_pressed = false;
        }
    }

    // Power management: dim/off screen after idle
    power_sleep_if_idle();

    delay(10);
}
