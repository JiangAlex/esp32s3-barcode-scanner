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
#include "config/config.h"
#include "scan_preview.h"

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
static lv_obj_t* g_page_settings = nullptr;
static lv_obj_t* g_page_inventory = nullptr;
static lv_obj_t* g_page_photo = nullptr;

static ui_page_t g_current_page = UI_PAGE_HOME;

// ─── Status bar icons ─────────────────────────────────────────────────────────

static lv_obj_t* g_icon_wifi = nullptr;
static lv_obj_t* g_icon_mqtt = nullptr;

// ─── HOME page menu items ─────────────────────────────────────────────────────

typedef enum {
    HOME_ITEM_SCAN = 0,
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

// ─── Scan mode state ─────────────────────────────────────────────────────────

static scan_mode_t g_scan_mode = SCAN_MODE_QUERY;

// ─── Inventory ────────────────────────────────────────────────────────────────

#define INVENTORY_MAX 50

typedef struct {
    char type[16];
    char content[128];
} inventory_item_t;

static inventory_item_t g_inventory_items[INVENTORY_MAX];
static uint8_t g_inventory_count = 0;

// ─── INVENTORY page widgets ───────────────────────────────────────────────────

static lv_obj_t* g_inv_lbl_count = nullptr;
static lv_obj_t* g_inv_lbl_status = nullptr;

// ─── PHOTO page widgets ───────────────────────────────────────────────────────

static lv_obj_t* g_photo_lbl_status = nullptr;
static lv_obj_t* g_photo_lbl_result = nullptr;

// ─── SETTINGS page widgets ───────────────────────────────────────────────────

static lv_obj_t* g_settings_slider = nullptr;
static lv_obj_t* g_settings_lbl_brightness = nullptr;

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
    lv_obj_align(g_icon_wifi, LV_ALIGN_RIGHT_MID, -52, 0);

    // MQTT icon
    g_icon_mqtt = lv_label_create(g_status_bar);
    lv_label_set_text(g_icon_mqtt, "[M]");
    lv_obj_set_style_text_color(g_icon_mqtt, (lv_color_t){ .full = 0xF800 }, 0);  // red = disconnected
    lv_obj_set_style_text_font(g_icon_mqtt, &lv_font_montserrat_12, 0);
    lv_obj_align(g_icon_mqtt, LV_ALIGN_RIGHT_MID, -24, 0);

    // Pages
    create_home_page();
    create_scan_page();
    create_settings_page();
    create_inventory_page();
    create_photo_page();

    // Start at home
    show_page(UI_PAGE_HOME);

    Serial.println("[UI] initialized: HOME / SCAN / SETTINGS / INVENTORY / PHOTO");
}

// ─── Show/hide page ───────────────────────────────────────────────────────────

static void hide_all_pages(void) {
    if (g_page_home)       lv_obj_add_flag(g_page_home, LV_OBJ_FLAG_HIDDEN);
    if (g_page_scan)      lv_obj_add_flag(g_page_scan, LV_OBJ_FLAG_HIDDEN);
    if (g_page_settings)  lv_obj_add_flag(g_page_settings, LV_OBJ_FLAG_HIDDEN);
    if (g_page_inventory) lv_obj_add_flag(g_page_inventory, LV_OBJ_FLAG_HIDDEN);
    if (g_page_photo)     lv_obj_add_flag(g_page_photo, LV_OBJ_FLAG_HIDDEN);
}

