/**
 * @file ui_main.cpp
 * @brief LVGL multi-page UI framework implementation
 */

#include "ui_main.h"
#include <Arduino.h>
#include "config/config.h"

// ─── UI state ───────────────────────────────────────────────────────────────

static ui_page_t current_page = UI_PAGE_HOME;
static lv_obj_t* pages[UI_PAGE_COUNT] = {nullptr};
static lv_obj_t* tabview = nullptr;

// Scan page widgets
static lv_obj_t* lbl_scan_status = nullptr;
static lv_obj_t* lbl_scan_result = nullptr;
static lv_obj_t* lbl_item_info = nullptr;

// Status bar widgets
static lv_obj_t* lbl_wifi_status = nullptr;
static lv_obj_t* lbl_mqtt_status = nullptr;

// ─── Page builders ──────────────────────────────────────────────────────────

static void create_home_page(lv_obj_t* parent) {
    // Title
    lv_obj_t* lbl_title = lv_label_create(parent);
    lv_label_set_text(lbl_title, BOARD_NAME);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 10);

    // Version
    lv_obj_t* lbl_ver = lv_label_create(parent);
    lv_label_set_text_fmt(lbl_ver, "v%s", FW_VERSION);
    lv_obj_align(lbl_ver, LV_ALIGN_TOP_MID, 0, 35);

    // Status indicators
    lbl_wifi_status = lv_label_create(parent);
    lv_label_set_text(lbl_wifi_status, LV_SYMBOL_WIFI " WiFi: ---");
    lv_obj_align(lbl_wifi_status, LV_ALIGN_TOP_LEFT, 10, 60);

    lbl_mqtt_status = lv_label_create(parent);
    lv_label_set_text(lbl_mqtt_status, LV_SYMBOL_UPLOAD " MQTT: ---");
    lv_obj_align(lbl_mqtt_status, LV_ALIGN_TOP_LEFT, 10, 80);
}

static void create_scan_page(lv_obj_t* parent) {
    // Scan status
    lbl_scan_status = lv_label_create(parent);
    lv_label_set_text(lbl_scan_status, LV_SYMBOL_EYE_OPEN " Scanning...");
    lv_obj_set_style_text_font(lbl_scan_status, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl_scan_status, LV_ALIGN_TOP_MID, 0, 5);

    // Scan result area
    lbl_scan_result = lv_label_create(parent);
    lv_label_set_text(lbl_scan_result, "Point camera at barcode");
    lv_label_set_long_mode(lbl_scan_result, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_scan_result, 220);
    lv_obj_align(lbl_scan_result, LV_ALIGN_TOP_MID, 0, 30);

    // Item info area
    lbl_item_info = lv_label_create(parent);
    lv_label_set_text(lbl_item_info, "");
    lv_label_set_long_mode(lbl_item_info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(lbl_item_info, 220);
    lv_obj_align(lbl_item_info, LV_ALIGN_TOP_MID, 0, 80);
}

static void create_generate_page(lv_obj_t* parent) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, LV_SYMBOL_EDIT " QR Code Generator");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 5);

    // Text input area (placeholder)
    lv_obj_t* ta = lv_textarea_create(parent);
    lv_textarea_set_placeholder_text(ta, "Enter text to encode...");
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, 200);
    lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 35);

    // QR display area will be created dynamically by barcode_generate_qr()
}

static void create_history_page(lv_obj_t* parent) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, LV_SYMBOL_LIST " Scan History");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 5);

    // History list (placeholder — populated at runtime)
    lv_obj_t* list = lv_list_create(parent);
    lv_obj_set_size(list, 230, 240);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 30);
}

static void create_settings_page(lv_obj_t* parent) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 5);

    // Brightness slider
    lv_obj_t* lbl_bl = lv_label_create(parent);
    lv_label_set_text(lbl_bl, "Backlight:");
    lv_obj_align(lbl_bl, LV_ALIGN_TOP_LEFT, 10, 40);

    lv_obj_t* slider = lv_slider_create(parent);
    lv_slider_set_range(slider, 10, 255);
    lv_slider_set_value(slider, DISP_BL_DEFAULT, LV_ANIM_ON);
    lv_obj_set_width(slider, 180);
    lv_obj_align(slider, LV_ALIGN_TOP_LEFT, 10, 65);

    // Device info
    lv_obj_t* lbl_info = lv_label_create(parent);
    lv_label_set_text_fmt(lbl_info,
        "Device: %s\n"
        "FW: v%s\n"
        "MQTT: %s:%d",
        DEVICE_ID, FW_VERSION, MQTT_BROKER, MQTT_PORT
    );
    lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 10, 100);
}

// ─── Public functions ───────────────────────────────────────────────────────

void ui_main_init(void) {
    // Create tabview as main navigation
    tabview = lv_tabview_create(lv_scr_act(), LV_DIR_BOTTOM, 40);

    // Create tabs
    pages[UI_PAGE_HOME] = lv_tabview_add_tab(tabview, LV_SYMBOL_HOME);
    pages[UI_PAGE_SCAN] = lv_tabview_add_tab(tabview, LV_SYMBOL_EYE_OPEN);
    pages[UI_PAGE_GENERATE] = lv_tabview_add_tab(tabview, LV_SYMBOL_EDIT);
    pages[UI_PAGE_HISTORY] = lv_tabview_add_tab(tabview, LV_SYMBOL_LIST);
    pages[UI_PAGE_SETTINGS] = lv_tabview_add_tab(tabview, LV_SYMBOL_SETTINGS);

    // Build each page
    create_home_page(pages[UI_PAGE_HOME]);
    create_scan_page(pages[UI_PAGE_SCAN]);
    create_generate_page(pages[UI_PAGE_GENERATE]);
    create_history_page(pages[UI_PAGE_HISTORY]);
    create_settings_page(pages[UI_PAGE_SETTINGS]);

    current_page = UI_PAGE_HOME;
    Serial.println("[UI] Pages created: Home, Scan, Generate, History, Settings");
}

void ui_navigate_to(ui_page_t page) {
    if (page < UI_PAGE_COUNT) {
        lv_tabview_set_act(tabview, (uint32_t)page, LV_ANIM_ON);
        current_page = page;
    }
}

ui_page_t ui_get_current_page(void) {
    return current_page;
}

void ui_show_scan_result(const char* type_name, const char* content) {
    if (lbl_scan_status) {
        lv_label_set_text_fmt(lbl_scan_status, LV_SYMBOL_OK " %s", type_name);
    }
    if (lbl_scan_result) {
        lv_label_set_text(lbl_scan_result, content);
    }
}

void ui_show_item_info(const char* name, const char* spec, int quantity,
                       const char* location, const char* supplier) {
    if (lbl_item_info) {
        lv_label_set_text_fmt(lbl_item_info,
            "Name: %s\n"
            "Spec: %s\n"
            "Qty: %d\n"
            "Loc: %s\n"
            "Supplier: %s",
            name ? name : "-",
            spec ? spec : "-",
            quantity,
            location ? location : "-",
            supplier ? supplier : "-"
        );
    }
}

void ui_update_status(bool wifi_connected, bool mqtt_connected) {
    if (lbl_wifi_status) {
        lv_label_set_text_fmt(lbl_wifi_status, LV_SYMBOL_WIFI " WiFi: %s",
                              wifi_connected ? "Connected" : "Disconnected");
    }
    if (lbl_mqtt_status) {
        lv_label_set_text_fmt(lbl_mqtt_status, LV_SYMBOL_UPLOAD " MQTT: %s",
                              mqtt_connected ? "Connected" : "Disconnected");
    }
}
