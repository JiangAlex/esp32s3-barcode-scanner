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
#include <Arduino.h>
#include <LovyanGFX.hpp>

// Viewfinder dimensions (must fit in LCD, centered in 240x320)
#define VF_W   120
#define VF_H   90
#define VF_X   ((240 - VF_W) / 2)
#define VF_Y   ((320 - VF_H) / 2)

// Downscale factor from QVGA (320x240) to viewfinder
#define DOWN_SCALE_X  (320 / VF_W)   // 2
#define DOWN_SCALE_Y  (240 / VF_H)   // 2

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

// Convert 8-bit grayscale to RGB565
static inline uint16_t gray_to_rgb565(uint8_t g) {
    uint8_t r = g & 0xF8;
    uint8_t gg = g & 0xFC;
    uint8_t b = g << 3;
    return (r << 8) | (gg << 3) | (b >> 3);
}

// ─── Core 0: Frame Capture ────────────────────────────────────────────────────

static void capture_task(void* param) {
    (void)param;
    Serial.println("[PREVIEW] capture task started on Core 0");

    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
        s->set_framesize(s, FRAMESIZE_QVGA);
        Serial.printf("[PREVIEW] sensor: QVGA Grayscale, PID=0x%02x\n", s->id.PID);
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

        if (fb->format == PIXFORMAT_GRAYSCALE && fb->width == 320 && fb->height == 240) {
            // Downscale QVGA → viewfinder and convert GRAY → RGB565
            if (xSemaphoreTake(s_mutex_frame, pdMS_TO_TICKS(10)) == pdTRUE) {
                uint16_t* dst = (uint16_t*)s_framebuf;

                for (uint16_t dy = 0; dy < VF_H; dy++) {
                    uint16_t src_row = dy * DOWN_SCALE_Y;
                    uint8_t* src_row_ptr = fb->buf + src_row * 320;

                    for (uint16_t dx = 0; dx < VF_W; dx++) {
                        uint8_t gray = src_row_ptr[dx * DOWN_SCALE_X];
                        // Expand 8-bit gray to RGB565 using bit replication
                        // RRRRR = gray[7:3], GGGGGG = gray[7:2], BBBBB = gray[7:3]
                        uint16_t r = (gray << 3) | (gray >> 2);  // 5 bits: replicate top 5
                        uint16_t g = (gray << 2) | (gray >> 4);  // 6 bits: replicate top 6
                        uint16_t b = (gray << 3) | (gray >> 2);  // 5 bits: replicate top 5
                        dst[dy * VF_W + dx] = (r << 11) | (g << 5) | b;
                    }
                }
                xSemaphoreGive(s_mutex_frame);

                xSemaphoreGive(s_sem_frame);
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
        // Draw test pattern on first frame to verify display pipeline
        static bool test_drawn = false;
        if (!test_drawn) {
            if (s_spi_mutex) xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
            tft.startWrite();
            // Red bar (left 1/4)
            tft.fillRect(VF_X, VF_Y, VF_W/4, VF_H, 0xF800);
            // Green bar (second 1/4)
            tft.fillRect(VF_X + VF_W/4, VF_Y, VF_W/4, VF_H, 0x07E0);
            // Blue bar (third 1/4)
            tft.fillRect(VF_X + VF_W/2, VF_Y, VF_W/4, VF_H, 0x001F);
            // White bar (right 1/4)
            tft.fillRect(VF_X + VF_W*3/4, VF_Y, VF_W/4, VF_H, 0xFFFF);
            tft.endWrite();
            if (s_spi_mutex) xSemaphoreGive(s_spi_mutex);
            test_drawn = true;
            xSemaphoreGive(s_mutex_frame);
            continue;
        }

        // Draw the downscaled frame as a rectangle on the display
        if (s_spi_mutex) xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
        tft.startWrite();
        tft.setAddrWindow(VF_X, VF_Y, VF_W, VF_H);

        // Push all pixels at once
        tft.writePixels((uint16_t*)s_framebuf, VF_W * VF_H);
        tft.endWrite();
        if (s_spi_mutex) xSemaphoreGive(s_spi_mutex);

        // Draw black border first (creates contrast ring around viewfinder)
        uint16_t bdr = 3;
        tft.drawRect(VF_X - bdr, VF_Y - bdr, VF_W + bdr*2, VF_H + bdr*2, 0x0000);

        // Draw frame overlay brackets (thick white for max contrast on grayscale)
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

    // Start capture task on Core 0 (frame acquisition only)
    xTaskCreatePinnedToCore(capture_task, "preview_cap", 4096, NULL, 5, &s_task_capture, 0);

    // Start render task on Core 1 (LovyanGFX calls — safe on same core as LVGL)
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
