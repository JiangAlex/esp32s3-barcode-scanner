/**
 * @file ui_notify.cpp
 * @brief LVGL notification *history* screen + cross-task post buffer.
 *
 * Keeps the last N notifications received since connection and shows them as a
 * scrollable list (newest first). NOTE: this is the ESP32's own accumulated
 * history of notifications seen *while connected* — iOS/ANCS does NOT expose
 * the iPhone's existing Notification Center history.
 */
#include "ui_notify.h"
#include <lvgl.h>
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

LV_FONT_DECLARE(lv_font_cjk_tc_16);

static void rebuild_list(void);  // forward decl

#define HIST_MAX   8     // keep last 8 notifications

// ─── History ring (LVGL-task-owned; not touched from BLE callback) ──────────
typedef struct {
    char cat[24];
    char when[12];
    char title[128];
    char msg[160];
} notif_t;
static notif_t  s_hist[HIST_MAX];
static int      s_hist_count = 0;   // number of valid entries (<= HIST_MAX)

// ─── Widgets ────────────────────────────────────────────────────────────────
static lv_obj_t* s_scr    = nullptr;
static lv_obj_t* s_status = nullptr;   // top status line (fixed)
static lv_obj_t* s_list   = nullptr;   // scrollable history container

// ─── Cross-task buffer (BLE callback → LVGL task) ───────────────────────────
static portMUX_TYPE  s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_status_pending = false;
static volatile bool s_notif_pending  = false;
static char s_status_buf[48];
static char s_cat_buf[24];
static char s_when_buf[12];
static char s_title_buf[128];
static char s_msg_buf[160];

void ui_notify_init(void) {
    s_scr = lv_scr_act();
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    // Fixed status line at top.
    s_status = lv_label_create(s_scr);
    lv_obj_set_style_text_font(s_status, &lv_font_cjk_tc_16, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0x00C8FF), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 6, 4);
    lv_label_set_text(s_status, "booting...");

    // Scrollable history list below the status line.
    s_list = lv_obj_create(s_scr);
    lv_obj_set_size(s_list, 240, 292);
    lv_obj_align(s_list, LV_ALIGN_TOP_LEFT, 0, 28);
    lv_obj_set_style_bg_color(s_list, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    rebuild_list();  // show "等待通知..." placeholder
}

// Rebuild the list widget from the history ring (newest first).
static void rebuild_list(void) {
    if (!s_list) return;
    lv_obj_clean(s_list);
    if (s_hist_count == 0) {
        lv_obj_t* e = lv_label_create(s_list);
        lv_obj_set_style_text_font(e, &lv_font_cjk_tc_16, 0);
        lv_obj_set_style_text_color(e, lv_color_hex(0x888888), 0);
        lv_label_set_text(e, "等待通知...");
        return;
    }
    // s_hist holds valid entries linearly in [0 .. s_hist_count-1]
    // (oldest..newest); display newest first.
    for (int n = 0; n < s_hist_count; n++) {
        int idx = s_hist_count - 1 - n;
        notif_t* it = &s_hist[idx];

        lv_obj_t* card = lv_obj_create(s_list);
        lv_obj_set_width(card, LV_PCT(100));
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x101010), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 3, 0);
        lv_obj_set_style_pad_bottom(card, 6, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* head = lv_label_create(card);
        lv_obj_set_style_text_font(head, &lv_font_cjk_tc_16, 0);
        lv_obj_set_style_text_color(head, lv_color_hex(0xFFC800), 0);
        char h[180];
        if (it->when[0])
            snprintf(h, sizeof(h), "%s [%s] %s", it->when, it->cat, it->title);
        else
            snprintf(h, sizeof(h), "[%s] %s", it->cat, it->title);
        lv_label_set_long_mode(head, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(head, LV_PCT(100));
        lv_label_set_text(head, h);

        if (it->msg[0]) {
            lv_obj_t* body = lv_label_create(card);
            lv_obj_set_style_text_font(body, &lv_font_cjk_tc_16, 0);
            lv_obj_set_style_text_color(body, lv_color_hex(0xCCCCCC), 0);
            lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(body, LV_PCT(100));
            lv_label_set_text(body, it->msg);
        }
    }
    lv_obj_scroll_to_y(s_list, 0, LV_ANIM_OFF);  // keep newest in view
}