static void show_page(ui_page_t page) {
    hide_all_pages();
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
            // Animate slide-in
            anim_page_slide_in(g_page_scan);
            ui_preview_start();
            break;
        case UI_PAGE_SETTINGS:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_settings, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_settings);
            break;
        case UI_PAGE_INVENTORY:
            lv_obj_clear_flag(g_page_inventory, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_inventory);
            if (g_inv_lbl_count) {
                lv_label_set_text_fmt(g_inv_lbl_count, "%d items scanned", g_inventory_count);
            }
            if (g_inv_lbl_status) {
                lv_label_set_text(g_inv_lbl_status, "Press BOOT to upload");
            }
            break;
        case UI_PAGE_PHOTO:
            ui_preview_stop();
            lv_obj_clear_flag(g_page_photo, LV_OBJ_FLAG_HIDDEN);
            g_at_home = false;
            anim_page_slide_in(g_page_photo);
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
    lv_obj_set_scrollbar_mode(g_page_home, LV_SCROLLBAR_MODE_OFF);

    const char* labels[HOME_ITEM_COUNT] = {
        [HOME_ITEM_SCAN]     = "[ Scan ]",
        [HOME_ITEM_SETTINGS] = "[ Settings ]",
        [HOME_ITEM_ABOUT]    = "[ About ]"
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
        lv_obj_set_style_text_font(it->lbl, &lv_font_montserrat_16, 0);
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
        case HOME_ITEM_SETTINGS:
            show_page(UI_PAGE_SETTINGS);
            break;
        case HOME_ITEM_ABOUT:
            // About: show firmware version on scan page, then return home
            if (g_scan_lbl_status) {
                lv_label_set_text(g_scan_lbl_status, "Firmware Info");
            }
            if (g_scan_lbl_barcode) {
                lv_label_set_text_fmt(g_scan_lbl_barcode, "v%s", FW_VERSION);
            }
            if (g_scan_lbl_info) {
                lv_label_set_text(g_scan_lbl_info, DEVICE_ID);
            }
            show_page(UI_PAGE_SCAN);
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
    lv_obj_set_style_text_font(g_scan_lbl_status, &lv_font_montserrat_16, 0);
    lv_obj_align(g_scan_lbl_status, LV_ALIGN_TOP_MID, 0, 26);

    // Barcode result
    g_scan_lbl_barcode = lv_label_create(g_page_scan);
    lv_label_set_text(g_scan_lbl_barcode, "");
    lv_obj_set_style_text_color(g_scan_lbl_barcode, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_scan_lbl_barcode, &lv_font_montserrat_14, 0);
    lv_obj_align(g_scan_lbl_barcode, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_width(g_scan_lbl_barcode, 220);

    // Info line
    g_scan_lbl_info = lv_label_create(g_page_scan);
    lv_label_set_text(g_scan_lbl_info, "Long-press cycles mode");
    lv_obj_set_style_text_color(g_scan_lbl_info, (lv_color_t){ .full = 0x8410 }, 0); // dim grey
    lv_obj_set_style_text_font(g_scan_lbl_info, &lv_font_montserrat_12, 0);
    lv_obj_align(g_scan_lbl_info, LV_ALIGN_TOP_MID, 0, 95);
}

// ─── SETTINGS page ────────────────────────────────────────────────────────────

static void create_settings_page(void) {
    g_page_settings = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_page_settings, SCR_W, SCR_H - STATUS_H);
    lv_obj_set_pos(g_page_settings, 0, STATUS_H);
    lv_obj_set_style_bg_color(g_page_settings, COL_BG, 0);
    lv_obj_set_style_radius(g_page_settings, 0, 0);
    lv_obj_set_scrollbar_mode(g_page_settings, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(g_page_settings, LV_OBJ_FLAG_HIDDEN);

    // Brightness label
    g_settings_lbl_brightness = lv_label_create(g_page_settings);
    lv_label_set_text(g_settings_lbl_brightness, "Brightness");
    lv_obj_set_style_text_color(g_settings_lbl_brightness, COL_TEXT, 0);
    lv_obj_set_style_text_font(g_settings_lbl_brightness, &lv_font_montserrat_14, 0);
    lv_obj_align(g_settings_lbl_brightness, LV_ALIGN_TOP_LEFT, 20, 20);

    // Slider
    g_settings_slider = lv_slider_create(g_page_settings);
    lv_obj_set_size(g_settings_slider, 200, 30);
    lv_obj_align(g_settings_slider, LV_ALIGN_TOP_LEFT, 20, 50);
    lv_slider_set_range(g_settings_slider, 10, 255);
    lv_slider_set_value(g_settings_slider, DISP_BL_DEFAULT, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g_settings_slider, COL_PANEL, 0);
    lv_obj_set_style_bg_color(g_settings_slider, COL_SLIDER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g_settings_slider, COL_FOCUS, LV_PART_KNOB);
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
    lv_label_set_text(lbl, "Inventory Batch");
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 15);

    // Item count
    g_inv_lbl_count = lv_label_create(g_page_inventory);
    lv_label_set_text(g_inv_lbl_count, "0 items scanned");
    lv_obj_set_style_text_color(g_inv_lbl_count, (lv_color_t){ .full = 0x07E0 }, 0);
    lv_obj_set_style_text_font(g_inv_lbl_count, &lv_font_montserrat_12, 0);
    lv_obj_align(g_inv_lbl_count, LV_ALIGN_TOP_MID, 0, 45);

    // Status line
    g_inv_lbl_status = lv_label_create(g_page_inventory);
    lv_label_set_text(g_inv_lbl_status, "Press BOOT to upload");
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
            if (ev == NAV_CONFIRM) {
                // Placeholder: toggle a scanning flag
            }
            break;
        }

        case UI_PAGE_SETTINGS: {
            if (ev == NAV_CONFIRM) {
                // Apply brightness
                if (g_settings_slider) {
                    int val = lv_slider_get_value(g_settings_slider);
                    Serial.printf("[UI] brightness=%d\n", val);
                }
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
        [SCAN_MODE_QUERY]     = "[QUERY]",
        [SCAN_MODE_INPUT]     = "[INPUT]",
        [SCAN_MODE_INVENTORY] = "[INV]"
    };
    if (g_scan_lbl_mode) {
        lv_label_set_text(g_scan_lbl_mode, mode_names[g_scan_mode]);
    }
}

