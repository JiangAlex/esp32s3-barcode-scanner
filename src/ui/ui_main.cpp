/**
 * @file ui_main.cpp
 * @brief LVGL multi-page UI — single-button (BOOT key) navigation implementation
 *
 * Pages:
 *   HOME        — 3-item vertical menu (Scan / Settings / About)
 *   SCAN        — Scan status display
 *   SETTINGS    — Brightness slider
 *   INVENTORY   — Batch scan list
 *   PHOTO       — Camera capture result
 */

#include "ui_main.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "config/config.h"
#include "scan_preview.h"
#include "display/display.h"
#include "network/mqtt_client.h"
#include "network/wifi_manager.h"
#include "network/ble_hid.h"
#include "storage/scan_log.h"

// Traditional-Chinese CJK font (16px, bpp2) — generated subset
// (Big5 level-1 ~5401 chars + ASCII). Defined in src/ui/fonts/lv_font_cjk_tc_16.c
LV_FONT_DECLARE(lv_font_cjk_tc_16);

// ─── Screen dimensions ────────────────────────────────────────────────────────

#define SCR_W  240
#define SCR_H  320
#define STATUS_H  24   // status bar height
#define CONTENT_Y (STATUS_H + 10)
#define CONTENT_H (SCR_H - STATUS_H - 10)
#define ITEM_H    50   // height of each menu button
#define ITEM_GAP  8    // gap between items

// ─── Color constants (RGB565) ─────────────────────────────────────────────────

static const lv_color_t COL_BG       = { .full = 0x0000 };  // black background
static const lv_color_t COL_PANEL    = { .full = 0x0C21 };  // dark panel (#0C21 = ~RGB(12,33,49))
static const lv_color_t COL_FOCUS    = { .full = 0x04A1 };  // focus highlight blue
static const lv_color_t COL_NORMAL   = { .full = 0x18E3 };  // normal button
static const lv_color_t COL_TEXT     = { .full = 0xFFFF };  // white text
static const lv_color_t COL_STATUS  = { .full = 0x7BEF };  // status bar bg
static const lv_color_t COL_SLIDER  = { .full = 0x04A1 };  // slider color

// ─── LVGL Animations ──────────────────────────────────────────────────────────

#define ANIM_SPEED_NORMAL  200   // ms for page slide
#define ANIM_SPEED_FAST    100   // ms for button press

// Slide-in animation callback (from right, x: SCR_W → 0)
static void anim_slide_in_cb(void* obj, int32_t v) {
    lv_obj_set_x((lv_obj_t*)obj, v);
}

