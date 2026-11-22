/**
 * ui_theme.h — UI color roles (single source of truth for the palette).
 *
 * Light theme sampled from the lvgl_theme_shadcn "Buttons" reference image
 * (AlixANNERAUD/lvgl_theme_shadcn, MIT): shadcn/ui zinc base, no accent.
 * Values below are the shadcn zinc tokens; on-screen sampling of the
 * reference matched them within antialiasing error:
 *   background #FFFFFF, primary #1E1E21 (~zinc-900), secondary #F4F4F5,
 *   border #E3E3E6 (~zinc-200), text #111113 (~zinc-950), muted #70707A
 *   (~zinc-500), ring #9E9EA8 (~zinc-400), destructive #E60013.
 * No code was copied from that project; only the palette is reused.
 *
 * Status colors (OK / WARN / ALERT) are not in the reference; they use darker
 * green / amber / red so they stay readable on a white background.
 *
 * Color encoding: values are native RGB565 in lv_color_t.full, the same
 * convention the project already used (display.cpp lvgl_flush_cb hands
 * color_p->full to LovyanGFX writePixels as plain RGB565). Do NOT switch these
 * to lv_color_hex(): with LV_COLOR_16_SWAP=1 that would byte-swap them.
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include <lvgl.h>

// 8-bit-per-channel RGB -> native RGB565 value.
#define UI_RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)))

// Surfaces
static const lv_color_t COL_BG       = { .full = UI_RGB565(0xFF, 0xFF, 0xFF) };  // screen background
static const lv_color_t COL_PANEL    = { .full = UI_RGB565(0xF4, 0xF4, 0xF5) };  // list bodies / cards (secondary)
static const lv_color_t COL_NORMAL   = { .full = UI_RGB565(0xF4, 0xF4, 0xF5) };  // idle button fill (secondary)
static const lv_color_t COL_FOCUS    = { .full = UI_RGB565(0x18, 0x18, 0x1B) };  // focused button fill (primary)
static const lv_color_t COL_BORDER   = { .full = UI_RGB565(0xE4, 0xE4, 0xE7) };  // 1px outlines / dividers
static const lv_color_t COL_RING     = { .full = UI_RGB565(0xA1, 0xA1, 0xAA) };  // focus ring (reserved)

// Text. Contrast vs white: TEXT ~19:1, SOFT ~10:1, MUTED (red) ~4.6:1.
static const lv_color_t COL_TEXT     = { .full = UI_RGB565(0x09, 0x09, 0x0B) };  // primary text
static const lv_color_t COL_SOFT     = { .full = UI_RGB565(0x3F, 0x3F, 0x46) };  // labels / secondary values
static const lv_color_t COL_MUTED    = { .full = UI_RGB565(0xE7, 0x00, 0x0B) };  // hints / disabled / "off" (red, per request)
static const lv_color_t COL_ON_FOCUS = { .full = UI_RGB565(0xFA, 0xFA, 0xFA) };  // text on COL_FOCUS fill

// Accent: the reference has no color accent (monochrome primary).
static const lv_color_t COL_ACCENT   = { .full = UI_RGB565(0x18, 0x18, 0x1B) };  // emphasized values

// State colors (darker shades for a white background)
static const lv_color_t COL_OK       = { .full = UI_RGB565(0xE7, 0x00, 0x0B) };  // connected / healthy (red, per request)
static const lv_color_t COL_WARN     = { .full = UI_RGB565(0xB4, 0x53, 0x09) };  // busy / waiting
static const lv_color_t COL_ALERT    = { .full = UI_RGB565(0xE7, 0x00, 0x0B) };  // error / disconnected

// Status bar background: white with a bottom divider, like the reference header.
#define COL_STATUS  COL_BG

#endif  // UI_THEME_H
