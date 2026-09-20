/**
 * @file touch.h
 * @brief CST816D capacitive touch driver (I2C) for the Waveshare
 *        ESP32-S3-Touch-LCD-2. Provides a simple polled interface used by
 *        the LVGL pointer input device registered in display.cpp.
 */

#ifndef TOUCH_H
#define TOUCH_H

#include <stdint.h>

/**
 * @brief Initialize the CST816D touch controller over the shared I2C bus.
 * @param width   Active display width in pixels (post-rotation).
 * @param height  Active display height in pixels (post-rotation).
 * @param rotation Display rotation (0-3); used to map raw coordinates.
 * @return true if the CST816 chip is detected, false otherwise.
 */
bool touch_init(uint16_t width, uint16_t height, uint8_t rotation);

/**
 * @brief Poll the controller for the latest touch sample.
 *        Call once per LVGL read cycle before touch_get_coordinates().
 */
void touch_read(void);

/**
 * @brief Retrieve the most recent touch coordinates (rotation-adjusted).
 * @param x Out: X coordinate.
 * @param y Out: Y coordinate.
 * @return true if a new touch was available since the last read.
 */
bool touch_get_coordinates(uint16_t* x, uint16_t* y);

#endif /* TOUCH_H */
