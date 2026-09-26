#ifndef SCAN_PREVIEW_H
#define SCAN_PREVIEW_H

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Start/stop the scan preview task.
//
// Architecture:
//   - Core 0: capture task — grabs grayscale QVGA frames, downscales to
//              viewfinder size, converts GRAY→RGB565, writes to shared DRAM buffer.
//   - Core 1: render task — receives semaphore signal, copies from shared buffer,
//              draws via LovyanGFX (safe on same core as LVGL, no SPI queue contention).
//
// This two-task design avoids the xQueueGenericSend assert that occurred when
// LovyanGFX was called directly from Core 0 (Arduino SPI HAL queue is Core 1-local).
void scan_preview_start(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void scan_preview_stop(void);
bool scan_preview_is_running(void);

// Decode result callback. Invoked from the Core 0 capture task when a barcode
// is successfully decoded (subject to SCAN_COOLDOWN_MS debounce).
//
// IMPORTANT: the callback runs on Core 0. Do NOT call LVGL APIs directly from
// it — LVGL must only be touched on Core 1. Marshal the result to the LVGL
// task (e.g. copy into a pending buffer consumed by the LVGL task).
//
// Strings are owned by the caller's stack and are only valid during the call;
// copy them if you need to keep them.
typedef void (*scan_decode_cb_t)(const char* type_name, const char* content);
void scan_preview_set_decode_cb(scan_decode_cb_t cb);

// Request a one-shot hi-res (SVGA 800x600) decode on the next capture-task
// iteration that has no live-QVGA decode. User-triggered (long-press CONFIRM on
// the SCAN page in QUERY/INPUT mode) — helps small/fine 1D product-label
// barcodes that QVGA can't resolve. Safe to call from any task.
void scan_preview_request_hires(void);

// SPI mutex for serializing LCD access between LVGL flush and scan_preview render
// Owned by scan_preview; register with display via display_set_spi_mutex()
extern SemaphoreHandle_t s_spi_mutex;

#endif
