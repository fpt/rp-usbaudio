// SPDX-License-Identifier: MIT
// Audio status UI for Waveshare RP2350-LCD-0.96 (160x80 ST7735S)

#include "audio_ui.h"
#include "lcd.h"
#include "splash.h"

#include <stdio.h>
#include <string.h>

//--------------------------------------------------------------------
// Colors
//--------------------------------------------------------------------

#define COLOR_ORANGE    0xFD20

//--------------------------------------------------------------------
// Layout constants for 160x80
//
//  Y=0..8    Title "USB Audio DAC"
//  Y=10      Separator
//  Y=12..19  Status + sample rate
//  Y=21..28  Volume
//  Y=30..38  Buffer fill bar   (actual ring buffer fill %)
//  Y=42..50  ASRC ratio bar    (resampling ratio, centred at 1.000)
//  Y=54..62  L VU bar
//  Y=66..74  R VU bar
//--------------------------------------------------------------------

#define TITLE_Y     0
#define SEP_Y       9
#define STATUS_Y    11
#define VOLUME_Y    21
#define BUF_Y       30
#define ASRC_Y      42
#define VU_L_Y      54
#define VU_R_Y      66

#define BAR_LABEL_W 18   // 2 chars (16px) + 2px gap
#define BAR_X       BAR_LABEL_W
#define BAR_W       (LCD_WIDTH - BAR_X - 1)  // 141px
#define BAR_H       9

//--------------------------------------------------------------------
// State
//--------------------------------------------------------------------

static audio_ui_state_t current_state      = AUDIO_UI_DISCONNECTED;
static uint32_t         current_sample_rate = 48000;
static uint8_t          current_bit_depth   = 16;
static uint8_t          current_channels    = 2;
static uint8_t          current_volume      = 100;
static bool             current_mute        = false;
static uint8_t          level_left          = 0;
static uint8_t          level_right         = 0;
static uint8_t          current_buf_fill    = 50;   // 0–100%
static uint32_t         current_asrc_ratio  = 1000; // x1000, 1000=1.000

static bool dirty_all    = true;
static bool dirty_state  = false;
static bool dirty_volume = false;
static bool dirty_level  = false;
static bool dirty_buf    = false;
static bool dirty_asrc   = false;

//--------------------------------------------------------------------
// Primitives (thin wrappers over splash helpers)
//--------------------------------------------------------------------

static void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c) {
    for (int py = y; py < y + h && py < LCD_HEIGHT; py++)
        for (int px = x; px < x + w && px < LCD_WIDTH; px++)
            if (px >= 0 && py >= 0)
                fb[py * LCD_WIDTH + px] = c;
}

static void draw_str(uint16_t *fb, int x, int y, const char *s,
                     uint16_t fg, uint16_t bg) {
    splash_draw_string(fb, LCD_WIDTH, LCD_HEIGHT, x, y, s, fg, bg, 1);
}

static void draw_centered(uint16_t *fb, int y, const char *s,
                          uint16_t fg, uint16_t bg) {
    int str_w = (int)strlen(s) * 8;
    int x = (LCD_WIDTH - str_w) / 2;
    if (x < 0) x = 0;
    draw_str(fb, x, y, s, fg, bg);
}

//--------------------------------------------------------------------
// Drawing routines
//--------------------------------------------------------------------

static void draw_title(uint16_t *fb) {
    fill_rect(fb, 0, TITLE_Y, LCD_WIDTH, 9, COLOR_DARK_GRAY);
    draw_centered(fb, TITLE_Y, "USB Audio DAC", COLOR_CYAN, COLOR_DARK_GRAY);
    // Separator
    fill_rect(fb, 0, SEP_Y, LCD_WIDTH, 1, COLOR_GRAY);
}

