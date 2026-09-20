/**
 * @file touch.cpp
 * @brief CST816D capacitive touch driver (I2C) implementation.
 *        Ported from the official Waveshare bsp_cst816 reference, adapted to
 *        use pin/address definitions from pinout.h and the project's I2C bus.
 */

#include "touch.h"
#include <Arduino.h>
#include <Wire.h>
#include "config/pinout.h"

// CST816 register map (from official bsp_cst816)
#define CST816_ID_REG           0xA7
#define CST816_TOUCH_NUM_REG    0x02
#define CST816_TOUCH_XH_REG     0x03
#define CST816_TOUCH_XL_REG     0x04
#define CST816_TOUCH_YH_REG     0x05
#define CST816_TOUCH_YL_REG     0x06

static uint16_t g_width  = TFT_WIDTH;
static uint16_t g_height = TFT_HEIGHT;
static uint8_t  g_rotation = 0;

static bool     g_touch_flag = false;
static uint16_t g_raw_x = 0;
static uint16_t g_raw_y = 0;

// ─── Low-level I2C register access ──────────────────────────────────────────

static bool cst816_read(uint8_t reg, uint8_t* data, uint8_t len) {
    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) {
        return false;
    }
    Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, len);
    for (uint8_t i = 0; i < len; i++) {
        if (!Wire.available()) return false;
        data[i] = Wire.read();
    }
    return true;
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool touch_init(uint16_t width, uint16_t height, uint8_t rotation) {
    g_width = width;
    g_height = height;
    g_rotation = rotation;

    // Ensure the shared I2C bus is initialized (safe if already begun).
    Wire.begin(I2C_SHARED_SDA, I2C_SHARED_SCL);
    Wire.setClock(I2C_FREQ);

    if (TOUCH_PIN_RST != -1) {
        pinMode(TOUCH_PIN_RST, OUTPUT);
        digitalWrite(TOUCH_PIN_RST, LOW);
        delay(200);
        digitalWrite(TOUCH_PIN_RST, HIGH);
        delay(300);
    }

    uint8_t id = 0;
    if (!cst816_read(CST816_ID_REG, &id, 1)) {
        Serial.println("[TOUCH] CST816 I2C read failed");
        return false;
    }
    if (id != TOUCH_CHIP_ID) {
        Serial.printf("[TOUCH] Unexpected chip ID: 0x%02X (expected 0x%02X)\n",
                      id, TOUCH_CHIP_ID);
        return false;
    }

    Serial.println("[TOUCH] CST816D initialized");
    return true;
}

void touch_read(void) {
    uint8_t touch_num = 0;
    if (!cst816_read(CST816_TOUCH_NUM_REG, &touch_num, 1) || touch_num == 0) {
        return;
    }

    uint8_t xh = 0, xl = 0, yh = 0, yl = 0;
    cst816_read(CST816_TOUCH_XH_REG, &xh, 1);
    cst816_read(CST816_TOUCH_XL_REG, &xl, 1);
    cst816_read(CST816_TOUCH_YH_REG, &yh, 1);
    cst816_read(CST816_TOUCH_YL_REG, &yl, 1);

    g_raw_x = (uint16_t)((xh & 0x0F) << 8) | xl;
    g_raw_y = (uint16_t)((yh & 0x0F) << 8) | yl;
    g_touch_flag = true;
}

bool touch_get_coordinates(uint16_t* x, uint16_t* y) {
    if (!g_touch_flag) {
        return false;
    }
    g_touch_flag = false;

    switch (g_rotation) {
        case 1:
            *x = g_raw_y;
            *y = g_height - 1 - g_raw_x;
            break;
        case 2:
            *x = g_width  - 1 - g_raw_x;
            *y = g_height - 1 - g_raw_y;
            break;
        case 3:
            *x = g_width - 1 - g_raw_y;
            *y = g_raw_x;
            break;
        default: // rotation 0 (portrait)
            *x = g_raw_x;
            *y = g_raw_y;
            break;
    }
    return true;
}
