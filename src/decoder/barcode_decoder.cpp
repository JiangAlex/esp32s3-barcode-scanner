/**
 * @file barcode_decoder.cpp
 * @brief Barcode decoding engine implementation
 *
 * Uses quirc for QR Code decoding.
 * For other formats (Code128, EAN, DataMatrix), uses the ESP-IDF code scanner
 * component or a lightweight ZXing-inspired decoder.
 *
 * Note: The ESP-IDF esp_code_scanner component provides multi-format support
 *       on ESP32-S3. If not available, falls back to quirc (QR only).
 */

#include "barcode_decoder.h"
#include <Arduino.h>
#include "config/pinout.h"
#include "config/config.h"

// ─── Quirc QR Code Decoder ──────────────────────────────────────────────────
// quirc is included via the esp32-camera library or can be added separately

extern "C" {
#include "quirc.h"
}

static struct quirc* qr_decoder = nullptr;

// ─── Buzzer feedback (optional — no onboard buzzer on this board) ───────────

static void beep_success(void) {
#if BUZZER_ENABLED
    ledcSetup(BUZZER_PWM_CH, BUZZER_FREQ, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_PWM_CH);
    ledcWrite(BUZZER_PWM_CH, 128);
    delay(SCAN_BEEP_DURATION_MS);
    ledcWrite(BUZZER_PWM_CH, 0);
    ledcDetachPin(BUZZER_PIN);
#endif
}

// ─── Public functions ───────────────────────────────────────────────────────

void barcode_decoder_init(void) {
    // Initialize quirc for QR code decoding
    qr_decoder = quirc_new();
    if (qr_decoder) {
        // Resize to match camera frame size (QVGA = 320x240)
        if (quirc_resize(qr_decoder, 320, 240) < 0) {
            Serial.println("[DECODE] Failed to resize quirc buffer");
            quirc_destroy(qr_decoder);
            qr_decoder = nullptr;
        } else {
            Serial.println("[DECODE] Quirc QR decoder initialized (320x240)");
        }
    } else {
        Serial.println("[DECODE] Failed to create quirc instance");
    }
}

DecodeResult barcode_decode(camera_fb_t* fb) {
    DecodeResult result = {false, BARCODE_UNKNOWN, "", ""};

    if (!fb || !fb->buf || fb->len == 0) {
        return result;
    }

    // ── QR Code decoding with quirc ──
    if (qr_decoder && fb->format == PIXFORMAT_GRAYSCALE) {
        // Copy image data to quirc buffer
        uint8_t* image = quirc_begin(qr_decoder, nullptr, nullptr);
        if (image) {
            memcpy(image, fb->buf, fb->width * fb->height);
            quirc_end(qr_decoder);

            // Check for decoded QR codes
            int count = quirc_count(qr_decoder);
            for (int i = 0; i < count; i++) {
                struct quirc_code code;
                struct quirc_data data;

                quirc_extract(qr_decoder, i, &code);
                quirc_decode_error_t err = quirc_decode(&code, &data);

                if (err == QUIRC_SUCCESS) {
                    result.success = true;
                    result.type = BARCODE_QR_CODE;
                    result.content = String((const char*)data.payload, data.payload_len);
                    result.type_name = "QR Code";

                    // Buzzer feedback
                    beep_success();

                    Serial.printf("[DECODE] QR: %s\n", result.content.c_str());
                    return result;
                }
            }
        }
    }

    // ── TODO: Add Code128, EAN, DataMatrix decoding ──
    // Options:
    // 1. ESP-IDF esp_code_scanner component (if available)
    // 2. ZXing-cpp lightweight port
    // 3. Custom 1D barcode decoder for Code128/EAN

    return result;
}

const char* barcode_type_to_string(BarcodeType type) {
    switch (type) {
        case BARCODE_QR_CODE:       return "QR Code";
        case BARCODE_DATA_MATRIX:   return "DataMatrix";
        case BARCODE_CODE_128:      return "Code 128";
        case BARCODE_CODE_39:       return "Code 39";
        case BARCODE_EAN_13:        return "EAN-13";
        case BARCODE_EAN_8:         return "EAN-8";
        case BARCODE_UPC_A:         return "UPC-A";
        case BARCODE_UPC_E:         return "UPC-E";
        default:                    return "Unknown";
    }
}