static void draw_state(uint16_t *fb) {
    fill_rect(fb, 0, STATUS_Y, LCD_WIDTH, 9, COLOR_DARK_GRAY);

    char buf[24];
    uint16_t color;

    switch (current_state) {
    case AUDIO_UI_DISCONNECTED:
        snprintf(buf, sizeof(buf), "Disconnected");
        color = COLOR_RED;
        break;
    case AUDIO_UI_CONNECTED:
        snprintf(buf, sizeof(buf), "Ready");
        color = COLOR_YELLOW;
        break;
    case AUDIO_UI_STREAMING:
        // "Playing 48.0kHz" = 15 chars × 8px = 120px (fits in 160)
        snprintf(buf, sizeof(buf), "Playing %u.%ukHz",
                 (unsigned)(current_sample_rate / 1000),
                 (unsigned)((current_sample_rate % 1000) / 100));
        color = COLOR_GREEN;
        break;
    default:
        snprintf(buf, sizeof(buf), "Unknown");
        color = COLOR_GRAY;
        break;
    }

    draw_centered(fb, STATUS_Y, buf, color, COLOR_DARK_GRAY);
}

static void draw_volume(uint16_t *fb) {
    fill_rect(fb, 0, VOLUME_Y, LCD_WIDTH, 9, COLOR_DARK_GRAY);

    char buf[16];
    uint16_t color;
    if (current_mute) {
        snprintf(buf, sizeof(buf), "Vol: MUTED");
        color = COLOR_RED;
    } else {
        snprintf(buf, sizeof(buf), "Vol: %d%%", current_volume);
        color = COLOR_WHITE;
    }
    draw_centered(fb, VOLUME_Y, buf, color, COLOR_DARK_GRAY);
}

// Draw a labelled horizontal bar at the given Y position.
// label: 2-char string (e.g. "BF", "L ", "R ")
// fill_pct: 0–100 fill percentage
// color: bar fill color
static void draw_hbar(uint16_t *fb, int bar_y, const char *label,
                      uint8_t fill_pct, uint16_t bar_color) {
    // Clear row
    fill_rect(fb, 0, bar_y, LCD_WIDTH, BAR_H + 1, COLOR_DARK_GRAY);

    // Label (left-aligned, vertically centred in bar)
    draw_str(fb, 0, bar_y, label, COLOR_GRAY, COLOR_DARK_GRAY);

    // Bar background
    fill_rect(fb, BAR_X, bar_y, BAR_W, BAR_H, COLOR_BLACK);

    // Bar fill
    int fill_w = (fill_pct * BAR_W) / 100;
    if (fill_w > 0)
        fill_rect(fb, BAR_X, bar_y, fill_w, BAR_H, bar_color);

    // Border
    // Top
    fill_rect(fb, BAR_X, bar_y, BAR_W, 1, COLOR_GRAY);
    // Bottom
    fill_rect(fb, BAR_X, bar_y + BAR_H - 1, BAR_W, 1, COLOR_GRAY);
    // Left
    fill_rect(fb, BAR_X, bar_y, 1, BAR_H, COLOR_GRAY);
    // Right
    fill_rect(fb, BAR_X + BAR_W - 1, bar_y, 1, BAR_H, COLOR_GRAY);
}

// Ring buffer fill bar: 0–100%, target 50%, center marker.
static void draw_buf(uint16_t *fb) {
    int32_t dev = (int32_t)current_buf_fill - 50;
    if (dev < 0) dev = -dev;
    uint16_t color;
    if (dev > 35)      color = COLOR_RED;
    else if (dev > 20) color = COLOR_ORANGE;
    else               color = COLOR_GREEN;

    draw_hbar(fb, BUF_Y, "BF", current_buf_fill, color);

    // Center marker (white tick at 50%)
    int cx = BAR_X + BAR_W / 2;
    fill_rect(fb, cx, BUF_Y + 1, 1, BAR_H - 2, COLOR_WHITE);
}

// ASRC ratio bar: maps ratio 980–1020 (±2%) to 0–100%, centre = 1.000.
static void draw_asrc(uint16_t *fb) {
    int32_t offset = (int32_t)current_asrc_ratio - 1000;
    int32_t pct    = 50 + (offset * 50) / 20;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    int32_t dev = offset < 0 ? -offset : offset;
    uint16_t color;
    if (dev > 15)     color = COLOR_RED;
    else if (dev > 8) color = COLOR_ORANGE;
    else              color = COLOR_CYAN;

    draw_hbar(fb, ASRC_Y, "AS", (uint8_t)pct, color);

    // Center marker
    int cx = BAR_X + BAR_W / 2;
    fill_rect(fb, cx, ASRC_Y + 1, 1, BAR_H - 2, COLOR_WHITE);
}

