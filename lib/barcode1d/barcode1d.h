/**
 * @file barcode1d.h
 * @brief Lightweight 1D barcode decoder (line-scan) for embedded targets.
 *
 * Pure C, no dynamic allocation. Decodes a single horizontal luma scanline
 * (8-bit grayscale) into a barcode string. Designed for product-label
 * scanning on ESP32-S3 where memory and flash are constrained; ZXing/ZBar
 * are too heavy.
 *
 * Supported symbologies (added incrementally):
 *   - EAN-13, UPC-A   (Task 1)
 *   - Code128         (Task 2)
 *
 * Usage:
 *   Feed one luma row (width samples) via bc1d_decode_line(). The decoder
 *   converts the row to bar/space run-lengths internally and attempts each
 *   supported symbology. Returns true on the first successful decode with a
 *   verified checksum.
 */

#ifndef BARCODE1D_H
#define BARCODE1D_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BC1D_NONE = 0,
    BC1D_EAN_13,
    BC1D_UPC_A,
    BC1D_CODE_128,
} bc1d_type_t;

typedef struct {
    bool        ok;             // true if a valid code was decoded
    bc1d_type_t type;           // which symbology matched
    char        text[48];       // decoded payload (NUL-terminated)
    int         length;         // strlen(text)
} bc1d_result_t;

/**
 * Decode a single luma scanline.
 *
 * @param luma   pointer to width 8-bit grayscale samples (one row)
 * @param width  number of samples in the row
 * @param out    result (populated on success)
 * @return true if a barcode was decoded with a valid checksum
 */
bool bc1d_decode_line(const uint8_t* luma, int width, bc1d_result_t* out);

/**
 * Decode by sampling N evenly-spaced horizontal scanlines across an image.
 * Returns on the first successful line. Suited to viewfinder frames where the
 * barcode's exact row is unknown.
 *
 * @param luma    grayscale image buffer
 * @param width   image width in pixels
 * @param height  image height in pixels
 * @param stride  bytes per row (>= width)
 * @param n_lines number of scanlines to sample
 * @param out     result (populated on success)
 * @return true if any scanline decoded a valid barcode
 */
bool bc1d_decode_image(const uint8_t* luma, int width, int height, int stride,
                       int n_lines, bc1d_result_t* out);

/**
 * Human-readable symbology name.
 */
const char* bc1d_type_name(bc1d_type_t type);

#ifdef __cplusplus
}
#endif

#endif /* BARCODE1D_H */
