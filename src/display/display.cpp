/**
 * @file display.cpp
 * @brief TFT display driver + LVGL initialization using LovyanGFX
 */

#include "display.h"
#include <LovyanGFX.hpp>
#include "config/pinout.h"
#include "config/config.h"
#include "input/touch.h"
#include "scan_preview.h"  // for s_spi_mutex extern declaration

// ─── LovyanGFX Panel Configuration ─────────────────────────────────────────

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX(void) {
        // SPI bus configuration (shared with SD card)
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;       // FSPI (ESP32-S3)
            cfg.spi_mode = 0;
            cfg.freq_write = TFT_SPI_FREQ;
            cfg.freq_read = 16000000;
            cfg.spi_3wire = false;
            cfg.use_lock = true;            // Required for shared bus
            cfg.pin_sclk = TFT_PIN_SCLK;
            cfg.pin_mosi = TFT_PIN_MOSI;
            cfg.pin_miso = TFT_PIN_MISO;    // Needed when bus is shared with SD
            cfg.pin_dc = TFT_PIN_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // Panel configuration (ST7789T3, IPS)
        {
            auto cfg = _panel_instance.config();
            cfg.pin_cs = TFT_PIN_CS;
            cfg.pin_rst = TFT_PIN_RST;
            cfg.pin_busy = -1;
            cfg.memory_width = TFT_WIDTH;
            cfg.memory_height = TFT_HEIGHT;
            cfg.panel_width = TFT_WIDTH;
            cfg.panel_height = TFT_HEIGHT;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            cfg.offset_rotation = DISP_ROTATION;
            cfg.dummy_read_pixel = 8;
            cfg.dummy_read_bits = 1;
            cfg.readable = false;
            cfg.invert = DISP_INVERT;       // ST7789T3 IPS needs inversion
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = DISP_BUS_SHARED;  // Share SPI bus with SD card
            _panel_instance.config(cfg);
        }

        // Backlight configuration
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = TFT_PIN_BL;
            cfg.invert = false;
            cfg.freq = 44100;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light_instance.config(cfg);
            _panel_instance.setLight(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

// ─── Static variables ───────────────────────────────────────────────────────

static LGFX tft;
static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_disp_t* disp = nullptr;

// Draw buffer in PSRAM (two buffers for DMA-like performance)
static lv_color_t* buf1 = nullptr;
static lv_color_t* buf2 = nullptr;

#define DRAW_BUF_LINES  40  // Lines per buffer (240 * 40 * 2 = 19.2KB each)

// ─── LVGL flush callback ────────────────────────────────────────────────────

static void lvgl_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    if (s_spi_mutex && xSemaphoreTake(s_spi_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        lv_disp_flush_ready(drv);
        return;
    }

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((uint16_t*)&color_p->full, w * h);
    tft.endWrite();

    if (s_spi_mutex) xSemaphoreGive(s_spi_mutex);

    lv_disp_flush_ready(drv);
}

// ─── LVGL touch (pointer) input callback ────────────────────────────────────

static lv_indev_drv_t indev_drv;

static void lvgl_touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    uint16_t x = 0, y = 0;
    touch_read();
    if (touch_get_coordinates(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ─── Public functions ───────────────────────────────────────────────────────

void display_init(void) {
    // Initialize TFT
    tft.begin();
    tft.setRotation(DISP_ROTATION);
    tft.fillScreen(TFT_BLACK);

    // Set default brightness
    tft.setBrightness(DISP_BL_DEFAULT);

    // Initialize LVGL
    lv_init();

    // Allocate draw buffers in PSRAM
    buf1 = (lv_color_t*)ps_malloc(sizeof(lv_color_t) * TFT_WIDTH * DRAW_BUF_LINES);
    buf2 = (lv_color_t*)ps_malloc(sizeof(lv_color_t) * TFT_WIDTH * DRAW_BUF_LINES);

    if (!buf1 || !buf2) {
        // Fallback to internal RAM with smaller buffer
        buf1 = (lv_color_t*)malloc(sizeof(lv_color_t) * TFT_WIDTH * 10);
        buf2 = nullptr;
        lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_WIDTH * 10);
    } else {
        lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_WIDTH * DRAW_BUF_LINES);
    }

    // Register display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = TFT_WIDTH;
    disp_drv.ver_res = TFT_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    disp = lv_disp_drv_register(&disp_drv);

    // Initialize CST816D touch and register pointer input device
    if (touch_init(TFT_WIDTH, TFT_HEIGHT, DISP_ROTATION)) {
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.disp = disp;
        indev_drv.read_cb = lvgl_touch_cb;
        lv_indev_drv_register(&indev_drv);
    }
}

void display_set_spi_mutex(SemaphoreHandle_t mutex) {
    s_spi_mutex = mutex;
}

void display_set_brightness(uint8_t brightness) {
    tft.setBrightness(brightness);
}

lv_disp_t* display_get_disp(void) {
    return disp;
}

lgfx::LGFX_Device& display_get_tft(void) {
    return tft;
}
