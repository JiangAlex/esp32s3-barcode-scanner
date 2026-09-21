/**
 * @file camera.h
 * @brief OV5640/OV2640 camera driver for ESP32-S3
 */

#ifndef CAMERA_H
#define CAMERA_H

#include <esp_camera.h>

/**
 * @brief Initialize the camera module (OV5640, OV2640-compatible).
 * @return true on success, false on failure
 */
bool camera_init(void);

/**
 * @brief Capture a single frame from the camera.
 * @return Pointer to camera frame buffer (must be returned with camera_return_fb)
 */
camera_fb_t* camera_capture(void);

/**
 * @brief Return a previously captured frame buffer.
 * @param fb Pointer to frame buffer to return
 */
void camera_return_fb(camera_fb_t* fb);

/**
 * @brief Deinitialize the camera (free resources).
 */
void camera_deinit(void);

/**
 * @brief Switch camera to JPEG mode (SVGA) for photo capture.
 *        Can be called to switch back from grayscale mode.
 * @param enable true = JPEG/SVGA mode, false = previous/grayscale mode
 */
void camera_set_jpeg_mode(bool enable);

/**
 * @brief Capture a JPEG frame from the camera.
 *        Must be in JPEG mode (call camera_set_jpeg_mode(true) first).
 * @return Pointer to JPEG frame buffer (must be returned with camera_return_fb)
 */
camera_fb_t* camera_capture_jpeg(void);

#endif /* CAMERA_H */
