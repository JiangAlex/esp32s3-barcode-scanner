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

// SPI mutex for serializing LCD access between LVGL flush and scan_preview render
// Owned by scan_preview; register with display via display_set_spi_mutex()
extern SemaphoreHandle_t s_spi_mutex;

#endif
