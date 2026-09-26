/**
 * @file scan_preview.cpp
 * @brief Live camera viewfinder — captures grayscale QVGA frames,
 *        downscales to viewfinder size, converts GRAY→RGB565,
 *        and draws to LCD via LovyanGFX on Core 1.
 *
 * Architecture: Core 0 captures frames to a shared PSRAM buffer.
 *               Core 1 (the same core as LVGL) renders via LovyanGFX,
 *               signaled by a semaphore. This avoids cross-core SPI
 *               queue contention that occurs when LovyanGFX is called
 *               from Core 0 (xQueueGenericSend assert).
 */

#include "scan_preview.h"
#include "display/display.h"
#include "camera/camera.h"
#include "decoder/barcode_decoder.h"
#include "config/config.h"
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <esp_heap_caps.h>

// Viewfinder dimensions (must fit in LCD, centered in 240x320)
#define VF_W   200
#define VF_H   150
#define VF_X   ((240 - VF_W) / 2)
#define VF_Y   ((320 - VF_H) / 2)

// Source camera frame — always-on SVGA (see config.h CAM_FRAME_SIZE). Decoding
// uses this full resolution; the preview is downsampled from it.
#define SRC_W  800
#define SRC_H  600

// Hi-res decode buffer size (== source frame). Kept for the luma buffer alloc.
#define HIRES_W            800
#define HIRES_H            600
#define HIRES_LUMA_PX      (HIRES_W * HIRES_H)

// Fixed-point (16.16) sampling steps for non-integer downscale.
// SVGA 800x600 → VF 200x150 is a 4x reduction on both axes.
#define STEP_X  ((SRC_W << 16) / VF_W)   // src px per dst px, 16.16
#define STEP_Y  ((SRC_H << 16) / VF_H)

// ─── Shared state (protected by semaphore) ───────────────────────────────────

static TaskHandle_t s_task_capture = nullptr;  // Core 0 capture task
static TaskHandle_t s_task_render  = nullptr;   // Core 1 render task
static volatile bool s_running = false;

// Shared downscaled framebuffer (in DRAM — accessible from both cores)
// One complete viewfinder frame in RGB565 format.
static uint8_t* s_framebuf = nullptr;

// Semaphore to signal "frame ready" from capture to render task
static SemaphoreHandle_t s_sem_frame = nullptr;

// Mutex to protect shared framebuffer read/write
static SemaphoreHandle_t s_mutex_frame = nullptr;

// SPI mutex to serialize LCD access between LVGL flush and render_task
// Created here (owner), registered with display via display_set_spi_mutex()
SemaphoreHandle_t s_spi_mutex = nullptr;

// Decode callback (invoked from Core 0 capture task) + one-time decoder init flag
static scan_decode_cb_t s_decode_cb = nullptr;
static bool s_decoder_ready = false;

// SVGA luma buffer (PSRAM). Allocated lazily on the first hi-res attempt.
static uint8_t* s_hires_luma = nullptr;
static bool     s_af_available = false;   // set by scan_preview_start from probe
static volatile bool s_hires_requested = false;  // set by scan_preview_request_hires()

// Convert 8-bit grayscale to RGB565
static inline uint16_t gray_to_rgb565(uint8_t g) {
    uint8_t r = g & 0xF8;
    uint8_t gg = g & 0xFC;
    uint8_t b = g << 3;
    return (r << 8) | (gg << 3) | (b >> 3);
}