// Button press animation: fade out/in using opa
static void anim_btn_fade_cb(void* obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

// Run a page slide-in animation (from right edge)
// LVGL 8.x: assign struct members directly after lv_anim_init
static void anim_page_slide_in(lv_obj_t* page) {
    lv_anim_t a;
    lv_anim_init(&a);
    a.var = page;
    a.start_value = SCR_W;
    a.end_value = 0;
    a.time = ANIM_SPEED_NORMAL;
    a.exec_cb = (lv_anim_exec_xcb_t)anim_slide_in_cb;
    a.path_cb = lv_anim_path_overshoot;
    lv_anim_start(&a);
}

// Run a button press animation (briefly fade the button)
static void anim_button_press(lv_obj_t* btn) {
    lv_anim_t a;
    lv_anim_init(&a);
    a.var = btn;
    a.start_value = LV_OPA_COVER;
    a.end_value = LV_OPA_30;
    a.time = ANIM_SPEED_FAST;
    a.exec_cb = (lv_anim_exec_xcb_t)anim_btn_fade_cb;
    a.path_cb = lv_anim_path_ease_in;
    lv_anim_start(&a);

    lv_anim_t a2;
    lv_anim_init(&a2);
    a2.var = btn;
    a2.start_value = LV_OPA_30;
    a2.end_value = LV_OPA_COVER;
    a2.time = ANIM_SPEED_FAST;
    a2.exec_cb = (lv_anim_exec_xcb_t)anim_btn_fade_cb;
    a2.path_cb = lv_anim_path_ease_out;
    a2.playback_delay = ANIM_SPEED_FAST;
    lv_anim_start(&a2);
}

// ─── Page containers ─────────────────────────────────────────────────────────

static lv_obj_t* g_status_bar = nullptr;
static lv_obj_t* g_page_home = nullptr;
static lv_obj_t* g_page_scan = nullptr;
static lv_obj_t* g_page_result = nullptr;
static lv_obj_t* g_page_settings = nullptr;
static lv_obj_t* g_page_inventory = nullptr;
static lv_obj_t* g_page_inventory_list = nullptr;
static lv_obj_t* g_page_photo = nullptr;
static lv_obj_t* g_page_log = nullptr;
static lv_obj_t* g_page_about = nullptr;

static ui_page_t g_current_page = UI_PAGE_HOME;

// ─── Status bar icons ─────────────────────────────────────────────────────────

static lv_obj_t* g_icon_wifi = nullptr;
static lv_obj_t* g_icon_mqtt = nullptr;
static lv_obj_t* g_icon_ble  = nullptr;

// ─── HOME page menu items ─────────────────────────────────────────────────────

typedef enum {
    HOME_ITEM_SCAN = 0,
    HOME_ITEM_INVENTORY,
    HOME_ITEM_SETTINGS,
    HOME_ITEM_ABOUT,
    HOME_ITEM_COUNT
} home_item_t;

static menu_item_t g_home_items[HOME_ITEM_COUNT];

// ─── SCAN page widgets ────────────────────────────────────────────────────────

static lv_obj_t* g_scan_lbl_status = nullptr;
static lv_obj_t* g_scan_lbl_barcode = nullptr;
static lv_obj_t* g_scan_lbl_info = nullptr;
static lv_obj_t* g_scan_lbl_mode = nullptr;
// Stocktake feedback line, placed BELOW the camera viewfinder (screen y=85..235)
// so the live preview (drawn via LovyanGFX) does not paint over it.
static lv_obj_t* g_scan_lbl_stocktake = nullptr;

// ─── Scan mode state ─────────────────────────────────────────────────────────

static scan_mode_t g_scan_mode = SCAN_MODE_QUERY;

// ─── Cross-core pending scan (Core 0 decode → Core 1 LVGL) ────────────────────
// The preview capture task runs on Core 0 and cannot touch LVGL. It writes the
// decoded result here under a mutex; the LVGL task (Core 1) drains it via
// ui_process_pending_scan().

static portMUX_TYPE g_scan_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_scan_pending = false;
static char g_pending_type[16]     = {0};
static char g_pending_content[128] = {0};

// Callback invoked from the Core 0 preview task on a successful decode.
// Keep it minimal: copy strings into the pending buffer, set the flag.
static void on_preview_decode(const char* type_name, const char* content) {
    portENTER_CRITICAL(&g_scan_mux);
    strncpy(g_pending_type, type_name ? type_name : "",
            sizeof(g_pending_type) - 1);
    g_pending_type[sizeof(g_pending_type) - 1] = '\0';
    strncpy(g_pending_content, content ? content : "",
            sizeof(g_pending_content) - 1);
    g_pending_content[sizeof(g_pending_content) - 1] = '\0';
    g_scan_pending = true;
    portEXIT_CRITICAL(&g_scan_mux);
}

// Public: inject a scan result from outside the preview task (e.g. a barcode
// typed over the serial console for testing). Takes the exact same cross-task
// path as a real camera decode, so QUERY/STOCK/STOCKTAKE dispatch is identical.
void ui_inject_scan(const char* type_name, const char* content) {
    on_preview_decode(type_name ? type_name : "SERIAL", content);
}

// ─── Cross-core pending RESULT (MQTT callback context → Core 1 LVGL) ──────────
// mqtt_on_response() runs in the MQTT/loop() context, NOT the LVGL task, so it
// must not touch LVGL widgets directly. It posts the formatted result here via
// ui_post_result(); the LVGL task drains it in ui_process_pending_scan().
static portMUX_TYPE g_result_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_result_pending = false;
static char g_result_label[24]    = {0};
static char g_result_content[128] = {0};

void ui_post_result(const char* label, const char* content) {
    portENTER_CRITICAL(&g_result_mux);
    strncpy(g_result_label, label ? label : "",
            sizeof(g_result_label) - 1);
    g_result_label[sizeof(g_result_label) - 1] = '\0';
    strncpy(g_result_content, content ? content : "",
            sizeof(g_result_content) - 1);
    g_result_content[sizeof(g_result_content) - 1] = '\0';
    g_result_pending = true;
    portEXIT_CRITICAL(&g_result_mux);
}

// ─── Cross-core structured query-result item (MQTT context → Core 1 LVGL) ─────
// mqtt_on_response() (MQTT/loop context) posts a structured item here; the LVGL
// task drains it in ui_process_pending_scan(), switches to the RESULT page and
// populates the 6 fields. Mirrors the ui_post_result pattern.
static portMUX_TYPE g_item_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_item_pending = false;
static char g_item_barcode[128]  = {0};
static char g_item_name[64]      = {0};
static char g_item_spec[64]      = {0};
static char g_item_location[48]  = {0};
static char g_item_supplier[64]  = {0};
static char g_item_date[24]      = {0};
static int  g_item_qty           = 0;

static void item_copy(char* dst, size_t cap, const char* src) {
    strncpy(dst, src ? src : "", cap - 1);
    dst[cap - 1] = '\0';
}

void ui_post_result_item(const char* barcode, const char* name,
                         const char* spec, int quantity,
                         const char* location, const char* supplier,
                         const char* date_in) {
    portENTER_CRITICAL(&g_item_mux);
    item_copy(g_item_barcode,  sizeof(g_item_barcode),  barcode);
    item_copy(g_item_name,     sizeof(g_item_name),     name);
    item_copy(g_item_spec,     sizeof(g_item_spec),     spec);
    item_copy(g_item_location, sizeof(g_item_location), location);
    item_copy(g_item_supplier, sizeof(g_item_supplier), supplier);
    item_copy(g_item_date,     sizeof(g_item_date),     date_in);
    g_item_qty = quantity;
    g_item_pending = true;
    portEXIT_CRITICAL(&g_item_mux);
}

// ─── Stocktake session state (P3, Redmine #79) ─────────────────────────────
// The firmware keeps a per-barcode running count (actual_qty) for the current
// session. Each physical scan of a barcode increments its count by 1 and the
// updated actual_qty is published to the server via mqtt_stocktake_scan().

#define STOCKTAKE_MAX 50

typedef struct {
    char    barcode[128];
    int     actual_qty;   // accumulated count for this barcode this session
} stocktake_entry_t;

static stocktake_entry_t g_st_entries[STOCKTAKE_MAX];
static uint8_t g_st_entry_count = 0;      // distinct barcodes counted
static int     g_st_session_id  = 0;      // 0 = no session
static bool    g_st_active      = false;  // true once server assigns session_id
static bool    g_st_pending_start = false; // start requested, awaiting ack

// ─── INVENTORY page widgets ───────────────────────────────────────────────────

static lv_obj_t* g_inv_lbl_count = nullptr;
static lv_obj_t* g_inv_lbl_status = nullptr;

// ─── PHOTO page widgets ───────────────────────────────────────────────────────

static lv_obj_t* g_photo_lbl_status = nullptr;
static lv_obj_t* g_photo_lbl_result = nullptr;

// ─── RESULT page widgets ──────────────────────────────────────────────────────
// 6-field query result detail + two action buttons (rescan / home).
static lv_obj_t* g_result_lbl_barcode = nullptr;
static lv_obj_t* g_result_val_name = nullptr;
static lv_obj_t* g_result_val_spec = nullptr;
static lv_obj_t* g_result_val_qty = nullptr;
static lv_obj_t* g_result_val_location = nullptr;
static lv_obj_t* g_result_val_supplier = nullptr;
static lv_obj_t* g_result_val_date = nullptr;
static lv_obj_t* g_result_btn_rescan = nullptr;
static lv_obj_t* g_result_btn_home = nullptr;
static int8_t    g_result_focus = 0;   // 0=rescan, 1=home

// ─── INVENTORY LIST page widgets ──────────────────────────────────────────────
static lv_obj_t* g_invlist_lbl_count = nullptr;
static lv_obj_t* g_invlist_container = nullptr;   // scrollable list body
static lv_obj_t* g_invlist_btn_back = nullptr;
static lv_obj_t* g_invlist_btn_upload = nullptr;
static int8_t    g_invlist_focus = 0;   // 0=back, 1=upload

// ─── LOG page widgets ─────────────────────────────────────────────────────────
static lv_obj_t* g_log_lbl_date = nullptr;
static lv_obj_t* g_log_body = nullptr;    // scrollable entries container
static int       g_log_day_offset = 0;   // 0 = today, negative = past days

// ─── SETTINGS page widgets ───────────────────────────────────────────────────
// A focusable vertical list: WiFi / MQTT / BLE (read-only status) + Brightness
// slider + Volume (stub) + Language + Work Log entry. g_settings_focus selects
// the active row; NAV_NEXT cycles, NAV_CONFIRM acts on the focused row.
#define SETTINGS_ROW_COUNT 7
typedef enum {
    SET_ROW_WIFI = 0,
    SET_ROW_MQTT,
    SET_ROW_BLE,
    SET_ROW_BRIGHTNESS,
    SET_ROW_VOLUME,
    SET_ROW_LANGUAGE,
    SET_ROW_LOG
} settings_row_t;

static lv_obj_t* g_settings_list = nullptr;        // scrollable list container
static lv_obj_t* g_settings_rows[SETTINGS_ROW_COUNT] = {nullptr};
static lv_obj_t* g_settings_vals[SETTINGS_ROW_COUNT] = {nullptr};
static lv_obj_t* g_settings_slider = nullptr;       // brightness slider (in its row)
static lv_obj_t* g_settings_lbl_brightness = nullptr;
static int8_t    g_settings_focus = 0;
static bool      g_settings_lang_zh = true;         // true=繁中, false=English
static ui_wifi_portal_cb_t g_wifi_portal_cb = nullptr;

void ui_set_wifi_portal_cb(ui_wifi_portal_cb_t cb) { g_wifi_portal_cb = cb; }

// ─── Navigation state ─────────────────────────────────────────────────────────

static int8_t g_home_focus = 0;   // focused item index on HOME
static bool g_at_home = true;      // at HOME page vs sub-page

// ─── Focus ring color update ──────────────────────────────────────────────────

// ─── Forward declarations ────────────────────────────────────────────────────

static void show_page(ui_page_t page);
static void update_home_focus(void);
static void on_home_select(int8_t idx);
static void create_home_page(void);
static void create_scan_page(void);
static void create_settings_page(void);
static void create_inventory_page(void);
static void create_photo_page(void);
static void create_result_page(void);
static void create_inventory_list_page(void);
static void create_log_page(void);
static void create_about_page(void);
static void settings_refresh(void);
static void update_settings_focus(void);
static void log_render(void);
static void log_update_date_label(void);
static void set_item_focused(menu_item_t* item, bool focused);
static void update_status_bar(void);

// ─── Public: init ─────────────────────────────────────────────────────────────

void ui_main_init(void) {
    // Black background
    lv_obj_set_style_bg_color(lv_scr_act(), COL_BG, 0);

    // Status bar
    g_status_bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_status_bar, SCR_W, STATUS_H);
    lv_obj_set_pos(g_status_bar, 0, 0);
    lv_obj_set_style_bg_color(g_status_bar, COL_STATUS, 0);
    lv_obj_set_style_radius(g_status_bar, 0, 0);
    lv_obj_set_scrollbar_mode(g_status_bar, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t* lbl_title = lv_label_create(g_status_bar);
    lv_label_set_text(lbl_title, BOARD_NAME);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 8, 0);

    // WiFi icon (right side, before MQTT icon)
    g_icon_wifi = lv_label_create(g_status_bar);
    lv_label_set_text(g_icon_wifi, "[W]");
    lv_obj_set_style_text_color(g_icon_wifi, (lv_color_t){ .full = 0xF800 }, 0);  // red = disconnected
    lv_obj_set_style_text_font(g_icon_wifi, &lv_font_montserrat_12, 0);
    lv_obj_align(g_icon_wifi, LV_ALIGN_RIGHT_MID, -80, 0);

    // MQTT icon
    g_icon_mqtt = lv_label_create(g_status_bar);
    lv_label_set_text(g_icon_mqtt, "[M]");
    lv_obj_set_style_text_color(g_icon_mqtt, (lv_color_t){ .full = 0xF800 }, 0);  // red = disconnected
    lv_obj_set_style_text_font(g_icon_mqtt, &lv_font_montserrat_12, 0);
    lv_obj_align(g_icon_mqtt, LV_ALIGN_RIGHT_MID, -52, 0);

    // BLE icon — grey=off, red=advertising/waiting, green=connected
    g_icon_ble = lv_label_create(g_status_bar);
    lv_label_set_text(g_icon_ble, "[B]");
    lv_obj_set_style_text_color(g_icon_ble, (lv_color_t){ .full = 0x8410 }, 0);   // grey = off
    lv_obj_set_style_text_font(g_icon_ble, &lv_font_montserrat_12, 0);
    lv_obj_align(g_icon_ble, LV_ALIGN_RIGHT_MID, -24, 0);

    // Pages
    create_home_page();
    create_scan_page();
    create_result_page();
    create_settings_page();
    create_inventory_page();
    create_inventory_list_page();
    create_photo_page();
    create_log_page();
    create_about_page();

    // Start at home
    show_page(UI_PAGE_HOME);

    Serial.println("[UI] initialized: HOME / SCAN / RESULT / SETTINGS / INVENTORY / INV-LIST / PHOTO / LOG / ABOUT");
}

// ─── Show/hide page ───────────────────────────────────────────────────────────

