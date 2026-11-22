/**
 * @file qr_scanner.h
 * @brief External QL3301L barcode/QR scan module over UART (TTL-232).
 *
 * The module decodes 1D/2D codes itself and sends the decoded text over UART,
 * terminated by CR (0x0D) by default (9600 8N1). A read is started by pulling
 * TRIG (PIN12) LOW; in the factory default "manual, level trigger" mode the
 * module reads while TRIG is held LOW and stops on success or timeout.
 *
 * Only built/used on boards with HAS_UART_SCANNER (see config/pinout.h).
 * Reference: docs/QL3301 (QL3301L集成手册, 小模块配置手册_V2.0.3).
 */

#ifndef QR_SCANNER_H
#define QR_SCANNER_H

#include <stdint.h>

// Same signature as scan_preview's decode callback, so the UI can feed both
// sources into one pipeline (ui_main.cpp on_preview_decode).
typedef void (*qr_scan_cb_t)(const char* type_name, const char* content);

// Open the UART and park TRIG HIGH (idle). Call once from setup().
void qr_scanner_init(void);

// Set the receiver for decoded lines. NULL = discard incoming lines
// (e.g. while the SCAN page is not shown).
void qr_scanner_set_cb(qr_scan_cb_t cb);

// Start one read: drive TRIG LOW until a line arrives or QR_TRIG_HOLD_MS passes.
void qr_scanner_trigger(void);

// Service the UART and the TRIG timeout. Call often from loop().
// Returns true if a decoded line was delivered during this call.
bool qr_scanner_poll(void);

// True while TRIG is held LOW (a read is in progress).
bool qr_scanner_is_triggering(void);

#endif /* QR_SCANNER_H */
