/**
 * @file ble_hid.h
 * @brief Thin BLE HID keyboard layer (NimBLE) — the "wireless barcode gun".
 *
 * Advertises as a BLE HID keyboard (device name BLE_DEVICE_NAME, "SoftSnail").
 * Once a host (PC/phone) pairs, scanned barcodes are typed at the cursor via
 * ble_hid_type_line() (US-layout ASCII), followed by Enter.
 *
 * Design notes:
 *  - Built directly on NimBLE-Arduino (no third-party keyboard fork) to avoid
 *    the arduino-esp32 3.x compatibility debt of ESP32-BLE-Keyboard.
 *  - On-demand: the BLE stack is started only while the INPUT scan mode is
 *    active (ble_hid_begin) and stopped on leaving it (ble_hid_stop), to keep
 *    DRAM use low and shorten the WiFi/BLE radio coexistence window.
 *  - type_line() must be called from a normal task (e.g. the LVGL task), never
 *    from an ISR.
 */

#ifndef BLE_HID_H
#define BLE_HID_H

#include <stdbool.h>

/** Start the BLE HID keyboard stack and begin advertising. Idempotent. */
void ble_hid_begin(void);

/** Stop advertising and release the BLE stack. Idempotent. */
void ble_hid_stop(void);

/**
 * @brief Register a callback invoked after ble_hid_stop() has torn down BLE,
 *        so the app can re-establish WiFi (which ble_hid_begin fully released
 *        to free DRAM for the BLE stack). May be nullptr.
 */
typedef void (*ble_hid_stopped_cb_t)(void);
void ble_hid_set_stopped_cb(ble_hid_stopped_cb_t cb);

/** True once ble_hid_begin() has initialized the stack (advertising or connected). */
bool ble_hid_is_started(void);

/** True while a host is connected (paired and link up). */
bool ble_hid_is_connected(void);

/**
 * @brief Type an ASCII string at the host cursor, then press Enter.
 *        No-op if not connected. Safe to call from the LVGL task.
 * @param text NUL-terminated ASCII (US layout); non-mappable chars are skipped.
 */
void ble_hid_type_line(const char* text);

#endif /* BLE_HID_H */