static void hide_all_pages(void) {
    if (g_page_home)            lv_obj_add_flag(g_page_home, LV_OBJ_FLAG_HIDDEN);
    if (g_page_scan)            lv_obj_add_flag(g_page_scan, LV_OBJ_FLAG_HIDDEN);
    if (g_page_result)         lv_obj_add_flag(g_page_result, LV_OBJ_FLAG_HIDDEN);
    if (g_page_settings)       lv_obj_add_flag(g_page_settings, LV_OBJ_FLAG_HIDDEN);
    if (g_page_inventory)      lv_obj_add_flag(g_page_inventory, LV_OBJ_FLAG_HIDDEN);
    if (g_page_inventory_list) lv_obj_add_flag(g_page_inventory_list, LV_OBJ_FLAG_HIDDEN);
    if (g_page_photo)          lv_obj_add_flag(g_page_photo, LV_OBJ_FLAG_HIDDEN);
    if (g_page_log)            lv_obj_add_flag(g_page_log, LV_OBJ_FLAG_HIDDEN);
    if (g_page_about)          lv_obj_add_flag(g_page_about, LV_OBJ_FLAG_HIDDEN);
}

static void show_page(ui_page_t page) {
    hide_all_pages();

    // Leaving the SCAN page: tear down the on-demand BLE HID stack (started for
    // INPUT mode) so the radio/RAM is released and WiFi resumes. (ble_hid_stop
    // is idempotent / no-op if BLE was never started.)
    if (g_current_page == UI_PAGE_SCAN && page != UI_PAGE_SCAN) {
        if (ble_hid_is_started()) ble_hid_stop();
    }

    g_current_page = page;

    switch (page) {
        case UI_PAGE_HOME:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_home, LV_OBJ_FLAG_HIDDEN);
            g_at_home = true;
            break;
        case UI_PAGE_SCAN:
            lv_obj_clear_flag(g_page_scan, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            // Clear stale result text from a previous visit so old data does
            // not linger on the LCD. (Does NOT touch the stocktake session
            // state g_st_* — an open session survives leaving/returning.)
            if (g_scan_lbl_barcode)   lv_label_set_text(g_scan_lbl_barcode, "");
            if (g_scan_lbl_status)    lv_label_set_text(g_scan_lbl_status, "");
            if (g_scan_lbl_stocktake) {
                lv_label_set_text(g_scan_lbl_stocktake,
                                  g_st_active ? "Stocktake in progress" : "");
            }
            // If we are in COUNT mode but have no open session (e.g. returned
            // to this page after the session was ended/lost), re-open one so
            // scanning works without needing to re-cycle the mode.
            if (g_scan_mode == SCAN_MODE_STOCKTAKE && !g_st_active
                    && !g_st_pending_start) {
                ui_stocktake_begin();
            }
            // Entering SCAN already in INPUT-BLE mode (e.g. came back after
            // leaving): bring the BLE HID keyboard back up. INPUT-MQTT keeps
            // WiFi and needs no BLE.
            if (g_scan_mode == SCAN_MODE_INPUT_BLE && !ble_hid_is_started()) {
                ble_hid_begin();
            }
            // Animate slide-in
            anim_page_slide_in(g_page_scan);
            ui_preview_start();
            break;
        case UI_PAGE_SETTINGS:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_settings, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            g_settings_focus = 0;
            settings_refresh();
            update_settings_focus();
            anim_page_slide_in(g_page_settings);
            break;
        case UI_PAGE_INVENTORY:
            lv_obj_clear_flag(g_page_inventory, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_inventory);
            if (g_inv_lbl_count) {
                lv_label_set_text_fmt(g_inv_lbl_count, "%d barcode(s) counted", g_st_entry_count);
            }
            if (g_inv_lbl_status) {
                lv_label_set_text(g_inv_lbl_status,
                    g_st_active ? "Long-press to end session"
                                : "Scan in STOCKTAKE mode to start");
            }
            break;
        case UI_PAGE_PHOTO:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_photo, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_photo);
            break;
        case UI_PAGE_RESULT:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_result, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_result);
            break;
        case UI_PAGE_INVENTORY_LIST:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_inventory_list, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_inventory_list);
            break;
        case UI_PAGE_LOG:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_log, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            g_log_day_offset = 0;      // default to today on entry
            log_update_date_label();
            log_render();
            anim_page_slide_in(g_page_log);
            break;
        case UI_PAGE_ABOUT:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_about, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_about);
            break;
        default:
            break;
    }
}

// ─── HOME page ────────────────────────────────────────────────────────────────

static void create_home_page(void) {
    g_page_home = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_page_home, SCR_W, SCR_H - STATUS_H);
    lv_obj_set_pos(g_page_home, 0, STATUS_H);
    lv_obj_set_style_bg_color(g_page_home, COL_BG, 0);
    lv_obj_set_style_radius(g_page_home, 0, 0);
    // Zero the default lv_obj padding + border so the absolute button positions
    // below are measured from the real page edges (otherwise the ~5px content
    // padding shifts every item down/right and they look off-centre).
    lv_obj_set_style_pad_all(g_page_home, 0, 0);
    lv_obj_set_style_border_width(g_page_home, 0, 0);
    lv_obj_set_scrollbar_mode(g_page_home, LV_SCROLLBAR_MODE_OFF);

    const char* labels[HOME_ITEM_COUNT] = {
        [HOME_ITEM_SCAN]      = "掃描",
        [HOME_ITEM_INVENTORY] = "盤點",
        [HOME_ITEM_SETTINGS]  = "設定",
        [HOME_ITEM_ABOUT]     = "關於"
    };

    // Calculate vertical centering
    uint16_t total_h = HOME_ITEM_COUNT * ITEM_H + (HOME_ITEM_COUNT - 1) * ITEM_GAP;
    int16_t start_y = (SCR_H - STATUS_H - total_h) / 2;

    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        menu_item_t* it = &g_home_items[i];
        it->label = labels[i];

        // Button
        it->btn = lv_btn_create(g_page_home);
        lv_obj_set_size(it->btn, 200, ITEM_H);
        lv_obj_set_pos(it->btn, (SCR_W - 200) / 2, start_y + i * (ITEM_H + ITEM_GAP));
        lv_obj_set_style_radius(it->btn, 8, 0);
        lv_obj_set_style_bg_color(it->btn, COL_NORMAL, 0);
        lv_obj_set_style_bg_opa(it->btn, LV_OPA_COVER, 0);

        // Label
        it->lbl = lv_label_create(it->btn);
        lv_label_set_text(it->lbl, it->label);
        lv_obj_center(it->lbl);
        lv_obj_set_style_text_color(it->lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(it->lbl, &lv_font_cjk_tc_16, 0);
    }

    update_home_focus();
}

static void set_item_focused(menu_item_t* item, bool focused) {
    if (!item || !item->btn) return;
    lv_obj_set_style_bg_color(item->btn, focused ? COL_FOCUS : COL_NORMAL, 0);
}

static void update_home_focus(void) {
    for (int i = 0; i < HOME_ITEM_COUNT; i++) {
        set_item_focused(&g_home_items[i], (i == g_home_focus));
    }
}

static void on_home_select(int8_t idx) {
    switch (idx) {
        case HOME_ITEM_SCAN:
            show_page(UI_PAGE_SCAN);
            break;
        case HOME_ITEM_INVENTORY:
            show_page(UI_PAGE_INVENTORY);
            break;
        case HOME_ITEM_SETTINGS:
            show_page(UI_PAGE_SETTINGS);
            break;
        case HOME_ITEM_ABOUT:
            show_page(UI_PAGE_ABOUT);
            break;
        default:
            break;
    }
}

// ─── SCAN page ───────────────────────────────────────────────────────────────

