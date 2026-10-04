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

// Runtime touch I2C address. Starts at the configured CST816 addr but the
// probe in touch_read() may switch it to whichever address actually responds
// (this board scans 0x7E, not the standard CST816 0x15 — likely a variant).
static uint8_t g_touch_addr = TOUCH_I2C_ADDR;

static bool cst816_read_at(uint8_t addr, uint8_t reg, uint8_t* data, uint8_t len) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(true) != 0) {
        return false;
    }
    Wire.requestFrom(addr, len);
    for (uint8_t i = 0; i < len; i++) {
        if (!Wire.available()) return false;
        data[i] = Wire.read();
    }
    return true;
}

static bool cst816_read(uint8_t reg, uint8_t* data, uint8_t len) {
    return cst816_read_at(g_touch_addr, reg, data, len);
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

    // ID handshake, bounded retry. The official Waveshare bsp_cst816 loops
    // `while (bsp_touch_init() == false)` — i.e. it retries the ID read until
    // the chip answers 0xB6, implying CST816 needs time/retries after power-up
    // before it responds. We retry for up to ~2 s across BOTH candidate
    // addresses (0x15 standard, 0x7E seen on this board's scan) and lock onto
    // whichever returns the expected ID. Bounded (not infinite) so a dead/
    // absent chip can't hang boot.
    const uint8_t cand[2] = { 0x15, 0x7E };
    uint32_t t0 = millis();
    bool found = false;
    while ((millis() - t0) < 2000 && !found) {
        for (int i = 0; i < 2; i++) {
            uint8_t id = 0;
            bool ok = cst816_read_at(cand[i], CST816_ID_REG, &id, 1);
            Serial.printf("[TOUCH] id probe addr=0x%02X read=%s id=0x%02X\n",
                          cand[i], ok ? "OK" : "FAIL", id);
            if (ok && id == TOUCH_CHIP_ID) {
                g_touch_addr = cand[i];
                found = true;
                Serial.printf("[TOUCH] CST816 found at 0x%02X (ID 0x%02X)\n", cand[i], id);
                break;
            }
        }
        if (!found) delay(100);
    }
    if (!found) {
        Serial.println("[TOUCH] CST816 not responding on 0x15/0x7E after 2s — enabling anyway (polled)");
    }
    return true;
}

void touch_read(void) {
    // DIAGNOSTIC/PROBE: the standard CST816 address 0x15 never ACKs on this
    // board, but a scan shows 0x7E. Probe both for the touch-count register
    // and log the raw outcome (success AND failure) so we can see which
    // address actually responds to a register read. Rate-limited to 1 Hz.
    static bool s_addr_locked = false;
    if (!s_addr_locked) {
        static uint32_t s_last_probe = 0;
        if (millis() - s_last_probe >= 1000) {
            s_last_probe = millis();
            const uint8_t cand[2] = { 0x15, 0x7E };
            for (int i = 0; i < 2; i++) {
                uint8_t n = 0;
                bool ok = cst816_read_at(cand[i], CST816_TOUCH_NUM_REG, &n, 1);
                Serial.printf("[TOUCH] probe addr=0x%02X read=%s num=%u\n",
                              cand[i], ok ? "OK" : "FAIL", n);
                if (ok && n >= 1 && n <= 5) {
                    g_touch_addr = cand[i];
                    s_addr_locked = true;
                    Serial.printf("[TOUCH] locked onto addr 0x%02X\n", cand[i]);
                }
            }
        }
    }

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
    Serial.printf("[TOUCH] down raw=(%u,%u) num=%u\n", g_raw_x, g_raw_y, touch_num);
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
