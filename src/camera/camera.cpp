/**
 * @file camera.cpp
 * @brief OV5640/OV2640 camera driver implementation for ESP32-S3
 *        (Waveshare ESP32-S3-Touch-LCD-2, 24-pin FPC camera interface)
 */

#include "camera.h"
#include <Arduino.h>
#include "config/pinout.h"
#include "config/config.h"

bool camera_init(void) {
    camera_config_t config;

    config.ledc_channel = LEDC_CHANNEL_2;
    config.ledc_timer = LEDC_TIMER_1;
    config.pin_d0 = CAM_PIN_D0;
    config.pin_d1 = CAM_PIN_D1;
    config.pin_d2 = CAM_PIN_D2;
    config.pin_d3 = CAM_PIN_D3;
    config.pin_d4 = CAM_PIN_D4;
    config.pin_d5 = CAM_PIN_D5;
    config.pin_d6 = CAM_PIN_D6;
    config.pin_d7 = CAM_PIN_D7;
    config.pin_xclk = CAM_PIN_XCLK;
    config.pin_pclk = CAM_PIN_PCLK;
    config.pin_vsync = CAM_PIN_VSYNC;
    config.pin_href = CAM_PIN_HREF;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_pwdn = CAM_PIN_PWDN;
    config.pin_reset = CAM_PIN_RESET;

    config.xclk_freq_hz = CAM_XCLK_FREQ;
    config.frame_size = CAM_FRAME_SIZE;         // QVGA 320x240
    config.pixel_format = PIXFORMAT_GRAYSCALE;  // Grayscale for barcode scanning
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_DRAM;   // No PSRAM on this board
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = 1;                       // Single buffer saves DRAM

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed: 0x%x\n", err);
        return false;
    }

    // Adjust sensor settings for barcode scanning
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        // OV5640 has a flipped/mirrored orientation vs OV2640 on this module
        if (s->id.PID == OV5640_PID) {
            s->set_vflip(s, 1);
            s->set_hmirror(s, 0);
        }
        s->set_brightness(s, 1);        // Slightly brighter
        s->set_contrast(s, 1);          // Higher contrast for barcodes
        s->set_saturation(s, -2);       // Reduce saturation (grayscale anyway)
        s->set_whitebal(s, 1);          // Auto white balance
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);     // Auto exposure
        s->set_aec2(s, 1);             // Enable AEC DSP
        s->set_gain_ctrl(s, 1);         // Auto gain
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)6);

        Serial.printf("[CAM] Sensor PID: 0x%x initialized (QVGA Grayscale)\n", s->id.PID);
    } else {
        Serial.println("[CAM] Sensor handle unavailable");
    }

    return true;
}

camera_fb_t* camera_capture(void) {
    return esp_camera_fb_get();
}

void camera_return_fb(camera_fb_t* fb) {
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

void camera_deinit(void) {
    esp_camera_deinit();
}

void camera_set_jpeg_mode(bool enable) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return;

    if (enable) {
        // Switch to JPEG mode: SVGA 800x600
        s->set_framesize(s, FRAMESIZE_SVGA);
        s->set_pixformat(s, PIXFORMAT_JPEG);
        Serial.println("[CAM] JPEG mode: FRAMESIZE_SVGA");
    } else {
        // Switch back to grayscale QVGA
        s->set_framesize(s, FRAMESIZE_QVGA);
        s->set_pixformat(s, PIXFORMAT_GRAYSCALE);
        Serial.println("[CAM] Grayscale mode: FRAMESIZE_QVGA");
    }
}

camera_fb_t* camera_capture_jpeg(void) {
    // Ensure JPEG mode is set
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        if (s->id.PID == OV5640_PID) {
            s->set_vflip(s, 1);
            s->set_hmirror(s, 0);
        }
    }
    return esp_camera_fb_get();
}
