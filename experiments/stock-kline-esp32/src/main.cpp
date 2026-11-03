/**
 * ESP32-S3 Candlestick K-line viewer (experiment).
 *
 * Fetches daily OHLCV from the ProTech-Stock-News API and draws a candlestick
 * chart (red-up / green-down, TW convention) + MA5 + MA20 + a volume strip on
 * the 2.8" 240x320 LCD, using an LVGL canvas we draw into ourselves.
 *
 *   GET http://blog.softsnail.com:8020/api/stock/{CODE}/kline?period=daily&days=N
 *   -> [{date,open,high,low,close,volume}, ...]  (ascending by date)
 *
 * BOOT (GPIO0): short press scrolls one page older; long press jumps to latest.
 *
 * Standalone test firmware — does not touch the main barcode-scanner src/.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <lvgl.h>
#include "display_test.h"

// ─── Config ─────────────────────────────────────────────────────────────────
static const char* WIFI_SSID = "Alex_iPhone";          // test hotspot (memory.md)
static const char* WIFI_PASS = "0919297782";                 // <-- set your WiFi password
static const char* STOCK_CODE = "2330";
static const int   FETCH_DAYS  = 120;
static const char* API_HOST = "http://blog.softsnail.com:8020";

#define PIN_BOOT   0
#define VISIBLE_BARS 40          // candles per screen
#define MAX_BARS    120

// ─── Canvas geometry (portrait 240x320) ─────────────────────────────────────
#define CW 240
#define CH 320
#define TITLE_H   22
#define PRICE_TOP (TITLE_H + 2)
#define PRICE_H   220
#define VOL_TOP   (PRICE_TOP + PRICE_H + 6)
#define VOL_H     56
#define AXIS_W    40             // right-side price axis label room

// ─── Data ───────────────────────────────────────────────────────────────────
struct Bar { float o, h, l, c; float vol; char date[12]; };
static Bar   s_bars[MAX_BARS];
static int   s_count = 0;
static int   s_scroll = 0;       // offset from the newest (0 = show most recent)

static lv_obj_t*   s_canvas = nullptr;
static lv_color_t* s_cbuf   = nullptr;
static lv_obj_t*   s_title  = nullptr;

LV_FONT_DECLARE(lv_font_cjk_tc_16);

// ─── Fetch ──────────────────────────────────────────────────────────────────
static bool fetchKline() {
    HTTPClient http;
    char url[160];
    snprintf(url, sizeof(url), "%s/api/stock/%s/kline?period=daily&days=%d",
             API_HOST, STOCK_CODE, FETCH_DAYS);
    Serial.printf("[HTTP] GET %s\n", url);
    http.begin(url);
    int code = http.GET();
    if (code != 200) { Serial.printf("[HTTP] status=%d\n", code); http.end(); return false; }

    // Stream-parse with a filter would be lighter, but 120 bars fit fine in PSRAM.
    String body = http.getString();
    http.end();

    JsonDocument doc;  // v7 elastic; allocates in heap/PSRAM
    DeserializationError e = deserializeJson(doc, body);
    if (e) { Serial.printf("[JSON] %s\n", e.c_str()); return false; }

    JsonArray arr = doc.as<JsonArray>();
    s_count = 0;
    for (JsonObject o : arr) {
        if (s_count >= MAX_BARS) break;
        Bar& b = s_bars[s_count];
        b.o = o["open"]  | 0.0f;
        b.h = o["high"]  | 0.0f;
        b.l = o["low"]   | 0.0f;
        b.c = o["close"] | 0.0f;
        b.vol = (float)(o["volume"] | 0);
        strncpy(b.date, o["date"] | "", sizeof(b.date) - 1);
        b.date[sizeof(b.date) - 1] = 0;
        s_count++;
    }
    Serial.printf("[DATA] %d bars (%s .. %s)\n", s_count,
                  s_count ? s_bars[0].date : "-",
                  s_count ? s_bars[s_count - 1].date : "-");
    return s_count > 0;
}

// ─── Drawing helpers ─────────────────────────────────────────────────────────
static void canvas_line(int x0, int y0, int x1, int y1, lv_color_t col) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d);
    d.color = col; d.width = 1;
    lv_point_t p[2] = {{(lv_coord_t)x0,(lv_coord_t)y0},{(lv_coord_t)x1,(lv_coord_t)y1}};
    lv_canvas_draw_line(s_canvas, p, 2, &d);
}
static void canvas_rect(int x, int y, int w, int h, lv_color_t col) {
    if (w < 1) w = 1; if (h < 1) h = 1;
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d);
    d.bg_color = col; d.bg_opa = LV_OPA_COVER; d.border_width = 0;
    lv_canvas_draw_rect(s_canvas, x, y, w, h, &d);
}

static float ma_at(int endIdx, int period) {
    if (endIdx + 1 < period) return NAN;
    float s = 0;
    for (int k = endIdx - period + 1; k <= endIdx; k++) s += s_bars[k].c;
    return s / period;
}

// Render the current window into the canvas.
static void render() {
    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);
    if (s_count == 0) return;

    int last = s_count - 1 - s_scroll;         // newest visible index
    if (last < 0) last = 0;
    int first = last - VISIBLE_BARS + 1;
    if (first < 0) first = 0;
    int n = last - first + 1;

    // Price range over the window (include MA20 lookback influence lightly).
    float pmin = 1e9f, pmax = -1e9f, vmax = 0;
    for (int i = first; i <= last; i++) {
        pmin = min(pmin, s_bars[i].l);
        pmax = max(pmax, s_bars[i].h);
        vmax = max(vmax, s_bars[i].vol);
    }
    if (pmax <= pmin) pmax = pmin + 1;
    float pad = (pmax - pmin) * 0.04f;
    pmin -= pad; pmax += pad;

    const int plotW = CW - AXIS_W;
    const float slotW = (float)plotW / n;
    const int bodyW = max(1, (int)(slotW * 0.6f));

    auto yPrice = [&](float p) -> int {
        return (int)(PRICE_TOP + PRICE_H * (1.0f - (p - pmin) / (pmax - pmin)));
    };

    // Y grid + price labels (4 lines).
    lv_draw_label_dsc_t ld; lv_draw_label_dsc_init(&ld);
    ld.color = lv_color_hex(0x888888); ld.font = &lv_font_cjk_tc_16;
    for (int g = 0; g <= 3; g++) {
        float p = pmin + (pmax - pmin) * g / 3.0f;
        int y = yPrice(p);
        canvas_line(0, y, plotW, y, lv_color_hex(0x222222));
        char buf[12]; snprintf(buf, sizeof(buf), "%.0f", p);
        lv_canvas_draw_text(s_canvas, plotW + 2, y - 8, AXIS_W, &ld, buf);
    }

    lv_color_t RED = lv_color_hex(0xFF3B30), GRN = lv_color_hex(0x34C759);

    // Candles.
    for (int i = first; i <= last; i++) {
        int k = i - first;
        int cx = (int)(k * slotW + slotW / 2);
        int yo = yPrice(s_bars[i].o), yc = yPrice(s_bars[i].c);
        int yh = yPrice(s_bars[i].h), yl = yPrice(s_bars[i].l);
        bool up = s_bars[i].c >= s_bars[i].o;
        lv_color_t col = up ? RED : GRN;
        canvas_line(cx, yh, cx, yl, col);                    // wick
        int top = min(yo, yc), hgt = abs(yc - yo);
        canvas_rect(cx - bodyW / 2, top, bodyW, hgt, col);   // body

        // volume strip
        int vh = (vmax > 0) ? (int)(VOL_H * s_bars[i].vol / vmax) : 0;
        canvas_rect(cx - bodyW / 2, VOL_TOP + VOL_H - vh, bodyW, vh, col);
    }

    // MA5 (yellow) + MA20 (cyan) polylines.
    auto drawMA = [&](int period, lv_color_t col) {
        int px = -1, py = -1;
        for (int i = first; i <= last; i++) {
            float m = ma_at(i, period);
            if (isnan(m)) { px = -1; continue; }
            int k = i - first;
            int cx = (int)(k * slotW + slotW / 2);
            int cy = yPrice(m);
            if (px >= 0) canvas_line(px, py, cx, cy, col);
            px = cx; py = cy;
        }
    };
    drawMA(5,  lv_color_hex(0xFFD60A));
    drawMA(20, lv_color_hex(0x30D5E8));

    // Title: code + newest date + last close.
    char t[48];
    snprintf(t, sizeof(t), "%s  %s  %.0f", STOCK_CODE,
             s_bars[last].date, s_bars[last].c);
    lv_label_set_text(s_title, t);
}

// ─── BOOT button (scroll) ────────────────────────────────────────────────────
static void pollButton() {
    static bool prev = true;             // INPUT_PULLUP idle HIGH
    static uint32_t downT = 0;
    bool now = digitalRead(PIN_BOOT);
    if (prev && !now) downT = millis();                 // press
    if (!prev && now) {                                  // release
        uint32_t held = millis() - downT;
        if (held > 600) { s_scroll = 0; }                // long: latest
        else { s_scroll += VISIBLE_BARS / 2;             // short: older
               int maxScroll = max(0, s_count - VISIBLE_BARS);
               if (s_scroll > maxScroll) s_scroll = maxScroll; }
        render();
    }
    prev = now;
}

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n=== ESP32-S3 K-line viewer ===");
    pinMode(PIN_BOOT, INPUT_PULLUP);

    display_init();

    // Title label.
    s_title = lv_label_create(lv_scr_act());
    lv_obj_set_style_text_font(s_title, &lv_font_cjk_tc_16, 0);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 3);
    lv_label_set_text(s_title, "connecting WiFi...");

    // Canvas.
    s_cbuf = (lv_color_t*)ps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR(CW, CH));
    s_canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(s_canvas, s_cbuf, CW, CH, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_canvas, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);
    // keep title above canvas
    lv_obj_move_foreground(s_title);

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    // Diagnostic: list visible APs so we can confirm the SSID is reachable
    // (iPhone hotspot SSIDs can contain non-breaking spaces / exact casing).
    Serial.println("[WiFi] scanning...");
    int ns = WiFi.scanNetworks();
    Serial.printf("[WiFi] %d networks:\n", ns);
    for (int i = 0; i < ns; i++)
        Serial.printf("   '%s' rssi=%d enc=%d\n",
                      WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i));

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("[WiFi] connecting to '%s'\n", WIFI_SSID);
    uint32_t t0 = millis();
    int lastSt = -1;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        int st = WiFi.status();
        if (st != lastSt) { Serial.printf("[WiFi] status=%d\n", st); lastSt = st; }
        lv_timer_handler(); delay(200);
    }
    if (WiFi.status() != WL_CONNECTED) {
        lv_label_set_text(s_title, "WiFi FAILED");
        Serial.println("[WiFi] failed");
        return;
    }
    Serial.printf("[WiFi] connected, IP=%s\n", WiFi.localIP().toString().c_str());
    lv_label_set_text(s_title, "fetching K-line...");
    lv_timer_handler();

    if (fetchKline()) render();
    else lv_label_set_text(s_title, "fetch FAILED");
}

void loop() {
    pollButton();
    lv_timer_handler();
    delay(5);
}