void ui_notify_set_status(const char* status) {
    portENTER_CRITICAL(&s_mux);
    strncpy(s_status_buf, status ? status : "", sizeof(s_status_buf) - 1);
    s_status_buf[sizeof(s_status_buf) - 1] = 0;
    s_status_pending = true;
    portEXIT_CRITICAL(&s_mux);
}

void ui_notify_post(const char* category, const char* when, const char* title, const char* message) {
    portENTER_CRITICAL(&s_mux);
    strncpy(s_cat_buf,   category ? category : "", sizeof(s_cat_buf) - 1);
    strncpy(s_when_buf,  when ? when : "",         sizeof(s_when_buf) - 1);
    strncpy(s_title_buf, title ? title : "",       sizeof(s_title_buf) - 1);
    strncpy(s_msg_buf,   message ? message : "",   sizeof(s_msg_buf) - 1);
    s_cat_buf[sizeof(s_cat_buf) - 1]     = 0;
    s_when_buf[sizeof(s_when_buf) - 1]   = 0;
    s_title_buf[sizeof(s_title_buf) - 1] = 0;
    s_msg_buf[sizeof(s_msg_buf) - 1]     = 0;
    s_notif_pending = true;
    portEXIT_CRITICAL(&s_mux);
}

// Append to the history (linear array, oldest..newest). When full, drop the
// oldest by shifting everything down one slot. Called on the LVGL task.
static void hist_push(const char* cat, const char* when, const char* title, const char* msg) {
    int idx;
    if (s_hist_count < HIST_MAX) {
        idx = s_hist_count;
        s_hist_count++;
    } else {
        for (int k = 0; k < HIST_MAX - 1; k++) s_hist[k] = s_hist[k + 1];
        idx = HIST_MAX - 1;
    }
    strncpy(s_hist[idx].cat,   cat,   sizeof(s_hist[idx].cat) - 1);
    strncpy(s_hist[idx].when,  when,  sizeof(s_hist[idx].when) - 1);
    strncpy(s_hist[idx].title, title, sizeof(s_hist[idx].title) - 1);
    strncpy(s_hist[idx].msg,   msg,   sizeof(s_hist[idx].msg) - 1);
    s_hist[idx].cat[sizeof(s_hist[idx].cat) - 1]     = 0;
    s_hist[idx].when[sizeof(s_hist[idx].when) - 1]   = 0;
    s_hist[idx].title[sizeof(s_hist[idx].title) - 1] = 0;
    s_hist[idx].msg[sizeof(s_hist[idx].msg) - 1]     = 0;
}

void ui_notify_drain(void) {
    if (s_status_pending) {
        char tmp[48];
        portENTER_CRITICAL(&s_mux);
        memcpy(tmp, s_status_buf, sizeof(tmp));
        s_status_pending = false;
        portEXIT_CRITICAL(&s_mux);
        if (s_status) lv_label_set_text(s_status, tmp);
    }
    if (s_notif_pending) {
        char cat[24], when[12], title[128], msg[160];
        portENTER_CRITICAL(&s_mux);
        memcpy(cat, s_cat_buf, sizeof(cat));
        memcpy(when, s_when_buf, sizeof(when));
        memcpy(title, s_title_buf, sizeof(title));
        memcpy(msg, s_msg_buf, sizeof(msg));
        s_notif_pending = false;
        portEXIT_CRITICAL(&s_mux);
        hist_push(cat, when, title, msg);
        rebuild_list();
    }
}
