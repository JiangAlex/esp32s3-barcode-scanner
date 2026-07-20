/**
 * @file barcode_generator.cpp
 * @brief QR Code generation using ricmoo/QRCode library + LVGL canvas rendering
 */

#include "barcode_generator.h"
#include <qrcode.h>

// QR Code version determines max data capacity
// Version 6 = 41x41 modules, can hold ~134 alphanumeric chars
#define QR_VERSION  6

void barcode_generator_init(void) {
    Serial.println("[GEN] Barcode generator initialized");
}

lv_obj_t* barcode_generate_qr(lv_obj_t* parent, const char* text, uint16_t size) {
    if (!text || strlen(text) == 0) {
        return nullptr;
    }

    // Create QR code data
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
    qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_MEDIUM, text);

    // Calculate module (pixel) size
    uint16_t module_size = size / qrcode.size;
    if (module_size < 1) module_size = 1;

    // Actual canvas size (aligned to modules)
    uint16_t canvas_size = module_size * qrcode.size;

    // Create LVGL canvas
    lv_obj_t* canvas = lv_canvas_create(parent);
    static lv_color_t* cbuf = nullptr;

    // Allocate canvas buffer in PSRAM
    size_t buf_size = LV_CANVAS_BUF_SIZE_TRUE_COLOR(canvas_size, canvas_size);
    cbuf = (lv_color_t*)ps_malloc(buf_size);
    if (!cbuf) {
        // Fallback to regular malloc
        cbuf = (lv_color_t*)malloc(buf_size);
    }

    if (!cbuf) {
        Serial.println("[GEN] Failed to allocate QR canvas buffer");
        lv_obj_del(canvas);
        return nullptr;
    }

    lv_canvas_set_buffer(canvas, cbuf, canvas_size, canvas_size, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    // Draw QR code modules
    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                // Draw black module
                for (uint16_t dy = 0; dy < module_size; dy++) {
                    for (uint16_t dx = 0; dx < module_size; dx++) {
                        lv_canvas_set_px_color(
                            canvas,
                            x * module_size + dx,
                            y * module_size + dy,
                            lv_color_black()
                        );
                    }
                }
            }
        }
    }

    lv_obj_set_size(canvas, canvas_size, canvas_size);
    lv_obj_center(canvas);

    Serial.printf("[GEN] QR generated: %dx%d modules, canvas %dx%d px\n",
                  qrcode.size, qrcode.size, canvas_size, canvas_size);

    return canvas;
}

void barcode_update_qr(lv_obj_t* canvas, const char* text) {
    if (!canvas || !text || strlen(text) == 0) return;

    // Get canvas dimensions
    lv_coord_t w = lv_obj_get_width(canvas);

    // Regenerate QR code
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(QR_VERSION)];
    qrcode_initText(&qrcode, qrcodeData, QR_VERSION, ECC_MEDIUM, text);

    uint16_t module_size = w / qrcode.size;
    if (module_size < 1) module_size = 1;

    // Clear canvas
    lv_canvas_fill_bg(canvas, lv_color_white(), LV_OPA_COVER);

    // Draw new QR code
    for (uint8_t y = 0; y < qrcode.size; y++) {
        for (uint8_t x = 0; x < qrcode.size; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                for (uint16_t dy = 0; dy < module_size; dy++) {
                    for (uint16_t dx = 0; dx < module_size; dx++) {
                        lv_canvas_set_px_color(
                            canvas,
                            x * module_size + dx,
                            y * module_size + dy,
                            lv_color_black()
                        );
                    }
                }
            }
        }
    }

    lv_obj_invalidate(canvas);
}