// RGB565 → 8-bit luma (Rec.601), native uint16 read (byte order matches panel).
static void rgb565_to_luma_local(const uint16_t* src, uint8_t* dst, int npx) {
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

// Decode the current SVGA frame (already the always-on capture size). No mode
// switching — the camera is initialized at SVGA, so `fb` is 800x600 RGB565.
// Converts to luma and runs the full decoder (QR downsampled + 1D full-res).
// Returns true if a code was decoded (delivered via s_decode_cb). Core 0.
static bool decode_current_frame(camera_fb_t* fb) {
    if (!s_hires_luma) {
        s_hires_luma = (uint8_t*)heap_caps_malloc(HIRES_LUMA_PX, MALLOC_CAP_SPIRAM);
        if (!s_hires_luma) {
            Serial.println("[PREVIEW] decode: luma alloc failed");
            return false;
        }
    }

    if (!fb || fb->format != PIXFORMAT_RGB565 ||
        fb->width != HIRES_W || fb->height != HIRES_H) {
        Serial.printf("[PREVIEW] decode: unexpected frame %ux%u fmt=%u len=%u\n",
                      fb ? fb->width : 0, fb ? fb->height : 0,
                      fb ? fb->format : 0, fb ? fb->len : 0);
        return false;
    }

    rgb565_to_luma_local((const uint16_t*)fb->buf, s_hires_luma, HIRES_W * HIRES_H);
    DecodeResult res = barcode_decode_luma(s_hires_luma, HIRES_W, HIRES_H);
    if (res.success && res.content.length() > 0 && s_decode_cb) {
        s_decode_cb(res.type_name.c_str(), res.content.c_str());
        Serial.println("[PREVIEW] scan: DECODED");
        return true;
    }
    return false;   // per-frame misses are silent; the window logs the outcome
}

// ─── Core 0: Frame Capture ────────────────────────────────────────────────────

static void capture_task(void* param) {
    (void)param;
    Serial.println("[PREVIEW] capture task started on Core 0");

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_pixformat(s, PIXFORMAT_RGB565);
        s->set_framesize(s, FRAMESIZE_SVGA);
        Serial.printf("[PREVIEW] sensor: SVGA RGB565, PID=0x%02x\n", s->id.PID);
    }

    camera_fb_t* fb = nullptr;
    uint32_t cap_count = 0;

    while (s_running) {
        fb = esp_camera_fb_get();
        if (!fb) {
            delay(10);
            continue;
        }

        cap_count++;

        // Debug: print first frame info
        if (cap_count == 1) {
            Serial.printf("[PREVIEW] fb len=%u w=%u h=%u fmt=%u pix[0]=%02x pix[1]=%02x pix[2]=%02x\n",
                fb->len, fb->width, fb->height, fb->format, fb->buf[0], fb->buf[1], fb->buf[2]);
        }

        if (fb->format == PIXFORMAT_RGB565 && fb->width == SRC_W && fb->height == SRC_H) {
            // Downscale QVGA → viewfinder (fixed-point 16.16 sampling). Source is
            // RGB565, 2 bytes/pixel. Verified on-device: the camera's byte layout
            // matches the panel, so copy each pixel verbatim (no swap).
            if (xSemaphoreTake(s_mutex_frame, pdMS_TO_TICKS(10)) == pdTRUE) {
                uint16_t* dst = (uint16_t*)s_framebuf;

                uint32_t src_y = 0;  // 16.16
                for (uint16_t dy = 0; dy < VF_H; dy++) {
                    uint8_t* src_row_ptr = fb->buf + (src_y >> 16) * SRC_W * 2;  // 2 bytes/px
                    uint32_t src_x = 0;  // 16.16

                    for (uint16_t dx = 0; dx < VF_W; dx++) {
                        // Read the pixel as a native uint16_t. The official demo
                        // feeds fb->buf straight to LVGL without byte-swapping,
                        // so treat it as little-endian here too.
                        uint32_t sx = (src_x >> 16);
                        dst[dy * VF_W + dx] = ((uint16_t*)src_row_ptr)[sx];
                        src_x += STEP_X;
                    }
                    src_y += STEP_Y;
                }
                xSemaphoreGive(s_mutex_frame);

                xSemaphoreGive(s_sem_frame);
            }
        }

        // ── Decode is user-triggered; try several frames until one succeeds ──
        // A short press starts an attempt window. Because the lens is fixed-
        // focus, individual frames vary in sharpness with hand motion — most
        // yield no QR finder patterns, but within a few frames one is usually
        // sharp enough. So we decode consecutive frames until success or the
        // window (max frames / max time) expires. Preview keeps updating between
        // attempts since each loop still runs the downscale above.
        static int      s_attempts_left = 0;
        static uint32_t s_attempt_deadline = 0;
        const int       HIRES_MAX_ATTEMPTS = 8;
        const uint32_t  HIRES_MAX_MS = 2000;

        if (s_hires_requested) {
            s_hires_requested = false;
            s_attempts_left = HIRES_MAX_ATTEMPTS;
            s_attempt_deadline = millis() + HIRES_MAX_MS;
            Serial.println("[PREVIEW] scan: starting multi-frame attempt");
        }

        if (s_decode_cb && s_attempts_left > 0 &&
            fb->format == PIXFORMAT_RGB565 &&
            fb->width == SRC_W && fb->height == SRC_H) {
            s_attempts_left--;
            bool ok = decode_current_frame(fb);
            if (ok || millis() >= s_attempt_deadline) {
                if (!ok) Serial.println("[PREVIEW] scan: window expired, no code");
                s_attempts_left = 0;
            }
        }

        esp_camera_fb_return(fb);
        fb = nullptr;

        delay(20);
    }

    Serial.println("[PREVIEW] capture task stopped");
    vTaskDelete(NULL);
}

