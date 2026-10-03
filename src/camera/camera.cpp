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
    // VALIDATION (option B): the official Waveshare demo uses RGB565 for the
    // OV5640 on this board, not GRAYSCALE. OV5640 grayscale support in
    // esp32-camera is unreliable and likely why the preview was unrecognizable.
    // Temporarily use RGB565 to confirm the format is the root cause.
    config.pixel_format = PIXFORMAT_RGB565;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location = CAMERA_FB_IN_PSRAM;  // PSRAM now enabled (qio_opi)
    config.jpeg_quality = CAM_JPEG_QUALITY;
    config.fb_count = 2;                       // Double buffer (PSRAM available)

    // Initialize camera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[CAM] Init failed: 0x%x\n", err);
        return false;
    }

    // Sensor tuning for QR decoding. Over-sharpening causes ringing/overshoot
    // at black↔white module boundaries, which corrupts quirc's per-module
    // sampling (finder patterns still detect → size=21, but data ECC fails).
    // Use neutral sharpness and mild contrast; keep auto exposure/gain on.
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        if (s->id.PID == OV5640_PID) {
            s->set_vflip(s, 1);
            s->set_hmirror(s, 1);
        }
        s->set_contrast(s, 1);          // mild contrast (was 2)
        s->set_sharpness(s, 0);         // neutral — avoid ringing (was 2)
        s->set_brightness(s, 0);
        s->set_saturation(s, -2);       // we only use luma
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);     // auto exposure
        s->set_aec2(s, 1);
        s->set_gain_ctrl(s, 1);         // auto gain
        s->set_gainceiling(s, (gainceiling_t)GAINCEILING_4X);
        Serial.printf("[CAM] Sensor PID: 0x%x initialized (QVGA RGB565, QR-tuned)\n", s->id.PID);
    } else {
        Serial.println("[CAM] Sensor handle unavailable");
    }

    // Probe autofocus once at startup so the boot log reports whether the
    // attached lens is an AF (VCM) module. Result gates the SVGA AF trigger.
    camera_af_probe();

    // Algorithmic AF does not converge on the FD5640 module, but the VCM moves
    // when driven directly. Apply a fixed near-focus position so the preview is
    // usable for close-range labels from boot. Tune via the serial +/- commands.
    camera_af_set_manual(camera_af_get_manual());
    Serial.printf("[AF] manual focus applied: VCM=%u (tune with serial +/-)\n",
                  camera_af_get_manual());

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

// ─── OV5640 Autofocus ────────────────────────────────────────────────────────
// The esp32-camera component ships ov5640_af.c, but its implementation is gated
// behind CONFIG_CAMERA_AF_SUPPORT (an ESP-IDF menuconfig flag not enabled under
// the Arduino build) and its functions live in private_include. So we drive the
// AF MCU directly through the public sensor_t set_reg/get_reg interface, loading
// the vendored firmware blob — the same sequence the component uses.

#include "ov5640_af_firmware.h"

// OV5640 AF command/status registers.
#define OV5640_CMD_MAIN       0x3022
#define OV5640_CMD_ACK        0x3023
#define OV5640_CMD_PARA0      0x3024
#define OV5640_CMD_PARA4      0x3028
#define OV5640_CMD_FW_STATUS  0x3029

#define OV5640_AF_TRIG_SINGLE 0x03
#define OV5640_AF_CONTINUOUS  0x04
#define OV5640_FW_STATUS_IDLE    0x70
#define OV5640_FW_STATUS_FOCUSED 0x10
#define OV5640_FW_STATUS_S_FOCUSING 0x00   // running

static bool s_af_loaded = false;
static bool s_af_available = false;   // true once a FOCUSED state is observed

static bool af_reg_write(sensor_t* s, int reg, int val) {
    return s->set_reg(s, reg, 0xff, val) >= 0;
}
static int af_reg_read(sensor_t* s, int reg) {
    return s->get_reg(s, reg, 0xff);
}

// Wait for the AF firmware status register to reach IDLE (firmware booted).
static bool af_wait_fw_idle(sensor_t* s, uint32_t timeout_ms) {
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        int st = af_reg_read(s, OV5640_CMD_FW_STATUS);
        if (st == OV5640_FW_STATUS_IDLE) return true;
        delay(5);
    }
    return false;
}

