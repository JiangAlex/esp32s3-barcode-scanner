/**
 * @file qr_scanner.cpp
 * @brief QL3301L scan module driver (UART line reader + TRIG control).
 */

#include "qr_scanner.h"
#include "config/pinout.h"

#if HAS_UART_SCANNER

#include <Arduino.h>

// Hold TRIG LOW at most this long. Matches the module's default single-read
// time (5000 ms, config manual 3.1.2); the module stops on its own after that.
#define QR_TRIG_HOLD_MS     5000

// Longest decoded line kept. ui_main truncates scans to 127 chars anyway.
#define QR_LINE_MAX         160

static HardwareSerial& s_uart = Serial1;
static qr_scan_cb_t s_cb = nullptr;

static char     s_line[QR_LINE_MAX];
static size_t   s_len = 0;
static bool     s_overflow = false;

static bool     s_trig_active = false;
static uint32_t s_trig_start_ms = 0;

static void trig_release(void) {
    digitalWrite(QR_PIN_TRIG, HIGH);   // open-drain: HIGH = released (pulled up)
    s_trig_active = false;
}

void qr_scanner_init(void) {
    // TRIG is driven open-drain with the internal pull-up: the ESP32 only ever
    // pulls it LOW, never drives it HIGH. The module board also has its own
    // TRIG button to GND (QL3301L 集成手册 3.3.1: 10k pull-up + key to GND),
    // so a push-pull HIGH would short the GPIO when that button is pressed.
    // Open-drain lets both trigger sources coexist.
    digitalWrite(QR_PIN_TRIG, HIGH);   // latch "released" before enabling output
    pinMode(QR_PIN_TRIG, OUTPUT_OPEN_DRAIN | PULLUP);

    s_uart.begin(QR_UART_BAUD, SERIAL_8N1, QR_UART_RX, QR_UART_TX);
    Serial.printf("[QR] QL3301L UART RX=%d TX=%d %d 8N1, TRIG=%d\n",
                  QR_UART_RX, QR_UART_TX, QR_UART_BAUD, QR_PIN_TRIG);
}

void qr_scanner_set_cb(qr_scan_cb_t cb) {
    s_cb = cb;
    if (!cb && s_trig_active) trig_release();
}

void qr_scanner_trigger(void) {
    digitalWrite(QR_PIN_TRIG, LOW);
    s_trig_active = true;
    s_trig_start_ms = millis();
    Serial.println("[QR] trigger");
}

bool qr_scanner_poll(void) {
    bool delivered = false;

    while (s_uart.available() > 0) {
        uint8_t c = (uint8_t)s_uart.read();

        if (c == '\r' || c == '\n') {
            if (s_len == 0) continue;          // CRLF / blank line
            s_line[s_len] = '\0';
            if (s_overflow) {
                Serial.printf("[QR] line too long (>%d), truncated\n", QR_LINE_MAX - 1);
            }
            Serial.printf("[QR] scan: %s\n", s_line);
            if (s_trig_active) trig_release();  // read done
            if (s_cb) {
                s_cb("QL3301", s_line);
                delivered = true;
            }
            s_len = 0;
            s_overflow = false;
        } else if (c >= 0x20) {
            // Printable ASCII and UTF-8 / GBK multibyte bytes. Control bytes
            // (e.g. the 0x02 0x00 header of a config-command reply) are dropped.
            if (s_len < sizeof(s_line) - 1) s_line[s_len++] = (char)c;
            else s_overflow = true;
        }
    }

    if (s_trig_active && (millis() - s_trig_start_ms) >= QR_TRIG_HOLD_MS) {
        trig_release();
        Serial.println("[QR] trigger timeout (no code read)");
    }

    return delivered;
}

#else  // !HAS_UART_SCANNER — no-op stubs so callers need no #if

void qr_scanner_init(void) {}
void qr_scanner_set_cb(qr_scan_cb_t) {}
void qr_scanner_trigger(void) {}
bool qr_scanner_poll(void) { return false; }

#endif