// ─── Core 1: Render (LovyanGFX — same core as LVGL, no SPI contention) ────────

static void render_task(void* param) {
    (void)param;
    Serial.println("[PREVIEW] render task started on Core 1");

    lgfx::LGFX_Device& tft = display_get_tft();
    uint32_t frame_count = 0;
    uint32_t start_ms = millis();

    while (s_running) {
        // Wait for a frame to become available (with timeout)
        if (xSemaphoreTake(s_sem_frame, pdMS_TO_TICKS(200)) != pdTRUE) {
            continue;
        }

    // Copy frame from shared buffer (lock-protected)
    if (xSemaphoreTake(s_mutex_frame, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Serialize ALL LCD access against LVGL's flush_cb for the entire draw
        // sequence. LVGL runs in a different task on the same core; if we split
        // the image push and the overlay draws across separate startWrite/
        // endWrite spans (or leave overlays unguarded), LVGL's flush can
        // interleave its own SPI transaction and trip the Arduino SPI HAL's
        // xQueueGenericSend assert. One lock + one startWrite/endWrite avoids it.
        if (s_spi_mutex) xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
        tft.startWrite();

        // Camera image. Confirmed on-device: the camera's RGB565 layout matches
        // the panel's expected order, so push verbatim (no byte swap).
        tft.setAddrWindow(VF_X, VF_Y, VF_W, VF_H);
        tft.writePixels((uint16_t*)s_framebuf, VF_W * VF_H);

        // Black border (contrast ring around viewfinder)
        uint16_t bdr = 3;
        tft.drawRect(VF_X - bdr, VF_Y - bdr, VF_W + bdr*2, VF_H + bdr*2, 0x0000);

        // Frame overlay brackets (thick white for max contrast on grayscale)
        uint16_t col = 0xFFFF;  // white (max brightness)
        uint16_t thick = 5;      // thicker strokes
        uint16_t arm = 36;      // longer arms

        // Top-left corner — horizontal then vertical
        tft.drawFastHLine(VF_X, VF_Y, arm, col);
        tft.drawFastHLine(VF_X, VF_Y + 1, arm, col);
        tft.drawFastHLine(VF_X, VF_Y + 2, arm, col);
        tft.drawFastVLine(VF_X, VF_Y, arm, col);
        tft.drawFastVLine(VF_X + 1, VF_Y, arm, col);
        tft.drawFastVLine(VF_X + 2, VF_Y, arm, col);

        // Top-right corner
        tft.drawFastHLine(VF_X + VF_W - arm, VF_Y, arm, col);
        tft.drawFastHLine(VF_X + VF_W - arm, VF_Y + 1, arm, col);
        tft.drawFastHLine(VF_X + VF_W - arm, VF_Y + 2, arm, col);
        tft.drawFastVLine(VF_X + VF_W - thick, VF_Y, arm, col);
        tft.drawFastVLine(VF_X + VF_W - thick + 1, VF_Y, arm, col);
        tft.drawFastVLine(VF_X + VF_W - thick + 2, VF_Y, arm, col);

        // Bottom-left corner
        tft.drawFastHLine(VF_X, VF_Y + VF_H - thick, arm, col);
        tft.drawFastHLine(VF_X, VF_Y + VF_H - thick + 1, arm, col);
        tft.drawFastHLine(VF_X, VF_Y + VF_H - thick + 2, arm, col);
        tft.drawFastVLine(VF_X, VF_Y + VF_H - arm, arm, col);
        tft.drawFastVLine(VF_X + 1, VF_Y + VF_H - arm, arm, col);
        tft.drawFastVLine(VF_X + 2, VF_Y + VF_H - arm, arm, col);

        // Bottom-right corner
        tft.drawFastHLine(VF_X + VF_W - arm, VF_Y + VF_H - thick, arm, col);
        tft.drawFastHLine(VF_X + VF_W - arm, VF_Y + VF_H - thick + 1, arm, col);
        tft.drawFastHLine(VF_X + VF_W - arm, VF_Y + VF_H - thick + 2, arm, col);
        tft.drawFastVLine(VF_X + VF_W - thick, VF_Y + VF_H - arm, arm, col);
        tft.drawFastVLine(VF_X + VF_W - thick + 1, VF_Y + VF_H - arm, arm, col);
        tft.drawFastVLine(VF_X + VF_W - thick + 2, VF_Y + VF_H - arm, arm, col);

        // ── Scan line animation ──
        // A green line sweeps up and down inside the viewfinder. Each rendered
        // frame overwrites the whole VF image, so the previous line position is
        // erased naturally — we only need to draw it at its current position.
        {
            static int16_t line_y   = 0;     // offset within VF (0 .. VF_H-1)
            static int8_t  line_dir = 1;      // +1 down, -1 up
            const uint16_t scan_col = 0x07E0; // green
            const int16_t  step     = 4;      // px per frame

            int16_t y = VF_Y + line_y;
            // Draw a 2px-thick line, inset horizontally so it sits inside the frame.
            tft.drawFastHLine(VF_X + 4, y,     VF_W - 8, scan_col);
            tft.drawFastHLine(VF_X + 4, y + 1, VF_W - 8, scan_col);

            line_y += line_dir * step;
            if (line_y >= VF_H - 2) { line_y = VF_H - 2; line_dir = -1; }
            else if (line_y <= 0)   { line_y = 0;         line_dir =  1; }
        }

        tft.endWrite();
        if (s_spi_mutex) xSemaphoreGive(s_spi_mutex);

        xSemaphoreGive(s_mutex_frame);
    }

        frame_count++;
        if (frame_count % 20 == 0) {
            uint32_t elapsed = millis() - start_ms;
            Serial.printf("[PREVIEW] %.1f fps (frame %lu)\n",
                (float)(frame_count * 1000) / elapsed, frame_count);
        }
    }

    Serial.println("[PREVIEW] render task stopped");
    s_task_render = nullptr;
    vTaskDelete(NULL);
}

// ─── Public API ───────────────────────────────────────────────────────────────

void scan_preview_start(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    (void)x; (void)y; (void)w; (void)h;  // Fixed geometry
    if (s_running) return;

    // One-time barcode decoder init (quirc buffer sized to QVGA 320x240)
    if (!s_decoder_ready) {
        barcode_decoder_init();
        s_decoder_ready = true;
    }

    // Use AF in the hi-res path only if the startup probe confirmed an AF lens.
    s_af_available = camera_af_is_available();
    Serial.printf("[PREVIEW] hi-res AF %s\n", s_af_available ? "enabled" : "disabled (fixed-focus)");

    // Allocate shared framebuffer in DRAM (VF_W * VF_H * 2 bytes)
    if (!s_framebuf) {
        s_framebuf = (uint8_t*)malloc(VF_W * VF_H * 2);
        if (!s_framebuf) {
            Serial.println("[PREVIEW] FAILED to allocate shared framebuffer");
            return;
        }
    }

    // Create synchronization primitives
    if (!s_sem_frame) {
        s_sem_frame = xSemaphoreCreateBinary();
    }
    if (!s_mutex_frame) {
        s_mutex_frame = xSemaphoreCreateMutex();
    }
    // Create SPI mutex and register with display to guard LVGL flush
    if (!s_spi_mutex) {
        s_spi_mutex = xSemaphoreCreateMutex();
        display_set_spi_mutex(s_spi_mutex);
    }

    s_running = true;

    // Start capture task on Core 0.
    // Stack is 32KB: barcode_decode() → quirc. quirc_decode() (Reed-Solomon
    // error correction + a ~8KB quirc_data struct on the stack) needs far more
    // than the identify stage; 16KB overflowed the moment a real QR was found.
    xTaskCreatePinnedToCore(capture_task, "preview_cap", 32768, NULL, 5, &s_task_capture, 0);

    // Start render task on Core 1 (LovyanGFX calls — safe on same core as LVGL).
    // No decoding here, so 4KB is sufficient.
    xTaskCreatePinnedToCore(render_task, "preview_disp", 4096, NULL, 3, &s_task_render, 1);

    Serial.println("[PREVIEW] started (capture=Core0, render=Core1)");
}

void scan_preview_stop(void) {
    if (!s_running) return;
    s_running = false;

    // Wait for both tasks to stop
    if (s_task_capture) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        s_task_capture = nullptr;
    }
    if (s_task_render) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
        s_task_render = nullptr;
    }

    // Clean up shared buffer
    if (s_framebuf) {
        free(s_framebuf);
        s_framebuf = nullptr;
    }

    Serial.println("[PREVIEW] stopped");
}

bool scan_preview_is_running(void) {
    return s_running;
}

void scan_preview_set_decode_cb(scan_decode_cb_t cb) {
    s_decode_cb = cb;
}

void scan_preview_request_hires(void) {
    s_hires_requested = true;
}
