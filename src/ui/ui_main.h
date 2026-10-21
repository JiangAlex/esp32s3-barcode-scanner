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
    UI_PAGE_RESULT,          // query result detail (6 fields)
    UI_PAGE_SETTINGS,
    UI_PAGE_INVENTORY,
    UI_PAGE_INVENTORY_LIST,  // stocktake accumulated list
    UI_PAGE_PHOTO,
    UI_PAGE_LOG,             // work log (placeholder in Phase A)
    UI_PAGE_ABOUT,           // firmware info (standalone page)
    UI_PAGE_COUNT
} ui_page_t;

// ─── Scan Modes ──────────────────────────────────────────────────────────────

typedef enum {
    SCAN_MODE_QUERY = 0,   // scan → MQTT query → show result
    SCAN_MODE_INPUT_BLE,    // scan → BLE HID keyboard output (wireless gun)
    SCAN_MODE_INPUT_MQTT,   // scan → MQTT scan-input → Web UI field (SSE relay)
    SCAN_MODE_STOCK_IN,     // scan → MQTT stock-in (warehouse/stock/in)
    SCAN_MODE_STOCK_OUT,    // scan → MQTT stock-out (warehouse/stock/out)
    SCAN_MODE_STOCKTAKE,    // scan → stocktake session scan (actual_qty count)
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
 * @brief Register a callback that opens the WiFi web-config portal. The
 *        settings page WiFi row (CONFIRM) invokes it; actual SSID/password/
 *        MQTT-broker configuration is done in the web portal, not on-device.
 */
typedef void (*ui_wifi_portal_cb_t)(void);
void ui_set_wifi_portal_cb(ui_wifi_portal_cb_t cb);

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
 * @brief Post a result to be shown on the scan page, SAFE to call from a
 *        non-LVGL context (e.g. the MQTT response callback). The LVGL task
 *        applies it on its next ui_process_pending_scan() tick.
 */
void ui_post_result(const char* label, const char* content);

/**
 * @brief Post a structured query-result item to be shown on the RESULT page.
 *        SAFE to call from a non-LVGL context (MQTT response callback). The
 *        LVGL task switches to the RESULT page and populates the 6 fields on
 *        its next ui_process_pending_scan() tick. Any field may be nullptr.
 */
void ui_post_result_item(const char* barcode, const char* name,
                         const char* spec, int quantity,
                         const char* location, const char* supplier,
                         const char* date_in);

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
 * @brief Inject a scan result from outside the camera path (e.g. a barcode
 *        typed over the serial console for testing). Thread-safe; takes the
 *        same cross-task path as a real decode. type_name may be nullptr.
 */
void ui_inject_scan(const char* type_name, const char* content);

/**
 * @brief Get current inventory item count.
 */
uint8_t ui_inventory_count(void);

// ─── Stocktake session (P3, Redmine #79) ────────────────────────────────────
// Firmware drives an asynchronous stocktake session that matches the server
// contract: start → (wait for session_id) → per-barcode scan with actual_qty →
// end. The INVENTORY page hosts the session UI.
//
// Flow:
//   1. Entering STOCKTAKE scan mode calls mqtt_stocktake_start().
//   2. Server replies "stocktake_started"; main.cpp calls
//      ui_stocktake_on_started(session_id) to arm the session.
//   3. Each scan in STOCKTAKE mode increments that barcode's local actual_qty
//      and publishes mqtt_stocktake_scan(session_id, barcode, actual_qty).
//   4. Server acks "stocktake_scanned"; main.cpp calls
//      ui_stocktake_on_scanned(...) to show system/actual/difference.
//   5. Long-press on the INVENTORY page calls ui_stocktake_end(), which
//      publishes mqtt_stocktake_end(session_id); server acks "stocktake_ended"
//      → ui_stocktake_on_ended().

/** @brief True while a stocktake session is open (session_id assigned). */
bool ui_stocktake_is_active(void);

/** @brief Request a new stocktake session (publishes start). */
void ui_stocktake_begin(void);

/** @brief End the current stocktake session (publishes end). */
void ui_stocktake_end(void);

/** @brief Server ack: session started; arms the session with its id. */
void ui_stocktake_on_started(int session_id);

/** @brief Server ack: a scan was recorded (system/actual/difference). */
void ui_stocktake_on_scanned(const char* name, int system_qty,
                             int actual_qty, int difference);

/** @brief Server ack: session closed. */
void ui_stocktake_on_ended(void);

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
