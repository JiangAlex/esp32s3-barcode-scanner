/**
 * @file ui_main.h
 * @brief LVGL multi-page UI — single-button (BOOT key) navigation
 *
 * Navigation:
 *   - Short press (< 300ms): next item
 *   - Long press (> 1000ms): confirm/enter
 *   - Long hold (> 2000ms): go home (from any sub-page)
 */

#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <lvgl.h>

// ─── Page IDs ────────────────────────────────────────────────────────────────

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_SCAN,
    UI_PAGE_SETTINGS,
    UI_PAGE_INVENTORY,
    UI_PAGE_PHOTO,
    UI_PAGE_COUNT
} ui_page_t;

// ─── Scan Modes ──────────────────────────────────────────────────────────────

typedef enum {
    SCAN_MODE_QUERY = 0,   // scan → MQTT query → show result
    SCAN_MODE_INPUT,        // scan → BLE HID keyboard output
    SCAN_MODE_INVENTORY,    // scan → add to inventory list
    SCAN_MODE_COUNT
} scan_mode_t;

// ─── Navigation Events ────────────────────────────────────────────────────────

typedef enum {
    NAV_NONE = 0,
    NAV_NEXT,      // short press: advance to next item
    NAV_CONFIRM,   // long press: enter / confirm
    NAV_HOME       // long hold: return to home
} nav_event_t;

// ─── Menu Items (per page) ────────────────────────────────────────────────────

#define MENU_ITEMS_MAX   5

typedef struct {
    const char* label;        // display text (ASCII only for now)
    lv_obj_t*   btn;          // LVGL button widget
    lv_obj_t*   lbl;          // label inside button
} menu_item_t;

// ─── Page Data ────────────────────────────────────────────────────────────────

typedef struct {
    ui_page_t       id;
    const char*     title;     // page title (ASCII)
    lv_obj_t*       container; // page container widget
    menu_item_t*    items;    // array of menu items
    uint8_t         item_count;
    int8_t          focused;   // currently focused item index (-1 = no selection)
} page_data_t;

// ─── Public API ───────────────────────────────────────────────────────────────

/**
 * @brief Initialize UI — create all pages, set up single-button nav.
 */
void ui_main_init(void);

/**
 * @brief Feed a navigation event from the button handler.
 *        Call this from loop() when a button event occurs.
 */
void ui_nav_event(nav_event_t ev);

/**
 * @brief Get current active page.
 */
ui_page_t ui_get_current_page(void);

/**
 * @brief Update scan result text on scan page.
 */
void ui_show_scan_result(const char* type_name, const char* content);

/**
 * @brief Update status bar (WiFi/MQTT indicators).
 */
void ui_update_status(bool wifi_connected, bool mqtt_connected);

/**
 * @brief Get current scan mode.
 */
scan_mode_t ui_get_scan_mode(void);

/**
 * @brief Cycle to next scan mode.
 */
void ui_cycle_scan_mode(void);

/**
 * @brief Handle a completed barcode scan in the current mode.
 *        Dispatches to MQTT query, BLE HID output, or inventory add.
 */
void ui_on_scan(const char* type_name, const char* content);

/**
 * @brief Get current inventory item count.
 */
uint8_t ui_inventory_count(void);

/**
 * @brief Confirm and upload inventory batch (called from inventory page).
 */
void ui_inventory_upload(void);

/**
 * @brief Clear all inventory items.
 */
void ui_inventory_clear(void);

/**
 * @brief Update photo result text on photo page.
 * @param status Status message (e.g. "Uploading...", "Success", "Error")
 * @param result Recognition result text (e.g. "ABC123", "Product Name")
 */
void ui_show_photo_result(const char* status, const char* result);

/** Start/stop camera preview on SCAN page. Called when entering/leaving SCAN page. */
void ui_preview_start(void);
void ui_preview_stop(void);

/**
 * @brief Drain any pending barcode scan captured by the Core 0 preview task.
 *        Must be called from the LVGL task (Core 1) — it is the only place
 *        LVGL widgets may be safely updated. Call once per LVGL loop iteration.
 */
void ui_process_pending_scan(void);

#endif /* UI_MAIN_H */
