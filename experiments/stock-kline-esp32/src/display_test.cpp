/**
 * @file display_test.cpp
 * @brief Minimal ST7789T3 + LVGL init (self-contained) for the ANCS LCD test.
 *
 * Panel/pin config mirrors the main project's display.cpp + pinout.h, stripped
 * of touch, SPI-mutex and scan_preview dependencies.
 */
#include "display_test.h"
#include <LovyanGFX.hpp>
#include <esp_timer.h>

// ─── Pins (from main project config/pinout.h) ──────────────────────────────
#define TFT_PIN_SCLK   39
#define TFT_PIN_MOSI   38
#define TFT_PIN_MISO   40
#define TFT_PIN_CS     45
#define TFT_PIN_DC     42
#define TFT_PIN_RST    -1
#define TFT_PIN_BL     1
#define TFT_W          240
#define TFT_H          320
#define TFT_SPI_FREQ   40000000
#define DISP_ROTATION  0
#define DISP_INVERT    true   // ST7789T3 IPS needs inversion (main project)

// ─── LovyanGFX panel ────────────────────────────────────────────────────────
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
public:
    LGFX() {
        {
            auto c = _bus.config();
            c.spi_host   = SPI2_HOST;
            c.spi_mode   = 0;
            c.freq_write = TFT_SPI_FREQ;
            c.freq_read  = 16000000;
            c.spi_3wire  = false;
            c.use_lock   = true;
            c.pin_sclk   = TFT_PIN_SCLK;
            c.pin_mosi   = TFT_PIN_MOSI;
            c.pin_miso   = TFT_PIN_MISO;
            c.pin_dc     = TFT_PIN_DC;
            _bus.config(c);
            _panel.setBus(&_bus);
        }
        {
            auto c = _panel.config();
            c.pin_cs          = TFT_PIN_CS;
            c.pin_rst         = TFT_PIN_RST;
            c.pin_busy        = -1;
            c.memory_width    = TFT_W;
            c.memory_height   = TFT_H;
            c.panel_width     = TFT_W;
            c.panel_height    = TFT_H;
            c.offset_rotation = DISP_ROTATION;
            c.readable        = false;
            c.invert          = DISP_INVERT;
            c.rgb_order       = false;
            c.dlen_16bit      = false;
            c.bus_shared      = true;
            _panel.config(c);
        }
        {
            auto c = _light.config();
            c.pin_bl      = TFT_PIN_BL;
            c.invert      = false;
            c.freq        = 44100;
            c.pwm_channel = 0;
            _light.config(c);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

static LGFX tft;
static lv_disp_drv_t      disp_drv;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t*        buf1 = nullptr;
static lv_color_t*        buf2 = nullptr;
#define DRAW_BUF_LINES 40

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((uint16_t*)&color_p->full, w * h);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void tick_cb(void*) { lv_tick_inc(2); }

void display_init(void) {
    tft.begin();
    tft.setRotation(DISP_ROTATION);
    tft.fillScreen(TFT_BLACK);
    tft.setBrightness(200);

    lv_init();
    buf1 = (lv_color_t*)ps_malloc(sizeof(lv_color_t) * TFT_W * DRAW_BUF_LINES);
    buf2 = (lv_color_t*)ps_malloc(sizeof(lv_color_t) * TFT_W * DRAW_BUF_LINES);
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, TFT_W * DRAW_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = TFT_W;
    disp_drv.ver_res  = TFT_H;
    disp_drv.flush_cb = flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    const esp_timer_create_args_t targs = { .callback = &tick_cb, .name = "lv_tick" };
    esp_timer_handle_t th = nullptr;
    esp_timer_create(&targs, &th);
    esp_timer_start_periodic(th, 2000);
}

void display_task_handler(void) { lv_timer_handler(); }
