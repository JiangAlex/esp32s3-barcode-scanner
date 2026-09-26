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
#include <esp_task_wdt.h>

// ─── Quirc QR Code Decoder ──────────────────────────────────────────────────
// quirc is included via the esp32-camera library or can be added separately

extern "C" {
#include "quirc.h"
}

extern "C" {
#include "barcode1d.h"
}

static struct quirc* qr_decoder = nullptr;

// Reusable luma buffer for 1D scanning (QVGA max here; SVGA path allocates its
// own). Placed in PSRAM to avoid pressuring the ~320KB internal DRAM. Sized for
// QVGA 320x240; the SVGA pipeline (Task 6) uses a separate larger buffer.
#define LUMA_MAX_PX  (320 * 240)
static uint8_t* s_luma = nullptr;

// Convert an RGB565 frame to 8-bit luma into `dst` (Rec.601). Byte order matches
// the on-device layout confirmed for display (native uint16 read, no swap).
static void rgb565_to_luma(const uint16_t* src, uint8_t* dst, int npx) {
    for (int i = 0; i < npx; i++) {
        uint16_t px = src[i];
        uint8_t r = (px >> 11) & 0x1F;
        uint8_t g = (px >> 5)  & 0x3F;
        uint8_t b =  px        & 0x1F;
        uint16_t r8 = (r << 3) | (r >> 2);
        uint16_t g8 = (g << 2) | (g >> 4);
        uint16_t b8 = (b << 3) | (b >> 2);
        dst[i] = (uint8_t)((r8 * 77 + g8 * 150 + b8 * 29) >> 8);
    }
}

// Compute the Otsu threshold from a rectangular ROI of a luma image. Restricting
// the histogram to the centered ROI (where the user aims the code) prevents
// background clutter — table edges, fingers, glare — from skewing the split.
static int otsu_threshold_roi(const uint8_t* luma, int w, int h,
                              int rx, int ry, int rw, int rh) {
    if (rx < 0) rx = 0;
    if (ry < 0) ry = 0;
    if (rx + rw > w) rw = w - rx;
    if (ry + rh > h) rh = h - ry;
    if (rw <= 0 || rh <= 0) { rx = 0; ry = 0; rw = w; rh = h; }

    uint32_t hist[256] = {0};
    for (int y = ry; y < ry + rh; y++) {
        const uint8_t* row = luma + (long)y * w;
        for (int x = rx; x < rx + rw; x++) hist[row[x]]++;
    }
    uint32_t total = (uint32_t)rw * (uint32_t)rh;
    uint64_t sum = 0;
    for (int t = 0; t < 256; t++) sum += (uint64_t)t * hist[t];

    uint64_t sumB = 0;
    uint32_t wB = 0;
    double maxVar = 0.0;
    int threshold = 128;
    for (int t = 0; t < 256; t++) {
        wB += hist[t];
        if (wB == 0) continue;
        uint32_t wF = total - wB;
        if (wF == 0) break;
        sumB += (uint64_t)t * hist[t];
        double mB = (double)sumB / wB;
        double mF = (double)(sum - sumB) / wF;
        double diff = mB - mF;
        double var = (double)wB * (double)wF * diff * diff;
        if (var > maxVar) { maxVar = var; threshold = t; }
    }
    return threshold;
}

// Binarize `luma` (w*h) into the quirc image buffer at threshold `th`, then run
// identify + decode. Returns the decoded payload via out params on success.
// `*out_count` receives the number of QR capstone groups quirc identified, so
// the caller can avoid re-thresholding a scene that has no QR at all.
static bool qr_try_threshold(const uint8_t* luma, int w, int h, int th,
                             String* content, int* out_count) {
    uint8_t* image = quirc_begin(qr_decoder, nullptr, nullptr);
    if (!image) { if (out_count) *out_count = 0; return false; }
    int npx = w * h;
    for (int i = 0; i < npx; i++) image[i] = (luma[i] > th) ? 255 : 0;
    quirc_end(qr_decoder);

    int count = quirc_count(qr_decoder);
    if (out_count) *out_count = count;
    for (int i = 0; i < count; i++) {
        struct quirc_code code;
        struct quirc_data data;
        quirc_extract(qr_decoder, i, &code);
        if (quirc_decode(&code, &data) == QUIRC_SUCCESS) {
            *content = String((const char*)data.payload, data.payload_len);
            return true;
        }
    }
    return false;
}

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

    // Allocate the shared 1D luma buffer (PSRAM preferred).
    if (!s_luma) {
        s_luma = (uint8_t*)heap_caps_malloc(LUMA_MAX_PX, MALLOC_CAP_SPIRAM);
        if (!s_luma) s_luma = (uint8_t*)malloc(LUMA_MAX_PX);  // DRAM fallback
        if (!s_luma) Serial.println("[DECODE] WARN: 1D luma buffer alloc failed");
        else Serial.println("[DECODE] 1D decoder ready (EAN-13/UPC-A/Code128)");
    }
}