uint8_t ui_inventory_count(void) {
    return g_inventory_count;
}

void ui_inventory_upload(void) {
    if (g_inv_lbl_status) {
        lv_label_set_text(g_inv_lbl_status, "Uploading...");
    }
    // TODO: implement upload logic
}

void ui_inventory_clear(void) {
    g_inventory_count = 0;
    if (g_inv_lbl_count) {
        lv_label_set_text_fmt(g_inv_lbl_count, "0 items scanned");
    }
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

void ui_update_status(bool wifi_connected, bool mqtt_connected) {
    if (g_icon_wifi) {
        lv_obj_set_style_text_color(g_icon_wifi,
            wifi_connected
                ? (lv_color_t){ .full = 0x07E0 }   // green = connected
                : (lv_color_t){ .full = 0xF800 },   // red = disconnected
            0);
    }
    if (g_icon_mqtt) {
        lv_obj_set_style_text_color(g_icon_mqtt,
            mqtt_connected
                ? (lv_color_t){ .full = 0x07E0 }
                : (lv_color_t){ .full = 0xF800 },
            0);
    }
}

void ui_on_scan(const char* type_name, const char* content) {
    ui_show_scan_result(type_name, content);

    switch (g_scan_mode) {
        case SCAN_MODE_INVENTORY:
            if (g_inventory_count < INVENTORY_MAX) {
                strncpy(g_inventory_items[g_inventory_count].type, type_name, sizeof(g_inventory_items[0].type) - 1);
                strncpy(g_inventory_items[g_inventory_count].content, content, sizeof(g_inventory_items[0].content) - 1);
                g_inventory_count++;
                if (g_inv_lbl_count) {
                    lv_label_set_text_fmt(g_inv_lbl_count, "%d items scanned", g_inventory_count);
                }
            }
            break;
        default:
            break;
    }
}

// ─── Camera Preview ──────────────────────────────────────────────────────────

void ui_preview_start(void) {
    scan_preview_start(0, 0, 0, 0);  // geometry is fixed internally
}

void ui_preview_stop(void) {
    scan_preview_stop();
}
