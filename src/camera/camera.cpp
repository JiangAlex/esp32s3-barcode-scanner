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

// Wait for the AF MCU to clear CMD_ACK (0x3023) back to 0x00, which signals the
// previously issued command has been consumed/completed.
static bool af_wait_ack_clear(sensor_t* s, uint32_t timeout_ms) {
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        if (af_reg_read(s, OV5640_CMD_ACK) == 0x00) return true;
        delay(5);
    }
    return false;
}

// Run one single-shot autofocus using the full handshake from the official
// esp32-camera ov5640_af.c (ov5640_af_start). The critical part we were
// missing before: the AF loop must first be prepared with MAIN=0x01 then
// MAIN=0x08 and an ack-clear wait, BEFORE issuing the actual focus command.
//
// On-device finding (2026-10-03): the preamble (0x01/0x08) acks fine, but the
// single-shot command (0x03) never completes — fw_status enters S_FOCUSING
// (0x00) and never converges, so ack is never cleared. This matches OV5640 AF
// modules whose single-trigger search is unreliable. Continuous AF (0x04) is
// more robust: it keeps driving the VCM and settles to S_FOCUSED (0x10). The
// official ov5640_af_set_mode(AUTO) also uses 0x04. We therefore start
// continuous AF and wait for FOCUSED instead of relying on ack-clear for 0x03.
// Returns true if the lens reached FOCUSED (0x10).
static bool af_run_focus(sensor_t* s, uint32_t timeout_ms) {
    af_reg_write(s, OV5640_CMD_MAIN, 0x01);          // prepare
    af_reg_write(s, OV5640_CMD_MAIN, 0x08);          // pause/release AF loop
    if (!af_wait_ack_clear(s, timeout_ms)) return false;

    af_reg_write(s, OV5640_CMD_ACK, 0x01);           // mark command pending
    af_reg_write(s, OV5640_CMD_MAIN, OV5640_AF_CONTINUOUS);  // continuous AF

    // Continuous AF does not clear ACK the way a completed single-shot would;
    // instead the firmware drives the VCM and fw_status reaches FOCUSED (0x10)
    // once a sharp peak is found. Wait for that.
    uint32_t start = millis();
    while ((millis() - start) < timeout_ms) {
        if (af_reg_read(s, OV5640_CMD_FW_STATUS) == OV5640_FW_STATUS_FOCUSED) return true;
        delay(10);
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

    // Reset the AF MCU, upload firmware to program memory at 0x8000, restart.
    if (!af_reg_write(s, 0x3000, 0x20)) { Serial.println("[AF] MCU reset failed"); return false; }
    uint16_t addr = 0x8000;
    for (size_t i = 0; i < sizeof(ov5640_af_firmware); i++) {
        if (!af_reg_write(s, addr++, ov5640_af_firmware[i])) {
            Serial.printf("[AF] firmware write failed at offset %u\n", (unsigned)i);
            return false;
        }
    }

    // DIAGNOSTIC: read back the first few firmware bytes to confirm the SCCB
    // writes to program memory (0x8000+) actually stick. If readback != blob,
    // the register path to program memory is the problem (not the handshake).
    Serial.print("[AF] fw readback @0x8000: ");
    bool fw_match = true;
    for (int i = 0; i < 8; i++) {
        int rb = af_reg_read(s, 0x8000 + i);
        Serial.printf("%02x ", rb & 0xff);
        if ((rb & 0xff) != ov5640_af_firmware[i]) fw_match = false;
    }
    Serial.printf("| expect: ");
    for (int i = 0; i < 8; i++) Serial.printf("%02x ", ov5640_af_firmware[i]);
    Serial.printf("| %s\n", fw_match ? "MATCH" : "MISMATCH");

    af_reg_write(s, OV5640_CMD_MAIN, 0x00);
    af_reg_write(s, OV5640_CMD_ACK, 0x00);
    for (int r = OV5640_CMD_PARA0; r <= OV5640_CMD_PARA4; r++) af_reg_write(s, r, 0x00);
    af_reg_write(s, OV5640_CMD_FW_STATUS, 0x7f);
    af_reg_write(s, 0x3000, 0x00);            // start MCU

    // DIAGNOSTIC: poll fw_status explicitly and log the transition to IDLE.
    {
        uint32_t t0 = millis();
        int last = -1, reads = 0;
        bool idle = false;
        while ((millis() - t0) < 3000) {
            int st = af_reg_read(s, OV5640_CMD_FW_STATUS);
            reads++;
            if (st != last) { Serial.printf("[AF] boot fw_status=0x%02x @%lums\n", st, millis() - t0); last = st; }
            if (st == OV5640_FW_STATUS_IDLE) { idle = true; break; }
            delay(5);
        }
        Serial.printf("[AF] boot poll: idle=%d reads=%d last=0x%02x\n", idle, reads, last);
        if (!idle) {
            Serial.println("[AF] firmware did not reach IDLE — AF MCU not responding");
            return false;
        }
    }
    s_af_loaded = true;
    Serial.println("[AF] firmware loaded, MCU IDLE. Triggering single-shot focus...");

    // DIAGNOSTIC: step through the focus handshake with continuous AF (0x04),
    // logging status transitions. Single-shot (0x03) was observed to stall at
    // S_FOCUSING; continuous AF drives the VCM to FOCUSED (0x10).
    Serial.println("[AF] focus seq: MAIN=0x01");
    af_reg_write(s, OV5640_CMD_MAIN, 0x01);
    Serial.println("[AF] focus seq: MAIN=0x08");
    af_reg_write(s, OV5640_CMD_MAIN, 0x08);
    bool pre_ack = af_wait_ack_clear(s, 3000);
    Serial.printf("[AF] preamble ack-clear=%d (ack=0x%02x fw=0x%02x)\n",
                  pre_ack, af_reg_read(s, OV5640_CMD_ACK) & 0xff,
                  af_reg_read(s, OV5640_CMD_FW_STATUS) & 0xff);

    af_reg_write(s, OV5640_CMD_ACK, 0x01);
    af_reg_write(s, OV5640_CMD_MAIN, OV5640_AF_CONTINUOUS);
    Serial.println("[AF] focus seq: MAIN=0x04 (continuous), waiting for FOCUSED...");

    uint32_t t0 = millis();
    int last = -1;
    bool focused = false;
    while ((millis() - t0) < 3000) {
        int st = af_reg_read(s, OV5640_CMD_FW_STATUS);
        if (st != last) { Serial.printf("[AF] focus fw_status=0x%02x @%lums\n", st, millis() - t0); last = st; }
        if (st == OV5640_FW_STATUS_FOCUSED) { focused = true; break; }
        delay(10);
    }
    Serial.printf("[AF] after focus: focused=%d fw_status=0x%02x\n", focused, last);

    bool af_ok = focused;
    if (af_ok) Serial.println("[AF] RESULT: continuous AF reached FOCUSED — AF-capable module confirmed");
    else       Serial.println("[AF] RESULT: AF did not reach FOCUSED — fixed-focus lens or VCM not converging");
    s_af_available = af_ok;
    return true;   // firmware path worked; focus outcome logged above
}

bool camera_af_is_available(void) {
    return s_af_available;
}

bool camera_af_trigger_oneshot(void) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s || s->id.PID != OV5640_PID || !s_af_loaded) return false;

    // Continuous-AF focus (see af_run_focus). The preamble (MAIN 0x01→0x08 +
    // ack wait) is mandatory. We wait up to the timeout for FOCUSED; a short
    // settle is unnecessary since reaching FOCUSED already means the VCM
    // converged, but keep a tiny margin.
    bool ok = af_run_focus(s, 1500);
    if (ok) delay(30);
    return ok;
}

bool camera_set_rgb565_svga(void) {
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    if (s->set_framesize(s, FRAMESIZE_SVGA) != 0) return false;
    return true;
}