// Shared decode core: QR (ROI Otsu + multi-threshold retry) then 1D
// (multi-scanline). The caller guarantees the quirc buffer is sized to (w,h).
static DecodeResult decode_core(const uint8_t* luma, int w, int h) {
    DecodeResult result = {false, BARCODE_UNKNOWN, "", ""};

    // ── QR: ROI Otsu threshold + bracketed retries ──
    // QR only at QVGA. quirc_end (identify) cost scales with pixel count; at
    // SVGA it can run for seconds and starve IDLE0 → task watchdog abort. QR
    // already decodes reliably at QVGA, and the hi-res path exists for fine 1D
    // barcodes (which the fast line-scan handles), so skip QR when w*h is large.
    bool qr_allowed = (w * h <= 320 * 240);
    if (qr_decoder && qr_allowed) {
        int rw = (w * 7) / 10, rh = (h * 7) / 10;
        int rx = (w - rw) / 2, ry = (h - rh) / 2;
        int base = otsu_threshold_roi(luma, w, h, rx, ry, rw, rh);
        const int offs[] = {0, -20, +20, -40, +40};

        // Bound total QR effort by a time budget rather than a fixed pass count.
        // quirc_end is cheap with no candidates but runs a costly perspective
        // fit once candidates exist; on-device a marginal QR made all 5
        // thresholds fit → ~1200 ms/frame. A budget lets us try several
        // thresholds (preserving decode success) while capping worst-case frame
        // time so preview fps stays responsive.
        uint32_t qr_start_ms = millis();
        const uint32_t QR_BUDGET_MS = 350;

        for (unsigned k = 0; k < sizeof(offs) / sizeof(offs[0]); k++) {
            int th = base + offs[k];
            if (th < 1) th = 1;
            if (th > 254) th = 254;

            // Feed the watchdog and yield before each identify pass so a busy
            // scene (many finder-like candidates) can't starve IDLE0.
            esp_task_wdt_reset();
            vTaskDelay(1);

            String content;
            int qr_count = 0;
            if (qr_try_threshold(luma, w, h, th, &content, &qr_count)) {
                result.success = true;
                result.type = BARCODE_QR_CODE;
                result.content = content;
                result.type_name = "QR Code";
                beep_success();
                Serial.printf("[DECODE] QR (%dx%d th=%d): %s\n", w, h, th, content.c_str());
                return result;
            }

            // If the base threshold identified no QR candidates at all, this
            // frame has no QR — skip the remaining bracketed thresholds (which
            // would only help a present-but-marginal QR). This is the key guard
            // against multi-threshold identify starving the CPU on busy scenes.
            if (k == 0 && qr_count == 0) break;

            // Stop once the QR time budget is exhausted; the next frame retries.
            if ((millis() - qr_start_ms) >= QR_BUDGET_MS) break;
        }
    }

    // ── 1D: multi-scanline line-scan decoder ──
    {
        bc1d_result_t r1d;
        // More scanlines at hi-res improves the chance of hitting the barcode band.
        int n_lines = (h >= 480) ? 25 : 15;
        if (bc1d_decode_image(luma, w, h, w, n_lines, &r1d)) {
            result.success = true;
            result.content = String(r1d.text);
            result.type_name = bc1d_type_name(r1d.type);
            switch (r1d.type) {
                case BC1D_EAN_13:   result.type = BARCODE_EAN_13; break;
                case BC1D_UPC_A:    result.type = BARCODE_UPC_A;  break;
                case BC1D_CODE_128: result.type = BARCODE_CODE_128; break;
                default:            result.type = BARCODE_UNKNOWN; break;
            }
            beep_success();
            Serial.printf("[DECODE] 1D %s (%dx%d): %s\n",
                          result.type_name.c_str(), w, h, result.content.c_str());
            return result;
        }
    }

    return result;
}

DecodeResult barcode_decode(camera_fb_t* fb) {
    DecodeResult result = {false, BARCODE_UNKNOWN, "", ""};

    if (!fb || !fb->buf || fb->len == 0) {
        return result;
    }

    // Produce luma into the shared buffer (QVGA path). Both QR and 1D consume it.
    int W = fb->width, H = fb->height;
    if (!s_luma ||
        !(fb->format == PIXFORMAT_GRAYSCALE || fb->format == PIXFORMAT_RGB565) ||
        W * H > LUMA_MAX_PX) {
        return result;
    }
    if (fb->format == PIXFORMAT_GRAYSCALE) {
        memcpy(s_luma, fb->buf, (size_t)W * H);
    } else {
        rgb565_to_luma((const uint16_t*)fb->buf, s_luma, W * H);
    }

    // quirc buffer is sized to QVGA in init; the QVGA path matches directly.
    return decode_core(s_luma, W, H);
}

DecodeResult barcode_decode_luma(const uint8_t* luma, int w, int h) {
    DecodeResult result = {false, BARCODE_UNKNOWN, "", ""};
    if (!luma || w <= 0 || h <= 0) return result;

    // Resize the quirc buffer to the hi-res frame for QR geometry, decode, then
    // restore to QVGA so the live QVGA path keeps working.
    bool resized = false;
    if (qr_decoder && (w != 320 || h != 240)) {
        if (quirc_resize(qr_decoder, w, h) >= 0) resized = true;
        else Serial.println("[DECODE] hi-res quirc_resize failed; QR skipped");
    }

    result = decode_core(luma, w, h);

    if (resized) {
        if (quirc_resize(qr_decoder, 320, 240) < 0) {
            Serial.println("[DECODE] WARN: failed to restore quirc to QVGA");
        }
    }
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
