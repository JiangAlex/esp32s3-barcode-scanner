/**
 * @file power.cpp
 * @brief Low-power mode management — backlight dimming and screen off
 */

#include "power.h"
#include "display/display.h"
#include "config/config.h"

static uint32_t g_last_activity_ms = 0;
static bool g_is_idle = false;
static uint8_t g_normal_brightness = POWER_NORMAL_BRIGHTNESS;

void power_init(void) {
    g_last_activity_ms = millis();
    g_is_idle = false;
    g_normal_brightness = DISP_BL_DEFAULT;
    Serial.printf("[POWER] init: dim=%dms off=%dms brightness=%d\n",
                  POWER_DIM_DELAY_MS, POWER_OFF_DELAY_MS, g_normal_brightness);
}

void power_update_idle_time(void) {
    g_last_activity_ms = millis();
    if (g_is_idle) {
        power_wake();
    }
}

void power_sleep_if_idle(void) {
    uint32_t elapsed = millis() - g_last_activity_ms;

    if (elapsed >= POWER_OFF_DELAY_MS) {
        // Screen off
        if (!g_is_idle) {
            display_set_brightness(0);
            g_is_idle = true;
            Serial.println("[POWER] screen off");
        }
    } else if (elapsed >= POWER_DIM_DELAY_MS) {
        // Dimmed
        if (!g_is_idle) {
            display_set_brightness(POWER_DIM_BRIGHTNESS);
            g_is_idle = true;
            Serial.println("[POWER] dimmed");
        }
    }
}

bool power_is_idle(void) {
    return g_is_idle;
}

void power_wake(void) {
    if (g_is_idle) {
        display_set_brightness(g_normal_brightness);
        g_is_idle = false;
        Serial.println("[POWER] wake");
    }
    g_last_activity_ms = millis();
}
