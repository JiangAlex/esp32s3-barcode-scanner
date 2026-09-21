/**
 * @file http_upload.h
 * @brief HTTP POST photo upload to AI OCR recognition server
 */

#ifndef HTTP_UPLOAD_H
#define HTTP_UPLOAD_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Upload a JPEG photo to the recognition server via HTTP POST.
 *
 * Posts to: http://SERVER_URL:PORT/api/photo-recognize
 *
 * @param jpg_buf Pointer to JPEG image data
 * @param jpg_len Size of JPEG data in bytes
 * @param result_buf Buffer to receive the JSON result string
 * @param result_buf_size Size of result buffer
 * @return true on success (HTTP 200), false on failure
 */
bool http_upload_photo(const uint8_t* jpg_buf, size_t jpg_len,
                       char* result_buf, size_t result_buf_size);

#endif /* HTTP_UPLOAD_H */