static void create_scan_page(void) {
    g_page_scan = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_page_scan, SCR_W, SCR_H - STATUS_H);
    lv_obj_set_pos(g_page_scan, 0, STATUS_H);
    lv_obj_set_style_bg_color(g_page_scan, COL_BG, 0);
    lv_obj_set_style_radius(g_page_scan, 0, 0);
    lv_obj_set_scrollbar_mode(g_page_scan, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_page_scan, LV_OBJ_FLAG_HIDDEN);

    // Mode indicator label (top-left)
    g_scan_lbl_mode = lv_label_create(g_page_scan);
    lv_label_set_text(g_scan_lbl_mode, "[QUERY]");
    lv_obj_set_style_text_color(g_scan_lbl_mode, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_scan_lbl_mode, &lv_font_montserrat_12, 0);
    lv_obj_align(g_scan_lbl_mode, LV_ALIGN_TOP_LEFT, 10, 6);

    // Status label
    g_scan_lbl_status = lv_label_create(g_page_scan);
    lv_label_set_text(g_scan_lbl_status, "Ready to Scan");
    lv_obj_set_style_text_color(g_scan_lbl_status, COL_TEXT, 0);
    lv_obj_set_style_text_font(g_scan_lbl_status, &lv_font_cjk_tc_16, 0);
    lv_obj_align(g_scan_lbl_status, LV_ALIGN_TOP_MID, 0, 26);

    // Barcode result
    g_scan_lbl_barcode = lv_label_create(g_page_scan);
    lv_label_set_text(g_scan_lbl_barcode, "");
    lv_obj_set_style_text_color(g_scan_lbl_barcode, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_scan_lbl_barcode, &lv_font_cjk_tc_16, 0);
    lv_obj_align(g_scan_lbl_barcode, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_width(g_scan_lbl_barcode, 220);

    // Info line
    g_scan_lbl_info = lv_label_create(g_page_scan);
    lv_label_set_text(g_scan_lbl_info, "Short: mode  Long: confirm");
    lv_obj_set_style_text_color(g_scan_lbl_info, (lv_color_t){ .full = 0x8410 }, 0); // dim grey
    lv_obj_set_style_text_font(g_scan_lbl_info, &lv_font_montserrat_12, 0);
    lv_obj_align(g_scan_lbl_info, LV_ALIGN_TOP_MID, 0, 95);

    // Stocktake feedback line — BELOW the viewfinder (screen y>=235, i.e. page
    // y>=211) so the live camera preview does not cover it. Uses the CJK font
    // so Chinese item names render.
    g_scan_lbl_stocktake = lv_label_create(g_page_scan);
    lv_label_set_text(g_scan_lbl_stocktake, "");
    lv_obj_set_style_text_color(g_scan_lbl_stocktake, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_scan_lbl_stocktake, &lv_font_cjk_tc_16, 0);
    lv_obj_set_width(g_scan_lbl_stocktake, 230);
    lv_obj_align(g_scan_lbl_stocktake, LV_ALIGN_TOP_MID, 0, 215);
}

// ─── SETTINGS page ────────────────────────────────────────────────────────────

static void create_settings_page(void) {
    g_page_settings = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_page_settings, SCR_W, SCR_H - STATUS_H);
    lv_obj_set_pos(g_page_settings, 0, STATUS_H);
    lv_obj_set_style_bg_color(g_page_settings, COL_BG, 0);
    lv_obj_set_style_radius(g_page_settings, 0, 0);
    lv_obj_set_style_border_width(g_page_settings, 0, 0);
    lv_obj_set_scrollbar_mode(g_page_settings, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_page_settings, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t* title = lv_label_create(g_page_settings);
    lv_label_set_text(title, "設定");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_tc_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    // Scrollable list container
    g_settings_list = lv_obj_create(g_page_settings);
    lv_obj_set_size(g_settings_list, 232, SCR_H - STATUS_H - 32);
    lv_obj_align(g_settings_list, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_style_bg_color(g_settings_list, COL_BG, 0);
    lv_obj_set_style_border_width(g_settings_list, 0, 0);
    lv_obj_set_style_pad_all(g_settings_list, 2, 0);
    lv_obj_set_style_pad_row(g_settings_list, 4, 0);
    lv_obj_set_flex_flow(g_settings_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(g_settings_list, LV_SCROLLBAR_MODE_AUTO);

    const char* row_labels[SETTINGS_ROW_COUNT] = {
        "WiFi", "MQTT", "藍牙 HID", "亮度", "音量", "語言", "工作日誌"
    };

    for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
        lv_obj_t* row = lv_obj_create(g_settings_list);
        lv_obj_set_size(row, lv_pct(100), 34);
        lv_obj_set_style_bg_color(row, COL_PANEL, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, COL_PANEL, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_hor(row, 8, 0);
        lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        g_settings_rows[i] = row;

        lv_obj_t* lbl = lv_label_create(row);
        lv_label_set_text(lbl, row_labels[i]);
        lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_cjk_tc_16, 0);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        if (i == SET_ROW_BRIGHTNESS) {
            // Brightness slider lives inside its row (right side).
            g_settings_slider = lv_slider_create(row);
            lv_obj_set_size(g_settings_slider, 110, 8);
            lv_obj_align(g_settings_slider, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_slider_set_range(g_settings_slider, 10, 255);
            lv_slider_set_value(g_settings_slider, DISP_BL_DEFAULT, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(g_settings_slider, COL_BG, 0);
            lv_obj_set_style_bg_color(g_settings_slider, COL_SLIDER, LV_PART_INDICATOR);
            lv_obj_set_style_bg_color(g_settings_slider, COL_FOCUS, LV_PART_KNOB);
            g_settings_lbl_brightness = lbl;
        } else {
            // Value / status label (right side).
            lv_obj_t* val = lv_label_create(row);
            lv_label_set_text(val, "--");
            lv_obj_set_style_text_color(val, (lv_color_t){ .full = 0x07E0 }, 0);
            lv_obj_set_style_text_font(val, &lv_font_cjk_tc_16, 0);
            lv_obj_align(val, LV_ALIGN_RIGHT_MID, 0, 0);
            g_settings_vals[i] = val;
        }
    }
    g_settings_focus = 0;
}

// Refresh read-only status values + focus ring (LVGL task only).
static void settings_refresh(void) {
    if (g_settings_vals[SET_ROW_WIFI]) {
        lv_label_set_text(g_settings_vals[SET_ROW_WIFI],
                          wifi_is_connected() ? "已連線" : "未連線");
    }
    if (g_settings_vals[SET_ROW_MQTT]) {
        lv_label_set_text(g_settings_vals[SET_ROW_MQTT],
                          mqtt_is_connected() ? "已連線" : "未連線");
    }
    if (g_settings_vals[SET_ROW_BLE]) {
        const char* bs = !ble_hid_is_started() ? "關閉"
                         : (ble_hid_is_connected() ? "已連線" : "等待配對");
        lv_label_set_text(g_settings_vals[SET_ROW_BLE], bs);
    }
    if (g_settings_vals[SET_ROW_VOLUME]) {
        lv_label_set_text(g_settings_vals[SET_ROW_VOLUME], "60%");   // Phase A stub
    }
    if (g_settings_vals[SET_ROW_LANGUAGE]) {
        lv_label_set_text(g_settings_vals[SET_ROW_LANGUAGE],
                          g_settings_lang_zh ? "繁體中文" : "English");
    }
    if (g_settings_vals[SET_ROW_LOG]) {
        lv_label_set_text(g_settings_vals[SET_ROW_LOG], "檢視 ▶");
    }
}

static void update_settings_focus(void) {
    for (int i = 0; i < SETTINGS_ROW_COUNT; i++) {
        if (!g_settings_rows[i]) continue;
        bool f = (i == g_settings_focus);
        lv_obj_set_style_border_color(g_settings_rows[i], f ? COL_FOCUS : COL_PANEL, 0);
        lv_obj_set_style_bg_color(g_settings_rows[i], f ? COL_FOCUS : COL_PANEL, 0);
        if (f) lv_obj_scroll_to_view(g_settings_rows[i], LV_ANIM_OFF);
    }
}

// ─── INVENTORY page ───────────────────────────────────────────────────────────

static void create_inventory_page(void) {
    g_page_inventory = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_page_inventory, SCR_W, SCR_H - STATUS_H);
    lv_obj_set_pos(g_page_inventory, 0, STATUS_H);
    lv_obj_set_style_bg_color(g_page_inventory, COL_BG, 0);
    lv_obj_set_style_radius(g_page_inventory, 0, 0);
    lv_obj_set_scrollbar_mode(g_page_inventory, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_page_inventory, LV_OBJ_FLAG_HIDDEN);

    // Title
    lv_obj_t* lbl = lv_label_create(g_page_inventory);
    lv_label_set_text(lbl, "Stocktake Session");
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 15);

    // Item count
    g_inv_lbl_count = lv_label_create(g_page_inventory);
    lv_label_set_text(g_inv_lbl_count, "0 barcode(s) counted");
    lv_obj_set_style_text_color(g_inv_lbl_count, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_inv_lbl_count, &lv_font_montserrat_12, 0);
    lv_obj_align(g_inv_lbl_count, LV_ALIGN_TOP_MID, 0, 45);

    // Status line
    g_inv_lbl_status = lv_label_create(g_page_inventory);
    lv_label_set_text(g_inv_lbl_status, "Scan in STOCKTAKE mode to start");
    lv_obj_set_style_text_color(g_inv_lbl_status, (lv_color_t){ .full = 0x8410 }, 0);
    lv_obj_set_style_text_font(g_inv_lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_align(g_inv_lbl_status, LV_ALIGN_TOP_MID, 0, 70);
}

// ─── PHOTO page ───────────────────────────────────────────────────────────────

static void create_photo_page(void) {
    g_page_photo = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_page_photo, SCR_W, SCR_H - STATUS_H);
    lv_obj_set_pos(g_page_photo, 0, STATUS_H);
    lv_obj_set_style_bg_color(g_page_photo, COL_BG, 0);
    lv_obj_set_style_radius(g_page_photo, 0, 0);
    lv_obj_set_scrollbar_mode(g_page_photo, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_page_photo, LV_OBJ_FLAG_HIDDEN);

    g_photo_lbl_status = lv_label_create(g_page_photo);
    lv_label_set_text(g_photo_lbl_status, "Photo Mode");
    lv_obj_set_style_text_color(g_photo_lbl_status, COL_TEXT, 0);
    lv_obj_set_style_text_font(g_photo_lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_align(g_photo_lbl_status, LV_ALIGN_TOP_MID, 0, 30);

    g_photo_lbl_result = lv_label_create(g_page_photo);
    lv_label_set_text(g_photo_lbl_result, "Capture to recognize");
    lv_obj_set_style_text_color(g_photo_lbl_result, (lv_color_t){ .full = 0x8410 }, 0);
    lv_obj_set_style_text_font(g_photo_lbl_result, &lv_font_montserrat_12, 0);
    lv_obj_align(g_photo_lbl_result, LV_ALIGN_TOP_MID, 0, 60);
}

// ─── Shared helpers for new pages ─────────────────────────────────────────────

// Create a standard full-screen page container (below the status bar), hidden.
static lv_obj_t* create_page_container(void) {
    lv_obj_t* page = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page, SCR_W, SCR_H - STATUS_H);
    lv_obj_set_pos(page, 0, STATUS_H);
    lv_obj_set_style_bg_color(page, COL_BG, 0);
    lv_obj_set_style_radius(page, 0, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(page, LV_OBJ_FLAG_HIDDEN);
    return page;
}

// Create an action button with a centered CJK label. Returns the button.
static lv_obj_t* create_action_btn(lv_obj_t* parent, const char* text,
                                   lv_coord_t w, lv_coord_t h) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_bg_color(btn, COL_NORMAL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_cjk_tc_16, 0);
    return btn;
}

static void set_btn_focused(lv_obj_t* btn, bool focused) {
    if (!btn) return;
    lv_obj_set_style_bg_color(btn, focused ? COL_FOCUS : COL_NORMAL, 0);
}

// ─── RESULT page ──────────────────────────────────────────────────────────────
// Create one "label: value" field row. Returns the value label (for updates).
static lv_obj_t* result_field_row(lv_obj_t* parent, const char* label_text,
                                  int16_t y, bool highlight) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, COL_STATUS, 0);  // muted
    lv_obj_set_style_text_font(lbl, &lv_font_cjk_tc_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 10, y);

    lv_obj_t* val = lv_label_create(parent);
    lv_label_set_text(val, "--");
    lv_obj_set_style_text_color(val,
        highlight ? (lv_color_t){ .full = 0x07E0 } : COL_TEXT, 0);
    lv_obj_set_style_text_font(val, &lv_font_cjk_tc_16, 0);
    lv_obj_set_width(val, 150);
    lv_obj_align(val, LV_ALIGN_TOP_LEFT, 80, y);
    return val;
}

