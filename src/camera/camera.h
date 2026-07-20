/**
 * @file camera.h
 * @brief OV2640 camera driver for ESP32-S3
 */

#ifndef CAMERA_H
#define CAMERA_H

#include <esp_camera.h>

/**
 * @brief Initialize OV2640 camera module.
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

#endif /* CAMERA_H */
