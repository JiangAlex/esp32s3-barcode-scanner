/**
 * @file barcode_generator.h
 * @brief QR Code / barcode generation and display on LVGL canvas
 */

#ifndef BARCODE_GENERATOR_H
#define BARCODE_GENERATOR_H

#include <Arduino.h>
#include <lvgl.h>

/**
 * @brief Initialize barcode generator module.
 */
void barcode_generator_init(void);

/**
 * @brief Generate and display a QR Code on an LVGL canvas.
 * @param parent LVGL parent object to place the QR code
 * @param text Text to encode in the QR code
 * @param size Canvas size in pixels (width = height)
 * @return Pointer to the created LVGL canvas object, or NULL on failure
 */
lv_obj_t* barcode_generate_qr(lv_obj_t* parent, const char* text, uint16_t size);

/**
 * @brief Update an existing QR code canvas with new text.
 * @param canvas Existing LVGL canvas object
 * @param text New text to encode
 */
void barcode_update_qr(lv_obj_t* canvas, const char* text);

#endif /* BARCODE_GENERATOR_H */
