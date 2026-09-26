/**
 * @file barcode_decoder.h
 * @brief Barcode decoding engine — supports QR Code, DataMatrix, Code128, EAN/UPC
 */

#ifndef BARCODE_DECODER_H
#define BARCODE_DECODER_H

#include <Arduino.h>
#include <esp_camera.h>

// Barcode types
enum BarcodeType {
    BARCODE_UNKNOWN = 0,
    BARCODE_QR_CODE,
    BARCODE_DATA_MATRIX,
    BARCODE_CODE_128,
    BARCODE_CODE_39,
    BARCODE_EAN_13,
    BARCODE_EAN_8,
    BARCODE_UPC_A,
    BARCODE_UPC_E,
};

// Decode result
struct DecodeResult {
    bool success;
    BarcodeType type;
    String content;
    String type_name;       // Human-readable type name
};

/**
 * @brief Initialize barcode decoder engine.
 */
void barcode_decoder_init(void);

/**
 * @brief Decode barcodes from a camera frame buffer.
 * @param fb Camera frame buffer (grayscale QVGA)
 * @return DecodeResult with success flag, type, and content
 */
DecodeResult barcode_decode(camera_fb_t* fb);

/**
 * @brief Decode from a standalone grayscale (luma) buffer of arbitrary size.
 *
 * Used by the high-resolution single-shot pipeline (SVGA). Resizes the quirc
 * buffer to (w,h) for QR, applies ROI Otsu + multi-threshold retry, and runs
 * the 1D multi-scanline decoder. Restores the quirc buffer to QVGA afterwards.
 *
 * @param luma  grayscale buffer, w*h bytes
 * @param w     width
 * @param h     height
 * @return DecodeResult
 */
DecodeResult barcode_decode_luma(const uint8_t* luma, int w, int h);

/**
 * @brief Get human-readable name for barcode type.
 */
const char* barcode_type_to_string(BarcodeType type);

#endif /* BARCODE_DECODER_H */
