/**
 * @file display.h
 * @brief TFT display driver + LVGL initialization (LovyanGFX)
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include <lvgl.h>

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

#endif /* DISPLAY_H */
