/**
 * @file ui_main.h
 * @brief LVGL multi-page UI framework
 *
 * Pages:
 *   - Home (menu)
 *   - Scan (camera preview + decode)
 *   - Generate (QR code generation)
 *   - History (scan log list)
 *   - Settings
 */

#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <lvgl.h>

// UI Pages
typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_SCAN,
    UI_PAGE_GENERATE,
    UI_PAGE_HISTORY,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT
} ui_page_t;

/**
 * @brief Initialize LVGL UI framework and create all pages.
 */
void ui_main_init(void);

/**
 * @brief Switch to a specific UI page.
 */
void ui_navigate_to(ui_page_t page);

/**
 * @brief Get current active page.
 */
ui_page_t ui_get_current_page(void);

/**
 * @brief Update scan result display on scan page.
 * @param type_name Barcode type string
 * @param content Decoded content
 */
void ui_show_scan_result(const char* type_name, const char* content);

/**
 * @brief Show warehouse item info on scan page.
 * @param name Item name
 * @param spec Specification
 * @param quantity Quantity
 * @param location Storage location
 * @param supplier Supplier
 */
void ui_show_item_info(const char* name, const char* spec, int quantity,
                       const char* location, const char* supplier);

/**
 * @brief Update network status display.
 * @param wifi_connected WiFi connected flag
 * @param mqtt_connected MQTT connected flag
 */
void ui_update_status(bool wifi_connected, bool mqtt_connected);

#endif /* UI_MAIN_H */
