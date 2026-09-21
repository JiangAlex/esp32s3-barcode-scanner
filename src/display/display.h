/**
 * @file display.h
 * @brief TFT display driver + LVGL initialization (LovyanGFX)
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>
#include <LovyanGFX.hpp>

/**
 * @brief Initialize TFT display and LVGL framework.
 *        Must be called before any LVGL operations.
 */
void display_init(void);

/**
 * @brief Set backlight brightness.
 * @param brightness 0-255 (0=off, 255=max)
 */
void display_set_brightness(uint8_t brightness);

/**
 * @brief Get the LVGL display pointer.
 */
lv_disp_t* display_get_disp(void);

/**
 * @brief Get the shared LovyanGFX device instance.
 *        Exposed so other modules can draw directly without re-initializing SPI.
 */
lgfx::LGFX_Device& display_get_tft(void);

/**
 * @brief Register an SPI mutex to serialize LCD access between LVGL flush
 *        and direct LovyanGFX callers (e.g. scan_preview render task).
 */
void display_set_spi_mutex(SemaphoreHandle_t mutex);

#endif /* DISPLAY_H */
