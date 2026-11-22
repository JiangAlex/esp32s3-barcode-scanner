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
static uint32_t s_rx_bytes = 0;      // total UART bytes received (diagnostic)
static uint32_t s_trig_start_ms = 0;

// Diagnostic: count TRIG level changes while the ESP32 is NOT driving it.
// Any edge here comes from the module side / wiring (noise, glitches),
// including pulses far shorter than a 'g' poll could see.
static volatile uint32_t s_idle_edges = 0;
static void IRAM_ATTR trig_edge_isr(void) { s_idle_edges++; }

// pinMode() resets the GPIO interrupt config, so re-arm after each change.
static void trig_idle_input(void) {
    pinMode(QR_PIN_TRIG, INPUT);
    attachInterrupt(digitalPinToInterrupt(QR_PIN_TRIG), trig_edge_isr, CHANGE);
}

static void trig_release(void) {
    // Back to high-impedance input: the ESP32 neither drives nor pulls the
    // line; the module's own pull-up (10k to 3.3V) keeps TRIG HIGH.
    trig_idle_input();
    s_trig_active = false;
}

void qr_scanner_init(void) {
    // TRIG idles as a plain INPUT (no pull-up, not driven), so outside a
    // trigger the ESP32 is electrically absent from the line. Only
    // qr_scanner_trigger() switches it to an open-drain output driven LOW
    // (never HIGH), so the module board's own TRIG key to GND cannot short it.
    trig_idle_input();

    s_uart.begin(QR_UART_BAUD, SERIAL_8N1, QR_UART_RX, QR_UART_TX);
    Serial.printf("[QR] QL3301L UART RX=%d TX=%d %d 8N1, TRIG=%d\n",
                  QR_UART_RX, QR_UART_TX, QR_UART_BAUD, QR_PIN_TRIG);
}

void qr_scanner_set_cb(qr_scan_cb_t cb) {
    s_cb = cb;
    if (!cb && s_trig_active) trig_release();
}

void qr_scanner_trigger(void) {
    detachInterrupt(digitalPinToInterrupt(QR_PIN_TRIG));   // our own edge, not noise
    pinMode(QR_PIN_TRIG, OUTPUT_OPEN_DRAIN);   // output only while triggering
    digitalWrite(QR_PIN_TRIG, LOW);
    s_trig_active = true;
    s_trig_start_ms = millis();
    Serial.println("[QR] trigger");
}

bool qr_scanner_poll(void) {
    bool delivered = false;

    while (s_uart.available() > 0) {
        uint8_t c = (uint8_t)s_uart.read();
        s_rx_bytes++;

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

void qr_scanner_debug(void) {
    Serial.printf("[QR] diag: TRIG(GPIO%d) level=%d driving_low=%d idle_edges=%lu | RX(GPIO%d) level=%d | uart_rx_bytes=%lu\n",
                  QR_PIN_TRIG, digitalRead(QR_PIN_TRIG), s_trig_active ? 1 : 0,
                  (unsigned long)s_idle_edges,
                  QR_UART_RX, digitalRead(QR_UART_RX), (unsigned long)s_rx_bytes);
}

#else  // !HAS_UART_SCANNER — no-op stubs so callers need no #if

void qr_scanner_init(void) {}
void qr_scanner_set_cb(qr_scan_cb_t) {}
void qr_scanner_trigger(void) {}
bool qr_scanner_poll(void) { return false; }
void qr_scanner_debug(void) {}

#endif