static void create_result_page(void) {
    g_page_result = create_page_container();

    lv_obj_t* title = lv_label_create(g_page_result);
    lv_label_set_text(title, "查詢結果");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_tc_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    g_result_lbl_barcode = lv_label_create(g_page_result);
    lv_label_set_text(g_result_lbl_barcode, "");
    lv_obj_set_style_text_color(g_result_lbl_barcode, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_result_lbl_barcode, &lv_font_cjk_tc_16, 0);
    lv_obj_set_width(g_result_lbl_barcode, 220);
    lv_obj_align(g_result_lbl_barcode, LV_ALIGN_TOP_MID, 0, 24);

    // 6 field rows
    int16_t y = 50;
    g_result_val_name     = result_field_row(g_page_result, "品名",   y,       true);
    g_result_val_spec     = result_field_row(g_page_result, "規格",   y + 24,  false);
    g_result_val_qty      = result_field_row(g_page_result, "數量",   y + 48,  false);
    g_result_val_location = result_field_row(g_page_result, "儲位",   y + 72,  true);
    g_result_val_supplier = result_field_row(g_page_result, "供應商", y + 96,  false);
    g_result_val_date     = result_field_row(g_page_result, "入庫日", y + 120, false);

    // Action buttons (bottom)
    g_result_btn_rescan = create_action_btn(g_page_result, "重新掃描", 100, 36);
    lv_obj_align(g_result_btn_rescan, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    g_result_btn_home = create_action_btn(g_page_result, "回主頁", 100, 36);
    lv_obj_align(g_result_btn_home, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    g_result_focus = 0;
    set_btn_focused(g_result_btn_rescan, true);
    set_btn_focused(g_result_btn_home, false);
}

static void update_result_focus(void) {
    set_btn_focused(g_result_btn_rescan, g_result_focus == 0);
    set_btn_focused(g_result_btn_home, g_result_focus == 1);
}

// Populate the 6 result fields (called on the LVGL task).
static void result_populate(const char* barcode, const char* name,
                            const char* spec, int qty, const char* location,
                            const char* supplier, const char* date_in) {
    if (g_result_lbl_barcode) lv_label_set_text(g_result_lbl_barcode, barcode ? barcode : "");
    if (g_result_val_name)     lv_label_set_text(g_result_val_name, name && *name ? name : "--");
    if (g_result_val_spec)     lv_label_set_text(g_result_val_spec, spec && *spec ? spec : "--");
    if (g_result_val_qty)      lv_label_set_text_fmt(g_result_val_qty, "%d", qty);
    if (g_result_val_location) lv_label_set_text(g_result_val_location, location && *location ? location : "--");
    if (g_result_val_supplier) lv_label_set_text(g_result_val_supplier, supplier && *supplier ? supplier : "--");
    if (g_result_val_date)     lv_label_set_text(g_result_val_date, date_in && *date_in ? date_in : "--");
    g_result_focus = 0;
    update_result_focus();
}

// ─── INVENTORY LIST page ──────────────────────────────────────────────────────

static void create_inventory_list_page(void) {
    g_page_inventory_list = create_page_container();

    lv_obj_t* title = lv_label_create(g_page_inventory_list);
    lv_label_set_text(title, "盤點清單");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_tc_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    g_invlist_lbl_count = lv_label_create(g_page_inventory_list);
    lv_label_set_text(g_invlist_lbl_count, "共 0 筆");
    lv_obj_set_style_text_color(g_invlist_lbl_count, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_invlist_lbl_count, &lv_font_cjk_tc_16, 0);
    lv_obj_align(g_invlist_lbl_count, LV_ALIGN_TOP_MID, 0, 24);

    // Scrollable list body
    g_invlist_container = lv_obj_create(g_page_inventory_list);
    lv_obj_set_size(g_invlist_container, 224, 150);
    lv_obj_align(g_invlist_container, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_style_bg_color(g_invlist_container, COL_PANEL, 0);
    lv_obj_set_style_border_width(g_invlist_container, 1, 0);
    lv_obj_set_style_border_color(g_invlist_container, COL_STATUS, 0);
    lv_obj_set_style_radius(g_invlist_container, 4, 0);
    lv_obj_set_style_pad_all(g_invlist_container, 4, 0);
    lv_obj_set_flex_flow(g_invlist_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(g_invlist_container, LV_SCROLLBAR_MODE_AUTO);

    // Action buttons
    g_invlist_btn_back = create_action_btn(g_page_inventory_list, "返回", 100, 32);
    lv_obj_align(g_invlist_btn_back, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    g_invlist_btn_upload = create_action_btn(g_page_inventory_list, "上傳全部", 100, 32);
    lv_obj_align(g_invlist_btn_upload, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    g_invlist_focus = 0;
    set_btn_focused(g_invlist_btn_back, true);
    set_btn_focused(g_invlist_btn_upload, false);
}

static void update_invlist_focus(void) {
    set_btn_focused(g_invlist_btn_back, g_invlist_focus == 0);
    set_btn_focused(g_invlist_btn_upload, g_invlist_focus == 1);
}

// Render the current stocktake entries into the list body (LVGL task only).
static void invlist_render(void) {
    if (!g_invlist_container) return;
    lv_obj_clean(g_invlist_container);   // remove previous rows

    if (g_invlist_lbl_count) {
        lv_label_set_text_fmt(g_invlist_lbl_count, "共 %d 筆", g_st_entry_count);
    }

    if (g_st_entry_count == 0) {
        lv_obj_t* empty = lv_label_create(g_invlist_container);
        lv_label_set_text(empty, "尚無資料");
        lv_obj_set_style_text_color(empty, COL_STATUS, 0);
        lv_obj_set_style_text_font(empty, &lv_font_cjk_tc_16, 0);
        return;
    }

    for (uint8_t i = 0; i < g_st_entry_count; i++) {
        lv_obj_t* row = lv_label_create(g_invlist_container);
        lv_label_set_text_fmt(row, "%d. %s  x%d",
                              i + 1, g_st_entries[i].barcode,
                              g_st_entries[i].actual_qty);
        lv_obj_set_style_text_color(row, (lv_color_t){ .full = 0x07E0 }, 0);
        lv_obj_set_style_text_font(row, &lv_font_cjk_tc_16, 0);
        lv_obj_set_width(row, lv_pct(100));
    }
}

// ─── LOG page (reads real scan_log data from SD) ──────────────────────────────

static void log_update_date_label(void) {
    if (!g_log_lbl_date) return;
    char date[16];
    scan_log_date_for_offset(g_log_day_offset, date, sizeof(date));
    lv_label_set_text_fmt(g_log_lbl_date, "◀  %s  ▶", date);
}

// Render the selected day's log entries into g_log_body (LVGL task only).
static void log_render(void) {
    if (!g_log_body) return;
    lv_obj_clean(g_log_body);

    char date[16];
    scan_log_date_for_offset(g_log_day_offset, date, sizeof(date));
    String json = scan_log_read(date, 0, 50);   // up to 50 newest-day entries

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, json);
    JsonArray arr = doc.as<JsonArray>();

    if (err || arr.isNull() || arr.size() == 0) {
        lv_obj_t* empty = lv_label_create(g_log_body);
        lv_label_set_text(empty, "尚無紀錄");
        lv_obj_set_style_text_color(empty, COL_STATUS, 0);
        lv_obj_set_style_text_font(empty, &lv_font_cjk_tc_16, 0);
        return;
    }

    for (JsonObject e : arr) {
        const char* ts      = e["ts"]      | "";
        const char* type    = e["type"]    | "";
        const char* content = e["content"] | "";
        // ts is ISO "YYYY-MM-DDTHH:MM:SS" — show just HH:MM:SS (after 'T').
        const char* clock = strchr(ts, 'T');
        clock = clock ? clock + 1 : ts;

        lv_obj_t* row = lv_label_create(g_log_body);
        lv_label_set_text_fmt(row, "%s %s %s", clock, type, content);
        lv_obj_set_style_text_color(row, (lv_color_t){ .full = 0x07E0 }, 0);
        lv_obj_set_style_text_font(row, &lv_font_cjk_tc_16, 0);
        lv_obj_set_width(row, lv_pct(100));
    }
}

static void create_log_page(void) {
    g_page_log = create_page_container();

    lv_obj_t* title = lv_label_create(g_page_log);
    lv_label_set_text(title, "工作日誌");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_tc_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    g_log_lbl_date = lv_label_create(g_page_log);
    lv_obj_set_style_text_color(g_log_lbl_date, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_log_lbl_date, &lv_font_cjk_tc_16, 0);
    lv_obj_align(g_log_lbl_date, LV_ALIGN_TOP_MID, 0, 26);
    log_update_date_label();

    // Scrollable entries body
    g_log_body = lv_obj_create(g_page_log);
    lv_obj_set_size(g_log_body, 224, 170);
    lv_obj_align(g_log_body, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_bg_color(g_log_body, COL_PANEL, 0);
    lv_obj_set_style_border_width(g_log_body, 1, 0);
    lv_obj_set_style_border_color(g_log_body, COL_STATUS, 0);
    lv_obj_set_style_radius(g_log_body, 4, 0);
    lv_obj_set_style_pad_all(g_log_body, 4, 0);
    lv_obj_set_style_pad_row(g_log_body, 2, 0);
    lv_obj_set_flex_flow(g_log_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(g_log_body, LV_SCROLLBAR_MODE_AUTO);

    lv_obj_t* hint = lv_label_create(g_page_log);
    lv_label_set_text(hint, "短按:換日  長按:返回");
    lv_obj_set_style_text_color(hint, COL_STATUS, 0);
    lv_obj_set_style_text_font(hint, &lv_font_cjk_tc_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

// ─── ABOUT page ───────────────────────────────────────────────────────────────

static void create_about_page(void) {
    g_page_about = create_page_container();

    lv_obj_t* title = lv_label_create(g_page_about);
    lv_label_set_text(title, "韌體資訊");
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_cjk_tc_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    lv_obj_t* info = lv_label_create(g_page_about);
    lv_label_set_text(info,
        BOARD_NAME "\n"
        "v" FW_VERSION "\n\n"
        "HW: ESP32-S3\n"
        "Display: ST7789T3\n"
        "Camera: OV5640\n"
        "ID: " DEVICE_ID);
    lv_obj_set_style_text_color(info, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(info, &lv_font_cjk_tc_16, 0);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 56);

    lv_obj_t* hint = lv_label_create(g_page_about);
    lv_label_set_text(hint, "長按返回主頁");
    lv_obj_set_style_text_color(hint, COL_STATUS, 0);
    lv_obj_set_style_text_font(hint, &lv_font_cjk_tc_16, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);
}

// ─── Navigation event handler ─────────────────────────────────────────────────
// Called from loop() via ui_nav_event()

void ui_nav_event(nav_event_t ev) {
    if (ev == NAV_NONE) return;

    if (ev == NAV_HOME) {
        g_home_focus = 0;
        update_home_focus();
        show_page(UI_PAGE_HOME);
        return;
    }

    switch (g_current_page) {
        case UI_PAGE_HOME: {
            if (ev == NAV_NEXT) {
                g_home_focus = (g_home_focus + 1) % HOME_ITEM_COUNT;
                update_home_focus();
                // Animate focused button
                if (g_home_items[g_home_focus].btn) {
                    anim_button_press(g_home_items[g_home_focus].btn);
                }
            } else if (ev == NAV_CONFIRM) {
                // Animate button press
                if (g_home_items[g_home_focus].btn) {
                    anim_button_press(g_home_items[g_home_focus].btn);
                }
                on_home_select(g_home_focus);
            }
            break;
        }

        case UI_PAGE_SCAN: {
            if (ev == NAV_NEXT) {
                // Short press triggers one high-resolution (SVGA) scan. A QUERY
                // hit now switches to the standalone RESULT page, so there is
                // no longer an in-place "result shown" state on this page.
                scan_preview_request_hires();
            } else if (ev == NAV_CONFIRM) {
                // Long press = cycle scan mode (QUERY → INPUT → IN → OUT → COUNT).
                ui_cycle_scan_mode();
            }
            break;
        }

        case UI_PAGE_SETTINGS: {
            if (ev == NAV_NEXT) {
                // Short press: move focus. For the brightness row, bump the
                // slider instead so a single button can still adjust it.
                if (g_settings_focus == SET_ROW_BRIGHTNESS && g_settings_slider) {
                    int v = lv_slider_get_value(g_settings_slider) + 40;
                    if (v > 255) v = 10;   // wrap
                    lv_slider_set_value(g_settings_slider, v, LV_ANIM_OFF);
                    display_set_brightness((uint8_t)v);
                } else {
                    g_settings_focus = (g_settings_focus + 1) % SETTINGS_ROW_COUNT;
                    update_settings_focus();
                }
            } else if (ev == NAV_CONFIRM) {
                switch (g_settings_focus) {
                    case SET_ROW_WIFI:
                        // WiFi row = open the web config portal. All detailed
                        // settings (SSID/password/MQTT broker) are entered in
                        // the portal, not on-device.
                        if (g_wifi_portal_cb) {
                            if (g_settings_vals[SET_ROW_WIFI]) {
                                lv_label_set_text(g_settings_vals[SET_ROW_WIFI], "配網中...");
                            }
                            g_wifi_portal_cb();
                        }
                        break;
                    case SET_ROW_BLE:
                        // BLE row = simple on/off toggle (pairing happens on the
                        // host side). Starting BLE pauses WiFi (single radio).
                        if (ble_hid_is_started()) ble_hid_stop();
                        else                      ble_hid_begin();
                        settings_refresh();
                        break;
                    case SET_ROW_BRIGHTNESS:
                        // Confirm = apply + advance focus off the slider row.
                        if (g_settings_slider) {
                            display_set_brightness((uint8_t)lv_slider_get_value(g_settings_slider));
                        }
                        g_settings_focus = (g_settings_focus + 1) % SETTINGS_ROW_COUNT;
                        update_settings_focus();
                        break;
                    case SET_ROW_LANGUAGE:
                        g_settings_lang_zh = !g_settings_lang_zh;
                        settings_refresh();
                        break;
                    case SET_ROW_LOG:
                        show_page(UI_PAGE_LOG);
                        break;
                    default:
                        // MQTT / Volume are read-only (status shown in value).
                        break;
                }
            }
            break;
        }

        case UI_PAGE_INVENTORY: {
            if (ev == NAV_NEXT) {
                // Short press opens the accumulated stocktake list.
                invlist_render();
                show_page(UI_PAGE_INVENTORY_LIST);
            } else if (ev == NAV_CONFIRM) {
                // Long press ends the open stocktake session.
                ui_stocktake_end();
            }
            break;
        }

        case UI_PAGE_RESULT: {
            if (ev == NAV_NEXT) {
                g_result_focus = (g_result_focus + 1) % 2;
                update_result_focus();
            } else if (ev == NAV_CONFIRM) {
                if (g_result_focus == 0) {
                    show_page(UI_PAGE_SCAN);   // rescan
                } else {
                    g_home_focus = 0;
                    update_home_focus();
                    show_page(UI_PAGE_HOME);   // back to home
                }
            }
            break;
        }

        case UI_PAGE_INVENTORY_LIST: {
            if (ev == NAV_NEXT) {
                g_invlist_focus = (g_invlist_focus + 1) % 2;
                update_invlist_focus();
            } else if (ev == NAV_CONFIRM) {
                if (g_invlist_focus == 0) {
                    show_page(UI_PAGE_INVENTORY);   // back
                } else {
                    // Upload all. Phase A: the per-scan stocktake flow already
                    // publishes each count to the server (mqtt_stocktake_scan);
                    // this button just ends the session as a batch commit.
                    ui_stocktake_end();
                    show_page(UI_PAGE_INVENTORY);
                }
            }
            break;
        }

        case UI_PAGE_LOG: {
            if (ev == NAV_NEXT) {
                // Cycle back through the last 7 days; re-read that day's log.
                g_log_day_offset -= 1;
                if (g_log_day_offset < -6) g_log_day_offset = 0;
                log_update_date_label();
                log_render();
            } else if (ev == NAV_CONFIRM) {
                show_page(UI_PAGE_SETTINGS);   // log is reached from settings
            }
            break;
        }

        case UI_PAGE_ABOUT: {
            if (ev == NAV_CONFIRM) {
                g_home_focus = 0;
                update_home_focus();
                show_page(UI_PAGE_HOME);
            }
            break;
        }

        default:
            break;
    }
}

// ─── Public accessors ──────────────────────────────────────────────────────────

ui_page_t ui_get_current_page(void) {
    return g_current_page;
}

scan_mode_t ui_get_scan_mode(void) {
    return g_scan_mode;
}

void ui_cycle_scan_mode(void) {
    g_scan_mode = (scan_mode_t)((g_scan_mode + 1) % SCAN_MODE_COUNT);
    const char* mode_names[SCAN_MODE_COUNT] = {
        [SCAN_MODE_QUERY]      = "[QUERY]",
        [SCAN_MODE_INPUT_BLE]  = "[INPUT-BLE]",
        [SCAN_MODE_INPUT_MQTT] = "[INPUT-MQTT]",
        [SCAN_MODE_STOCK_IN]   = "[IN]",
        [SCAN_MODE_STOCK_OUT]  = "[OUT]",
        [SCAN_MODE_STOCKTAKE]  = "[COUNT]"
    };
    if (g_scan_lbl_mode) {
        lv_label_set_text(g_scan_lbl_mode, mode_names[g_scan_mode]);
    }

    // Entering STOCKTAKE mode opens a session (if not already open). The server
    // replies asynchronously with session_id via ui_stocktake_on_started().
    if (g_scan_mode == SCAN_MODE_STOCKTAKE && !g_st_active && !g_st_pending_start) {
        ui_stocktake_begin();
    }

    // On-demand BLE HID + WiFi/BLE mutual exclusion (single radio):
    //   INPUT-BLE  → start BLE (ble_hid_begin pauses WiFi) — wireless gun to host.
    //   INPUT-MQTT → NO BLE; keep WiFi up so mqtt_scan_input can publish to the
    //                server (which relays to the Web UI via SSE).
    //   any other  → stop BLE (restores WiFi).
    if (g_scan_mode == SCAN_MODE_INPUT_BLE) {
        if (!ble_hid_is_started()) {
            if (g_scan_lbl_info) lv_label_set_text(g_scan_lbl_info, "BLE: starting...");
            ble_hid_begin();
        }
    } else {
        if (ble_hid_is_started()) ble_hid_stop();
    }
}

uint8_t ui_inventory_count(void) {
    return g_st_entry_count;
}

// ─── Stocktake session state machine (P3) ──────────────────────────────────

bool ui_stocktake_is_active(void) {
    return g_st_active;
}

// Find the entry index for a barcode, or -1 if not present.
static int st_find_entry(const char* barcode) {
    for (uint8_t i = 0; i < g_st_entry_count; i++) {
        if (strncmp(g_st_entries[i].barcode, barcode,
                    sizeof(g_st_entries[0].barcode)) == 0) {
            return (int)i;
        }
    }
    return -1;
}

void ui_stocktake_begin(void) {
    if (g_st_active || g_st_pending_start) return;
    g_st_pending_start = true;
    g_st_entry_count = 0;   // fresh session counts
    ui_post_result("STInfo", "Starting stocktake...");
    mqtt_stocktake_start();
}

void ui_stocktake_end(void) {
    if (!g_st_active || g_st_session_id == 0) {
        ui_post_result("STEnd", "No active session");
        return;
    }
    ui_post_result("STEnd", "Ending session...");
    mqtt_stocktake_end(g_st_session_id);
}

void ui_stocktake_on_started(int session_id) {
    g_st_session_id = session_id;
    g_st_active = true;
    g_st_pending_start = false;
    // Runs in MQTT context — post display text for the LVGL task to apply.
    char line[48];
    snprintf(line, sizeof(line), "Session #%d - scan to count", session_id);
    ui_post_result("STInfo", line);
    Serial.printf("[UI][ST] session started id=%d\n", session_id);
}

void ui_stocktake_on_scanned(const char* name, int system_qty,
                             int actual_qty, int difference) {
    // Runs in MQTT context — post display text for the LVGL task to apply.
    char line[64];
    snprintf(line, sizeof(line), "%s sys=%d act=%d d=%+d",
             name ? name : "?", system_qty, actual_qty, difference);
    ui_post_result("STInfo", line);
}

void ui_stocktake_on_ended(void) {
    g_st_active = false;
    g_st_pending_start = false;
    int closed = g_st_session_id;
    g_st_session_id = 0;
    char line[48];
    snprintf(line, sizeof(line), "Session #%d closed", closed);
    ui_post_result("STEnd", line);
    Serial.printf("[UI][ST] session ended id=%d\n", closed);
}

void ui_show_scan_result(const char* type_name, const char* content) {
    if (!g_scan_lbl_barcode) return;
    lv_label_set_text(g_scan_lbl_barcode, content ? content : "");
    if (g_scan_lbl_status && type_name) {
        lv_label_set_text_fmt(g_scan_lbl_status, "%s OK", type_name);
    }
}

void ui_show_photo_result(const char* status, const char* result) {
    if (g_photo_lbl_status && status) {
        lv_label_set_text(g_photo_lbl_status, status);
    }
    if (g_photo_lbl_result && result) {
        lv_label_set_text(g_photo_lbl_result, result);
    }
}

// Status-bar icon colors must be applied on the LVGL task, but
// ui_update_status() is called from loop()/MQTT contexts. Store the desired
// state here; ui_apply_status() (run by the LVGL task) applies it. This mirrors
// the cross-task pattern used for scans/results.
static portMUX_TYPE   g_status_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool  g_status_dirty = true;
static volatile bool  g_want_wifi = false;
static volatile bool  g_want_mqtt = false;

void ui_update_status(bool wifi_connected, bool mqtt_connected) {
    portENTER_CRITICAL(&g_status_mux);
    if (wifi_connected != g_want_wifi || mqtt_connected != g_want_mqtt
            || g_status_dirty) {
        g_want_wifi = wifi_connected;
        g_want_mqtt = mqtt_connected;
        g_status_dirty = true;
    }
    portEXIT_CRITICAL(&g_status_mux);
}

// Apply the pending status-bar colors. MUST be called from the LVGL task.
static void ui_apply_status(void) {
    if (!g_status_dirty) return;
    bool wifi_ok, mqtt_ok;
    portENTER_CRITICAL(&g_status_mux);
    wifi_ok = g_want_wifi;
    mqtt_ok = g_want_mqtt;
    g_status_dirty = false;
    portEXIT_CRITICAL(&g_status_mux);

    const lv_color_t green = { .full = 0x07E0 };
    const lv_color_t red   = { .full = 0xF800 };
    if (g_icon_wifi) {
        lv_obj_set_style_text_color(g_icon_wifi, wifi_ok ? green : red, 0);
        lv_obj_invalidate(g_icon_wifi);   // force repaint
    }
    if (g_icon_mqtt) {
        lv_obj_set_style_text_color(g_icon_mqtt, mqtt_ok ? green : red, 0);
        lv_obj_invalidate(g_icon_mqtt);
    }
}

// Color the [B] BLE icon from live BLE state (grey=off, red=waiting/advertising,
// green=connected). Cheap; called each LVGL tick. Only repaints on change.
static void ui_apply_ble_status(void) {
    if (!g_icon_ble) return;
    static int s_last = -1;
    int state = !ble_hid_is_started() ? 0 : (ble_hid_is_connected() ? 2 : 1);
    if (state == s_last) return;
    s_last = state;
    const lv_color_t grey  = { .full = 0x8410 };
    const lv_color_t red   = { .full = 0xF800 };
    const lv_color_t green = { .full = 0x07E0 };
    lv_color_t c = (state == 0) ? grey : (state == 2 ? green : red);
    lv_obj_set_style_text_color(g_icon_ble, c, 0);
    lv_obj_invalidate(g_icon_ble);
}

void ui_on_scan(const char* type_name, const char* content) {
    // Log every scan event to the SD work log, tagged with the current mode.
    // Runs on the LVGL task (Core 1), same core as the LCD flush, so the SD
    // write (shared FSPI bus) is serialized with display access. No-op if the
    // SD card is not mounted.
    static const char* const mode_tags[SCAN_MODE_COUNT] = {
        [SCAN_MODE_QUERY]      = "QUERY",
        [SCAN_MODE_INPUT_BLE]  = "INPUT-BLE",
        [SCAN_MODE_INPUT_MQTT] = "INPUT-MQTT",
        [SCAN_MODE_STOCK_IN]   = "IN",
        [SCAN_MODE_STOCK_OUT]  = "OUT",
        [SCAN_MODE_STOCKTAKE]  = "COUNT",
    };
    scan_log_add_entry(mode_tags[g_scan_mode], content);

    switch (g_scan_mode) {
        case SCAN_MODE_QUERY:
            // Query mode: show the scanned barcode on the result line as a
            // placeholder, then let the server's item details (posted by
            // mqtt_on_response via ui_post_result) replace it when the reply
            // arrives. Do NOT also call ui_show_scan_result here, which would
            // set "QR OK" and race with the item display.
            if (g_scan_lbl_barcode) {
                lv_label_set_text(g_scan_lbl_barcode, content ? content : "");
            }
            if (!mqtt_is_connected()) {
                if (g_scan_lbl_info) {
                    lv_label_set_text(g_scan_lbl_info, "MQTT offline — not sent");
                }
                break;
            }
            if (g_scan_lbl_info) {
                lv_label_set_text(g_scan_lbl_info, "Querying warehouse...");
            }
            if (!mqtt_query_barcode(content) && g_scan_lbl_info) {
                lv_label_set_text(g_scan_lbl_info, "Query send failed");
            }
            break;

        case SCAN_MODE_INPUT_BLE:
            // Wireless barcode gun: type the scanned value at the paired host's
            // cursor via BLE HID. BLE is started on entering this mode (see
            // ui_cycle_scan_mode / show_page), with WiFi paused.
            if (!ble_hid_is_connected()) {
                if (g_scan_lbl_info) {
                    lv_label_set_text(g_scan_lbl_info,
                        ble_hid_is_started() ? "BLE: pair host (SoftSnail)"
                                             : "BLE: starting...");
                }
                break;
            }
            ble_hid_type_line(content);
            if (g_scan_lbl_info) {
                lv_label_set_text(g_scan_lbl_info, "Sent via BLE keyboard");
            }
            break;

        case SCAN_MODE_INPUT_MQTT:
            // Push the scanned value to the server, which relays it to the Web
            // UI form field via SSE. Needs WiFi/MQTT up (this mode does NOT
            // start BLE, so WiFi stays connected).
            if (!mqtt_is_connected()) {
                if (g_scan_lbl_info) {
                    lv_label_set_text(g_scan_lbl_info, "MQTT offline — not sent");
                }
                break;
            }
            if (mqtt_scan_input(content, type_name)) {
                if (g_scan_lbl_info) {
                    lv_label_set_text(g_scan_lbl_info, "Pushed to Web UI");
                }
            } else if (g_scan_lbl_info) {
                lv_label_set_text(g_scan_lbl_info, "Push failed");
            }
            break;

        case SCAN_MODE_STOCK_IN:
            // Stock-in: each scan adds one unit (server increments quantity).
            if (g_scan_lbl_info) {
                lv_label_set_text(g_scan_lbl_info, "Stock-in...");
            }
            mqtt_stock_in(content, 1);
            break;

        case SCAN_MODE_STOCK_OUT:
            // Stock-out: each scan removes one unit.
            if (g_scan_lbl_info) {
                lv_label_set_text(g_scan_lbl_info, "Stock-out...");
            }
            mqtt_stock_out(content, 1);
            break;

        case SCAN_MODE_STOCKTAKE: {
            // Stocktake: a session must be open. Each scan of a barcode bumps
            // its running actual_qty by 1 and publishes the new count.
            if (!g_st_active || g_st_session_id == 0) {
                if (g_scan_lbl_stocktake) {
                    lv_label_set_text(g_scan_lbl_stocktake, "No session (long-press to set mode)");
                }
                break;
            }

            int idx = st_find_entry(content);
            if (idx < 0) {
                if (g_st_entry_count >= STOCKTAKE_MAX) {
                    if (g_scan_lbl_stocktake) {
                        lv_label_set_text(g_scan_lbl_stocktake, "Session full!");
                    }
                    break;
                }
                idx = g_st_entry_count++;
                strncpy(g_st_entries[idx].barcode, content,
                        sizeof(g_st_entries[0].barcode) - 1);
                g_st_entries[idx].barcode[sizeof(g_st_entries[0].barcode) - 1] = '\0';
                g_st_entries[idx].actual_qty = 0;
            }
            g_st_entries[idx].actual_qty++;

            if (g_scan_lbl_stocktake) {
                lv_label_set_text_fmt(g_scan_lbl_stocktake, "Count %s = %d",
                                      content, g_st_entries[idx].actual_qty);
            }
            mqtt_stocktake_scan(g_st_session_id, content, g_st_entries[idx].actual_qty);
            break;
        }

        default:
            break;
    }
}

// ─── Camera Preview ──────────────────────────────────────────────────────────

void ui_preview_start(void) {
    scan_preview_set_decode_cb(on_preview_decode);
    scan_preview_start(0, 0, 0, 0);  // geometry is fixed internally
}

void ui_preview_stop(void) {
    scan_preview_stop();
}

// Drain a pending decode result (called from the LVGL task on Core 1).
void ui_process_pending_scan(void) {
    // Apply any pending status-bar color change on the LVGL task.
    ui_apply_status();
    ui_apply_ble_status();

    // Drain a pending structured query-result item (posted by mqtt_on_response).
    // Switch to the RESULT page and populate its 6 fields. Runs on LVGL task.
    if (g_item_pending) {
        char barcode[128], name[64], spec[64], location[48], supplier[64], date[24];
        int qty;
        portENTER_CRITICAL(&g_item_mux);
        strncpy(barcode,  g_item_barcode,  sizeof(barcode));  barcode[sizeof(barcode)-1]  = '\0';
        strncpy(name,     g_item_name,     sizeof(name));     name[sizeof(name)-1]         = '\0';
        strncpy(spec,     g_item_spec,     sizeof(spec));     spec[sizeof(spec)-1]         = '\0';
        strncpy(location, g_item_location, sizeof(location)); location[sizeof(location)-1] = '\0';
        strncpy(supplier, g_item_supplier, sizeof(supplier)); supplier[sizeof(supplier)-1] = '\0';
        strncpy(date,     g_item_date,     sizeof(date));     date[sizeof(date)-1]         = '\0';
        qty = g_item_qty;
        g_item_pending = false;
        portEXIT_CRITICAL(&g_item_mux);

        ui_preview_stop();                 // stop LovyanGFX preview before leaving SCAN
        result_populate(barcode, name, spec, qty, location, supplier, date);
        show_page(UI_PAGE_RESULT);
    }

    // Drain a pending MQTT result first (posted from the MQTT/loop() context by
    // mqtt_on_response via ui_post_result). This must run in the LVGL task.
    if (g_result_pending) {
        char label[24];
        char content[128];
        portENTER_CRITICAL(&g_result_mux);
        strncpy(label, g_result_label, sizeof(label));
        strncpy(content, g_result_content, sizeof(content));
        label[sizeof(label) - 1] = '\0';
        content[sizeof(content) - 1] = '\0';
        g_result_pending = false;
        portEXIT_CRITICAL(&g_result_mux);

        if (strcmp(label, "STInfo") == 0) {
            // Stocktake start/scan feedback — shown BELOW the viewfinder so the
            // live preview does not paint over it.
            if (g_scan_lbl_stocktake) {
                lv_label_set_text(g_scan_lbl_stocktake, content);
            }
            if (g_inv_lbl_count) {
                lv_label_set_text_fmt(g_inv_lbl_count, "%d barcode(s) counted",
                                      g_st_entry_count);
            }
        } else if (strcmp(label, "STEnd") == 0) {
            // Stocktake session closed — update the below-viewfinder line and
            // the inventory page status.
            if (g_scan_lbl_stocktake) {
                lv_label_set_text(g_scan_lbl_stocktake, content);
            }
            if (g_inv_lbl_status) {
                lv_label_set_text(g_inv_lbl_status, content);
            }
        } else {
            ui_show_scan_result(label, content);
        }
    }

    if (!g_scan_pending) return;

    char type[16];
    char content[128];

    portENTER_CRITICAL(&g_scan_mux);
    strncpy(type, g_pending_type, sizeof(type));
    strncpy(content, g_pending_content, sizeof(content));
    type[sizeof(type) - 1] = '\0';
    content[sizeof(content) - 1] = '\0';
    g_scan_pending = false;
    portEXIT_CRITICAL(&g_scan_mux);

    // Only surface scans while the SCAN page is active.
    if (g_current_page != UI_PAGE_SCAN) return;

    ui_on_scan(type, content);
}
