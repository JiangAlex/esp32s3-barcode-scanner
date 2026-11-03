/**
 * @file display_test.h
 * @brief Minimal ST7789T3 + LVGL init for the ANCS LCD test.
 *
 * Self-contained (no touch, no SPI-mutex, no scan_preview) — just enough to
 * render LVGL widgets on the Waveshare ESP32-S3-Touch-LCD-2 panel. Pin values
 * are copied from the main project's config/pinout.h.
 */
#ifndef DISPLAY_TEST_H
#define DISPLAY_TEST_H

#include <lvgl.h>

/** Initialize TFT (ST7789T3) + LVGL. Call once in setup(). */
void display_init(void);

/** Pump LVGL (call periodically; this test drives it from a FreeRTOS task). */
void display_task_handler(void);

#endif /* DISPLAY_TEST_H */