static void draw_vu_bar(uint16_t *fb, int bar_y, const char *label, uint8_t level) {
    uint16_t color;
    if (level > 90)      color = COLOR_RED;
    else if (level > 70) color = COLOR_ORANGE;
    else if (level > 50) color = COLOR_YELLOW;
    else                 color = COLOR_GREEN;

    draw_hbar(fb, bar_y, label, level, color);
}

static void draw_levels(uint16_t *fb) {
    draw_vu_bar(fb, VU_L_Y, "L ", level_left);
    draw_vu_bar(fb, VU_R_Y, "R ", level_right);
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void audio_ui_init(void) {
    dirty_all = true;
}

void audio_ui_draw(uint16_t *fb, int fb_width, int fb_height) {
    (void)fb_width; (void)fb_height;

    // Background
    fill_rect(fb, 0, 0, LCD_WIDTH, LCD_HEIGHT, COLOR_DARK_GRAY);

    draw_title(fb);
    draw_state(fb);
    draw_volume(fb);
    draw_buf(fb);
    draw_asrc(fb);
    draw_levels(fb);

    dirty_all = dirty_state = dirty_volume = dirty_buf = dirty_level = dirty_asrc = false;
}

void audio_ui_set_state(audio_ui_state_t state) {
    if (current_state != state) { current_state = state; dirty_state = true; }
}

void audio_ui_set_sample_rate(uint32_t sample_rate) {
    if (current_sample_rate != sample_rate) {
        current_sample_rate = sample_rate;
        dirty_state = true;
    }
}

void audio_ui_set_bit_depth(uint8_t bits) {
    if (current_bit_depth != bits) { current_bit_depth = bits; dirty_state = true; }
}

void audio_ui_set_channels(uint8_t channels) {
    if (current_channels != channels) { current_channels = channels; dirty_state = true; }
}

void audio_ui_set_volume(uint8_t volume) {
    if (current_volume != volume) { current_volume = volume; dirty_volume = true; }
}

void audio_ui_set_mute(bool muted) {
    if (current_mute != muted) { current_mute = muted; dirty_volume = true; }
}

void audio_ui_set_level(uint8_t left, uint8_t right) {
    if (level_left != left || level_right != right) {
        level_left = left; level_right = right;
        dirty_level = true;
    }
}

void audio_ui_set_buf_fill(uint8_t fill_pct) {
    if (current_buf_fill != fill_pct) { current_buf_fill = fill_pct; dirty_buf = true; }
}

void audio_ui_set_asrc_ratio(uint32_t ratio_x1000) {
    if (current_asrc_ratio != ratio_x1000) {
        current_asrc_ratio = ratio_x1000;
        dirty_asrc = true;
    }
}

bool audio_ui_needs_update(void) {
    return dirty_all || dirty_state || dirty_volume || dirty_buf || dirty_asrc || dirty_level;
}

bool audio_ui_update(uint16_t *fb, int fb_width, int fb_height) {
    (void)fb_width; (void)fb_height;

    if (dirty_all) {
        audio_ui_draw(fb, LCD_WIDTH, LCD_HEIGHT);
        return true;
    }

    bool updated = false;
    if (dirty_state)  { draw_state(fb);   dirty_state  = false; updated = true; }
    if (dirty_volume) { draw_volume(fb);  dirty_volume = false; updated = true; }
    if (dirty_buf)    { draw_buf(fb);     dirty_buf    = false; updated = true; }
    if (dirty_asrc)   { draw_asrc(fb);    dirty_asrc   = false; updated = true; }
    if (dirty_level)  { draw_levels(fb);  dirty_level  = false; updated = true; }

    return updated;
}

