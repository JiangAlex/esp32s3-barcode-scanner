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
#include "input/touch.h"
#include "network/wifi_portal/wifi_portal.h"
#include "network/wifi_manager.h"   // C-style status helpers (wifi_is_connected, etc.)
#include "network/mqtt_client.h"
#include "network/ble_hid.h"
#include "storage/sd_card.h"
#include "storage/scan_log.h"

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

// ─── LVGL task (factory style: Core 1 + recursive mutex) ────────────────────
// The LVGL display driver, draw buffers, touch indev, flush callback and the
// esp_timer tick all live in display.cpp (display_init), which is the single
// source of truth. main.cpp only drives the LVGL handler loop below.

static void lvgl_task(void* param) {
    Serial.println("[LVGL] task started on Core 1");
    while (1) {
        if (lvgl_lock(1000)) {
            if (g_ui_update_pending) {
                g_ui_update_pending = false;
                update_btn_ui();
            }
            // Drain any barcode decoded by the Core 0 preview task (safe here:
            // this runs on Core 1 with the LVGL lock held).
            ui_process_pending_scan();
            lv_timer_handler();
            lvgl_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─── MQTT query response → UI ────────────────────────────────────────────────
// Called from the MQTT task context when a warehouse/response arrives. Formats
// the item fields and shows them on the scan result page. Kept defensive: any
// missing field falls back to a placeholder.
static void mqtt_on_response(const char* status, JsonDocument& doc) {
    // Server query contract (see barcode-warehouse-server/src/mqtt_handler.py):
    //   found     → {"status":"found","data":{barcode,name,spec,quantity,location,supplier,date_in}}
    //   not_found → {"status":"not_found","barcode":...,"message":...}
    //   error     → {"status":"error","message":...}
    // Stock / stocktake acks carry their result inside "data" as well and are
    // dispatched to the UI state machine (see below).
    if (status && strcmp(status, "found") == 0) {
        JsonObjectConst data = doc["data"];
        const char* barcode  = data["barcode"]  | "";
        const char* name     = data["name"]     | "?";
        const char* spec     = data["spec"]     | "";
        int         qty      = data["quantity"] | 0;
        const char* location = data["location"] | "";
        const char* supplier = data["supplier"] | "";
        const char* date_in  = data["date_in"]  | "";
        // Post the structured item; the LVGL task switches to the RESULT page
        // and populates the 6 fields (see ui_process_pending_scan).
        ui_post_result_item(barcode, name, spec, qty, location, supplier, date_in);
        return;
    }

    if (status && strcmp(status, "not_found") == 0) {
        ui_post_result("Query", "Not found");
        return;
    }

    // Stock-in / stock-out acks: show the resulting quantity.
    if (status && (strcmp(status, "stock_in_ok") == 0 ||
                   strcmp(status, "stock_out_ok") == 0)) {
        JsonObjectConst data = doc["data"];
        const char* name = data["name"]     | "?";
        int         qty  = data["quantity"] | 0;
        bool        in   = strcmp(status, "stock_in_ok") == 0;
        char line[128];
        snprintf(line, sizeof(line), "%s qty=%d", name, qty);
        ui_post_result(in ? "Stock-in" : "Stock-out", line);
        return;
    }

    // Stocktake session lifecycle → drive the firmware stocktake state machine.
    if (status && strcmp(status, "stocktake_started") == 0) {
        int session_id = doc["data"]["session_id"] | 0;
        ui_stocktake_on_started(session_id);
        return;
    }
    if (status && strcmp(status, "stocktake_scanned") == 0) {
        JsonObjectConst data = doc["data"];
        const char* name = data["name"]       | "?";
        int sys_qty      = data["system_qty"] | 0;
        int act_qty      = data["actual_qty"] | 0;
        int diff         = data["difference"] | 0;
        ui_stocktake_on_scanned(name, sys_qty, act_qty, diff);
        return;
    }
    if (status && strcmp(status, "stocktake_ended") == 0) {
        ui_stocktake_on_ended();
        return;
    }

    // Fallback: surface whatever status / message the server sent.
    const char* msg = doc["message"] | (status ? status : "error");
    ui_post_result("Server", msg);
}

// ─── Captive-portal WiFi manager ─────────────────────────────────────────────
// Ported from the WT02 project: on boot it loads saved SSID/password from
// EEPROM and auto-connects; if there is no saved config (or it fails) it opens
// an AP + web portal ("SoftSnail-Scanner-Setup") so the user can configure WiFi
// from a phone — no need to hardcode credentials or reflash.
static WiFiManager g_wifi;

static void on_wifi_connected() {
    Serial.printf("[WIFI] connected, IP=%s — connecting MQTT\n", g_wifi.getIP().c_str());

    // Sync the system clock via NTP so the work log uses real dates/times.
    // Without this, time(nullptr) sits near the 1970 epoch and every log entry
    // (and the YYYY-MM-DD.json filename) would be wrong. TZ = Taiwan (CST-8,
    // no DST). configTzTime applies the POSIX TZ string and starts SNTP.
    configTzTime("CST-8", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    Serial.println("[TIME] NTP sync requested (TZ=CST-8)");

    mqtt_reconnect();
    ui_update_status(true, mqtt_is_connected());
}

// ─── QMI8658 IMU ────────────────────────────────────────────────────────────

static void init_qmi8658(void) {
    uint8_t id = 0;
    if (i2c_reg_read(IMU_I2C_ADDR, 0x00, &id, 1)) {   // WHO_AM_I reg 0x00 → 0x05
        Serial.printf("[QMI8658] ID=0x%02X %s\n", id, id == 0x05 ? "OK" : "(expected 0x05)");
    } else {
        Serial.println("[QMI8658] read failed");
    }
}

// ─── I2C scan ───────────────────────────────────────────────────────────────

static void i2c_scan(void) {
    Serial.println("[I2C] scan:");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission(true) == 0) { Serial.printf("  0x%02X\n", a); found++; }
    }
    Serial.printf("[I2C] %d device(s)\n", found);
}

// ─── Setup ──────────────────────────────────────────────────────────────────

void setup() {
    // NOTE: do NOT drive GPIO48 here — it is the shared I2C SDA line
    // (I2C_SHARED_SDA=48). An earlier "CPU alive" blink set GPIO48 as a
    // push-pull OUTPUT and left it HIGH, corrupting the I2C bus: scans
    // returned phantom addresses and register reads failed (QMI8658 ID read
    // back 0x00 instead of 0x05, CST816 @0x15 never appeared). Leave the
    // I2C pins alone and let Wire.begin() configure them.

    // BOOT button (GPIO0, active LOW) — must enable internal pull-up explicitly.
    // GPIO0 is a strapping pin; its pull state after boot is not guaranteed,
    // so digitalRead() without pinMode() can latch HIGH permanently.
    pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

    Serial.begin(115200);
    delay(200);  // wait for CDC enumeration
    Serial.println("=== SETUP ===");
    Serial.flush();

    // Enable internal pull-ups on SDA/SCL before Wire.begin as a safety net.
    // If the board's external I2C pull-ups are weak/absent, lines float and
    // register reads come back 0x00/0xFF while address scans still ACK — which
    // matches the QMI8658 ID=0x00 symptom. ESP32 internal pull-ups are weak
    // (~45k) but often enough to recover reads at 100 kHz.
    pinMode(I2C_SHARED_SDA, INPUT_PULLUP);
    pinMode(I2C_SHARED_SCL, INPUT_PULLUP);
    Wire.begin(I2C_SHARED_SDA, I2C_SHARED_SCL);
    Wire.setClock(I2C_FREQ);
    i2c_scan();

    init_qmi8658();

    display_init();

    // Skip color splash - may block SPI
    tft.fillScreen(TFT_BLACK);
    Serial.println("[DISPLAY] init done");
    Serial.flush();

    // Initialize NVS settings persistence
    nvs_init();

    // Mount the SD card (shares the FSPI bus with the LCD; must come after
    // display_init so LovyanGFX owns/configures the shared bus first) and
    // initialize the work-log subsystem. If no card is present these fail
    // gracefully and logging is simply disabled.
    if (sd_card_init()) {
        Serial.printf("[MAIN] SD mounted: %u/%u MB used\n",
                      sd_card_used_mb(), sd_card_total_mb());
        scan_log_init();
    } else {
        Serial.println("[MAIN] SD not available — work log disabled");
    }

    // Initialize power management
    power_init();

    // LVGL (init, draw buffers, display driver, touch indev, and the esp_timer
    // tick) is fully set up inside display_init() above — it is the single
    // source of truth. We only create the recursive lock used by lvgl_task()
    // to serialize LVGL calls from Core 1.
    g_lvgl_mux = xSemaphoreCreateRecursiveMutex();

    // ─── LVGL UI (single-button navigation) ────────────────────────────────────
    ui_main_init();

    // Initialize camera AFTER LVGL is set up
    if (!camera_init()) {
        Serial.println("[MAIN] Camera init FAILED");
    } else {
        Serial.println("[MAIN] Camera init OK");
    }

    // ── Network: captive-portal WiFi + MQTT ─────────────────────────────────
    // g_wifi.begin() auto-connects from saved EEPROM config, or opens the
    // "SoftSnail-Scanner-Setup" AP + web portal for the user to set WiFi AND
    // the MQTT broker. We read the saved broker before initializing MQTT so a
    // portal-configured broker overrides the config.h default.
    mqtt_set_response_callback(mqtt_on_response);
    g_wifi.onConnected = on_wifi_connected;
    g_wifi.begin();
    {
        String b = g_wifi.getMqttBroker();
        uint16_t p = g_wifi.getMqttPort();
        if (b.length() > 0 || p != 0) mqtt_set_broker(b.c_str(), p);
    }
    mqtt_client_init();
    ui_update_status(wifi_is_connected(), mqtt_is_connected());

    // When BLE HID (INPUT mode) stops, it has fully released the WiFi driver to
    // free DRAM; re-establish WiFi + MQTT here.
    ble_hid_set_stopped_cb([]() {
        Serial.println("[BLE] stopped — re-establishing WiFi");
        // ble_hid_begin() released WiFi via WiFi.mode(WIFI_OFF) (Arduino class
        // stays in sync). Re-init the STA driver then reconnect.
        WiFi.mode(WIFI_STA);
        delay(100);
        g_wifi.begin();
        mqtt_client_init();
    });

    // Settings page "WiFi" row opens the web config portal on-demand. All
    // detailed config (SSID/password/MQTT broker) is entered in the portal.
    // startConfigPortal() only brings up the AP + web server; g_wifi.loop()
    // (driven in loop()) services it.
    ui_set_wifi_portal_cb([]() {
        Serial.println("[WIFI] opening config portal (from settings)");
        g_wifi.startConfigPortal();
    });

    Serial.println("=== READY ===");
    g_ui_ready = true;

    // Start LVGL task on Core 1 (matching factory bsp_lv_port_run)
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 5, NULL, 1);

    Serial.flush();
}

void loop() {
    // ── Serial console ───────────────────────────────────────────────────
    // Focus calibration AND a test hook to inject a barcode as if scanned:
    //   '+' / '='              : focus nearer (VCM += 10)   [single char]
    //   '-'                    : focus farther (VCM -= 10)  [single char]
    //   'p'                    : print current VCM          [single char]
    //   digits + Enter         : set VCM absolute (e.g. "300")
    //   any other text + Enter : treat as a scanned barcode (e.g. "RAC-28VP")
    // Single-char focus keys act immediately; everything else is line-buffered
    // until Enter so barcodes containing '-'/digits aren't eaten by the focus
    // commands.
    {
        static char s_line[160];
        static size_t s_len = 0;
        while (Serial.available() > 0) {
            char c = (char)Serial.read();

            if (c == '\n' || c == '\r') {
                if (s_len == 0) continue;        // ignore blank line
                s_line[s_len] = '\0';
                // Classify the completed line.
                bool all_digits = true;
                for (size_t i = 0; i < s_len; i++) {
                    if (s_line[i] < '0' || s_line[i] > '9') { all_digits = false; break; }
                }
                if (all_digits) {
                    int v = atoi(s_line);
                    if (v > 1023) v = 1023;
                    camera_af_set_manual((uint16_t)v);
                    Serial.printf("[FOCUS] VCM=%u (set)\n", (unsigned)v);
                } else {
                    // Treat as a barcode: inject into the scan pipeline.
                    Serial.printf("[SERIAL] inject barcode: %s\n", s_line);
                    ui_inject_scan("SERIAL", s_line);
                }
                s_len = 0;
            } else if (s_len == 0 && (c == '+' || c == '=' || c == '-' || c == 'p')) {
                // Single-char focus command (only when no line is in progress).
                if (c == 'p') {
                    Serial.printf("[FOCUS] current VCM=%u\n", camera_af_get_manual());
                } else if (c == '-') {
                    uint16_t v = camera_af_get_manual();
                    v = (v < 10) ? 0 : v - 10;
                    camera_af_set_manual(v);
                    Serial.printf("[FOCUS] VCM=%u (farther)\n", v);
                } else {  // '+' or '='
                    uint16_t v = camera_af_get_manual();
                    v = (v > 1013) ? 1023 : v + 10;
                    camera_af_set_manual(v);
                    Serial.printf("[FOCUS] VCM=%u (nearer)\n", v);
                }
            } else if (s_len == 0 && (c == 'n' || c == 'c' || c == 'h')) {
                // Single-char navigation command (emulates the BOOT key over
                // serial, for testing without physical access):
                //   n = NAV_NEXT (short press), c = NAV_CONFIRM (long press),
                //   h = NAV_HOME (long hold).
                if (c == 'n')      { Serial.println("[SERIAL] NAV_NEXT");    ui_nav_event(NAV_NEXT); }
                else if (c == 'c') { Serial.println("[SERIAL] NAV_CONFIRM"); ui_nav_event(NAV_CONFIRM); }
                else               { Serial.println("[SERIAL] NAV_HOME");    ui_nav_event(NAV_HOME); }
            } else if (s_len == 0 && (c == 'b' || c == 'B' || c == 't' || c == 's')) {
                // BLE HID PoC commands (testing):
                //   b = begin/advertise, B = stop, t = type a test string,
                //   s = status + free heap.
                if (c == 'b')      { ble_hid_begin(); }
                else if (c == 'B') { ble_hid_stop(); }
                else if (c == 't') {
                    Serial.printf("[SERIAL] BLE type test (connected=%d)\n",
                                  ble_hid_is_connected());
                    ble_hid_type_line("SoftSnail-BLE-TEST-123");
                } else {  // 's'
                    Serial.printf("[SERIAL] BLE started=%d connected=%d | heap total=%u internal=%u psram=%u\n",
                                  ble_hid_is_started(), ble_hid_is_connected(),
                                  (unsigned)ESP.getFreeHeap(),
                                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
                }
            } else if (s_len == 0 && c == 'd') {
                // Dump today's work log from SD (diagnostic).
                String js = scan_log_read(nullptr, 0, 100);
                Serial.printf("[SERIAL] today's log: %s\n", js.c_str());
            } else {
                // Accumulate into the line buffer (barcode or multi-digit VCM).
                if (s_len < sizeof(s_line) - 1) {
                    s_line[s_len++] = c;
                }
            }
        }
    }

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

    // Keep the screen fully awake while scanning — the SCAN page is an active
    // task even without button input, so reset the idle timer each loop there.
    if (ui_get_current_page() == UI_PAGE_SCAN) {
        power_update_idle_time();
    }

    // ── Network keep-alive ──────────────────────────────────────────────────
    g_wifi.loop();        // captive portal requests + connection state machine
    mqtt_client_loop();   // MQTT keep-alive + auto-reconnect (internally rate-limited)

    // Refresh the status-bar icons periodically (every 3 s).
    {
        static uint32_t s_net_last = 0;
        if (millis() - s_net_last >= 3000) {
            s_net_last = millis();
            ui_update_status(wifi_is_connected(), mqtt_is_connected());
        }
    }

    // Power management: dim/off screen after idle
    power_sleep_if_idle();

    delay(10);
}
