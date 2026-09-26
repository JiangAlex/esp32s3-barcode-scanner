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

/**
 * @brief Probe OV5640 autofocus capability.
 *
 * Loads the AF firmware into the sensor's on-chip MCU, triggers a single-shot
 * focus, and polls the firmware status register. Logs the outcome over serial.
 * This determines whether the attached lens is an AF (VCM) module — a fixed-
 * focus lens accepts the firmware but the status never reaches FOCUSED.
 *
 * @return true if the AF firmware loaded and reached IDLE (AF-capable path),
 *         false if firmware load timed out or the sensor is not OV5640.
 */
bool camera_af_probe(void);

/**
 * @brief Whether the startup AF probe confirmed an AF-capable lens (observed
 *        the FOCUSED state). If false, the lens is fixed-focus (or no OV5640)
 *        and AF triggering should be skipped.
 */
bool camera_af_is_available(void);

/**
 * @brief Trigger a single-shot autofocus and wait briefly for it to settle.
 *        Requires camera_af_probe() to have succeeded earlier (firmware loaded).
 * @return true if focus reached the FOCUSED state within the timeout.
 */
bool camera_af_trigger_oneshot(void);

/**
 * @brief Switch the sensor to SVGA (800x600) RGB565 for a hi-res single shot.
 *        Returns true on success. Pair with camera_set_rgb565_qvga() to revert.
 */
bool camera_set_rgb565_svga(void);

/**
 * @brief Switch the sensor back to QVGA (320x240) RGB565 (live preview mode).
 */
bool camera_set_rgb565_qvga(void);

#endif /* CAMERA_H */