bool camera_af_probe(void) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) { Serial.println("[AF] no sensor handle"); return false; }
    if (s->id.PID != OV5640_PID) {
        Serial.printf("[AF] sensor PID 0x%x is not OV5640 — AF not applicable\n", s->id.PID);
        return false;
    }

    Serial.printf("[AF] loading AF firmware (%u bytes) into OV5640 MCU...\n",
                  (unsigned)sizeof(ov5640_af_firmware));

    // Enable the VCM (voice-coil motor) driver before loading AF firmware.
    // The AF MCU firmware runs the focus *algorithm*, but the physical lens
    // actuator is driven through sensor registers 0x3600/0x3601 (VCM control).
    // esp32-camera's default OV5640 init does NOT set these (it ships with AF
    // disabled), so the MCU enters S_FOCUSING but the motor never moves and the
    // search never converges — exactly the fw_status=0x00-stuck symptom seen on
    // this FD5640 500W module. Values are from OmniVision's reference init.
    af_reg_write(s, 0x3600, 0x08);   // VCM control
    af_reg_write(s, 0x3601, 0x33);   // VCM control
    Serial.printf("[AF] VCM ctrl set: 0x3600=0x%02x 0x3601=0x%02x\n",
                  af_reg_read(s, 0x3600) & 0xff, af_reg_read(s, 0x3601) & 0xff);

    // Reset the AF MCU, upload firmware to program memory at 0x8000, restart.
    if (!af_reg_write(s, 0x3000, 0x20)) { Serial.println("[AF] MCU reset failed"); return false; }
    uint16_t addr = 0x8000;
    for (size_t i = 0; i < sizeof(ov5640_af_firmware); i++) {
        if (!af_reg_write(s, addr++, ov5640_af_firmware[i])) {
            Serial.printf("[AF] firmware write failed at offset %u\n", (unsigned)i);
            return false;
        }
    }
    af_reg_write(s, OV5640_CMD_MAIN, 0x00);
    af_reg_write(s, OV5640_CMD_ACK, 0x00);
    for (int r = OV5640_CMD_PARA0; r <= OV5640_CMD_PARA4; r++) af_reg_write(s, r, 0x00);
    af_reg_write(s, OV5640_CMD_FW_STATUS, 0x7f);
    af_reg_write(s, 0x3000, 0x00);            // start MCU

    if (!af_wait_fw_idle(s, 3000)) {
        Serial.println("[AF] firmware did not reach IDLE — AF MCU not responding");
        return false;
    }
    s_af_loaded = true;

    // Note: on the FD5640 500W module the algorithmic AF (single-shot 0x03 and
    // continuous 0x04) never converges — fw_status stays at S_FOCUSING. The VCM
    // itself works when driven directly, so scanning uses a fixed manual
    // near-focus position (see camera_af_set_manual / camera_init) instead of
    // the AF algorithm. We keep the firmware loaded so manual VCM control via
    // the sensor registers is available.
    s_af_available = false;   // algorithmic AF not used; manual focus is applied
    Serial.println("[AF] firmware loaded; using manual fixed-focus (algorithmic AF not converging on this module)");
    return true;
}

bool camera_af_is_available(void) {
    return s_af_available;
}

// Directly command the VCM to an absolute position, bypassing the AF MCU
// algorithm entirely. value range ~0..1023 (usable ~100..900): small = near
// focus, large = far/infinity. On-device the algorithmic AF never converges on
// this FD5640 module, but a manual sweep visibly changed focus — so the motor
// is good and we drive it directly.
static uint16_t s_vcm_manual = 120;   // near-focus peak (from sharpness sweeps)

static void af_set_vcm_manual(sensor_t* s, uint16_t vcm) {
    s->set_reg(s, 0x3022, 0x00, 0xff);                 // manual focus mode
    s->set_reg(s, 0x3023, (vcm >> 8) & 0x03, 0xff);    // position high bits
    s->set_reg(s, 0x3024, vcm & 0xff, 0xff);           // position low byte
}

bool camera_af_set_manual(uint16_t vcm) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s || s->id.PID != OV5640_PID) return false;
    s_vcm_manual = vcm;
    af_set_vcm_manual(s, vcm);
    return true;
}

uint16_t camera_af_get_manual(void) {
    return s_vcm_manual;
}

bool camera_af_trigger_oneshot(void) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s || s->id.PID != OV5640_PID) return false;

    // Algorithmic AF (single/continuous) does not converge on this module, but
    // the VCM moves when driven directly. Apply the current fixed near-focus
    // position. Fast and deterministic for fixed-distance label scanning.
    af_set_vcm_manual(s, s_vcm_manual);
    return true;
}

bool camera_set_rgb565_svga(void) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    if (s->set_framesize(s, FRAMESIZE_SVGA) != 0) return false;
    return true;
}
