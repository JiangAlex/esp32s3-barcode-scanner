/**
 * @file display.cpp
 * @brief TFT display driver + LVGL initialization using LovyanGFX
 */

#include "display.h"
#include <LovyanGFX.hpp>
#include "config/pinout.h"
#include "config/config.h"

// ─── LovyanGFX Panel Configuration ─────────────────────────────────────────

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ILI9341 _panel_instance;
    lgfx::Bus_SPI _bus_instance;
    lgfx::Light_PWM _light_instance;

public:
    LGFX(void) {
        // SPI bus configuration
        {
            auto cfg = _bus_instance.config();
            cfg.spi_host = SPI2_HOST;       // HSPI
            cfg.spi_mode = 0;
            cfg.freq_write = TFT_SPI_FREQ;
            cfg.freq_read = 16000000;
            cfg.pin_sclk = TFT_PIN_SCLK;
            cfg.pin_mosi = TFT_PIN_MOSI;
            cfg.pin_miso = -1;              // Not needed for display
            cfg.pin_dc = TFT_PIN_DC;
            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }

        // Panel configuration
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
            cfg.invert = false;
            cfg.rgb_order = false;
            cfg.dlen_16bit = false;
            cfg.bus_shared = false;
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
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((uint16_t*)&color_p->full, w * h);
    tft.endWrite();

    lv_disp_flush_ready(drv);
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
}

void display_set_brightness(uint8_t brightness) {
    tft.setBrightness(brightness);
}

lv_disp_t* display_get_disp(void) {
    return disp;
}
