/**
 * @file power.h
 * @brief Low-power mode management — backlight dimming and screen off
 * 
 * Behavior:
 *   - Normal: backlight at current brightness
 *   - After 30s idle: dim backlight to POWER_DIM_BRIGHTNESS
 *   - After 60s idle: set backlight to 0 (screen off)
 *   - Wake on any BOOT button press or touch event
 */

#ifndef POWER_H
#define POWER_H

#include <Arduino.h>
#include <stdint.h>

// Idle thresholds (milliseconds)
#define POWER_DIM_DELAY_MS    30000   // 30 seconds  — dim backlight
#define POWER_OFF_DELAY_MS    60000   // 60 seconds  — screen off

// Brightness levels
#define POWER_DIM_BRIGHTNESS  20      // Dimmed backlight (0-255)
#define POWER_NORMAL_BRIGHTNESS DISP_BL_DEFAULT  // Restored from config

/**
 * @brief Initialize power management.
 *        Call once during setup() after display_init().
 */
void power_init(void);

/**
 * @brief Call from loop() on any user input (button or touch).
 *        Resets idle timer and restores brightness if needed.
 */
void power_update_idle_time(void);

/**
 * @brief Call from loop() every iteration.
 *        Checks idle time and dims/off the display as needed.
 */
void power_sleep_if_idle(void);

/**
 * @brief Check if display is currently in low-power (dim/off) state.
 */
bool power_is_idle(void);

/**
 * @brief Restore brightness to normal (call on wake).
 */
void power_wake(void);

#endif /* POWER_H */
