// SPDX-License-Identifier: MIT
// Audio status UI for Waveshare RP2350-Touch-LCD-2.8 (320x240 landscape)

#include "audio_ui.h"

#include <stdio.h>
#include <string.h>

#include "splash.h"

//--------------------------------------------------------------------
// Colors
//--------------------------------------------------------------------

#define COLOR_ORANGE  0xFD20

//--------------------------------------------------------------------
// Layout (320x240)
//
//  Y=  4        Title "USB AUDIO DAC"  (2x scale = 16px)
//  Y= 24        Status + format        (1x = 8px)
//  Y= 36        Volume                 (1x = 8px)
//  Y= 47        separator
//  Y= 52        BUF bar  (h=14)
//  Y= 70        ASRC bar (h=14)
//  Y= 88        separator
//  Y= 93        "L" / "R" labels
//  Y=105        Vertical VU bars       (h=120)
//  Y=229        Level percentage text
//--------------------------------------------------------------------

#define TITLE_Y    4
#define STATUS_Y   24
#define VOLUME_Y   36
#define SEP1_Y     47
#define BUF_Y      52
#define ASRC_Y     70
#define SEP2_Y     88
#define VU_LABEL_Y 93
#define VU_BAR_Y   105
#define VU_BAR_H   120
#define VU_VAL_Y   229

// Left and right bar geometry
#define VU_L_X     40
#define VU_R_X     180
#define VU_BAR_W   100

// Small horizontal bars (BUF, ASRC)
#define HBAR_LABEL_X  2
#define HBAR_LABEL_W  38
#define HBAR_X        (HBAR_LABEL_X + HBAR_LABEL_W)   // 40
#define HBAR_W        216
#define HBAR_VAL_X    (HBAR_X + HBAR_W + 4)           // 260
#define HBAR_H        14

//--------------------------------------------------------------------
// State
//--------------------------------------------------------------------

static audio_ui_state_t current_state       = AUDIO_UI_DISCONNECTED;
static uint32_t         current_sample_rate = 48000;
static uint8_t          current_bit_depth   = 16;
static uint8_t          current_channels    = 2;
static uint8_t          current_volume      = 100;
static bool             current_mute        = false;
static uint8_t          current_buf_fill    = 50;
static uint32_t         current_asrc_ratio  = 1000;
static uint8_t          level_left          = 0;
static uint8_t          level_right         = 0;

static bool dirty_all    = true;
static bool dirty_state  = false;
static bool dirty_volume = false;
static bool dirty_meters = false;

//--------------------------------------------------------------------
// Primitives
//--------------------------------------------------------------------

static void fill_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c) {
    for (int py = y; py < y + h && py < 240; py++)
        for (int px = x; px < x + w && px < 320; px++)
            if (px >= 0 && py >= 0)
                fb[py * 320 + px] = c;
}

static void draw_str(uint16_t *fb, int x, int y, const char *s,
                     uint16_t fg, uint16_t bg, int scale) {
    splash_draw_string(fb, 320, 240, x, y, s, fg, bg, scale);
}

static void draw_centered(uint16_t *fb, int y, const char *s,
                          uint16_t fg, uint16_t bg, int scale) {
    int w = (int)strlen(s) * 8 * scale;
    int x = (320 - w) / 2;
    if (x < 0) x = 0;
    draw_str(fb, x, y, s, fg, bg, scale);
}

//--------------------------------------------------------------------
// Sections
//--------------------------------------------------------------------

static void draw_title(uint16_t *fb) {
    fill_rect(fb, 0, TITLE_Y - 2, 320, 20, COLOR_DARK_GRAY);
    draw_centered(fb, TITLE_Y, "USB AUDIO DAC", COLOR_CYAN, COLOR_DARK_GRAY, 2);
}

static void draw_state(uint16_t *fb) {
    fill_rect(fb, 0, STATUS_Y, 320, 9, COLOR_DARK_GRAY);
    char buf[48];
    uint16_t color;
    const char *ch = (current_channels == 1) ? "Mono" : "Stereo";
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
        snprintf(buf, sizeof(buf), "Playing: %u.%ukHz %ubit %s",
                 (unsigned)(current_sample_rate / 1000),
                 (unsigned)((current_sample_rate % 1000) / 100),
                 current_bit_depth, ch);
        color = COLOR_GREEN;
        break;
    default:
        snprintf(buf, sizeof(buf), "Unknown");
        color = COLOR_GRAY;
        break;
    }
    draw_centered(fb, STATUS_Y, buf, color, COLOR_DARK_GRAY, 1);
}

static void draw_volume(uint16_t *fb) {
    fill_rect(fb, 0, VOLUME_Y, 320, 9, COLOR_DARK_GRAY);
    char buf[20];
    uint16_t color;
    if (current_mute) {
        snprintf(buf, sizeof(buf), "Vol: MUTED");
        color = COLOR_RED;
    } else {
        snprintf(buf, sizeof(buf), "Vol: %u%%", current_volume);
        color = COLOR_WHITE;
    }
    draw_centered(fb, VOLUME_Y, buf, color, COLOR_DARK_GRAY, 1);
}

// Generic small horizontal bar with label (left), fill, center marker, and value (right).
// val_str must be a fixed-width string to avoid stale characters.
static void draw_hbar(uint16_t *fb, int y, const char *label,
                      uint8_t fill_pct, uint16_t bar_color, bool center_marker,
                      const char *val_str) {
    fill_rect(fb, 0, y, 320, HBAR_H + 2, COLOR_DARK_GRAY);
    draw_str(fb, HBAR_LABEL_X, y + (HBAR_H - 8) / 2, label,
             COLOR_GRAY, COLOR_DARK_GRAY, 1);

    fill_rect(fb, HBAR_X, y, HBAR_W, HBAR_H, COLOR_BLACK);

    int fill_w = (fill_pct * HBAR_W) / 100;
    if (fill_w > 0)
        fill_rect(fb, HBAR_X, y, fill_w, HBAR_H, bar_color);

    if (center_marker) {
        int cx = HBAR_X + HBAR_W / 2;
        fill_rect(fb, cx, y, 1, HBAR_H, COLOR_WHITE);
    }

    // Border
    fill_rect(fb, HBAR_X,             y,              HBAR_W, 1,      COLOR_GRAY);
    fill_rect(fb, HBAR_X,             y + HBAR_H - 1, HBAR_W, 1,      COLOR_GRAY);
    fill_rect(fb, HBAR_X,             y,              1,      HBAR_H, COLOR_GRAY);
    fill_rect(fb, HBAR_X + HBAR_W-1, y,               1,      HBAR_H, COLOR_GRAY);

    draw_str(fb, HBAR_VAL_X, y + (HBAR_H - 8) / 2, val_str,
             COLOR_WHITE, COLOR_DARK_GRAY, 1);
}

static void draw_buf(uint16_t *fb) {
    uint16_t color;
    int32_t dev = (int32_t)current_buf_fill - 50;
    if (dev < 0) dev = -dev;
    if (dev > 35)      color = COLOR_RED;
    else if (dev > 20) color = COLOR_YELLOW;
    else               color = COLOR_GREEN;

    // Fixed 4-char width prevents stale characters (e.g. " 0%" not "0%")
    char val[8];
    snprintf(val, sizeof(val), "%3u%%", current_buf_fill);
    draw_hbar(fb, BUF_Y, "BUF ", current_buf_fill, color, true, val);
}

static void draw_asrc(uint16_t *fb) {
    int32_t offset = (int32_t)current_asrc_ratio - 1000;
    int32_t pct    = 50 + (offset * 50) / 20;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    uint16_t color;
    int32_t dev = offset < 0 ? -offset : offset;
    if (dev > 15)     color = COLOR_RED;
    else if (dev > 8) color = COLOR_ORANGE;
    else              color = COLOR_CYAN;

    // Always 5 chars: "1.002"
    char val[12];
    snprintf(val, sizeof(val), "%lu.%03lu",
             (unsigned long)(current_asrc_ratio / 1000),
             (unsigned long)(current_asrc_ratio % 1000));
    draw_hbar(fb, ASRC_Y, "ASRC", (uint8_t)pct, color, true, val);
}

// Vertical VU bar filling from bottom. Gradient: green/yellow/orange/red zones
// are always drawn; the unfilled top is masked with black.
static void draw_vbar(uint16_t *fb, int bar_x, int bar_y, int bar_w, int bar_h,
                      uint8_t level, const char *label) {
    // Label centered above bar
    int label_x = bar_x + (bar_w - 8) / 2;
    fill_rect(fb, bar_x, VU_LABEL_Y, bar_w, 9, COLOR_DARK_GRAY);
    draw_str(fb, label_x, VU_LABEL_Y, label, COLOR_GRAY, COLOR_DARK_GRAY, 1);

    // Gradient bands (bottom to top): green 0-55%, yellow 55-75%, orange 75-90%, red 90-100%
    int green_h  = (55 * bar_h) / 100;
    int yellow_h = (20 * bar_h) / 100;
    int orange_h = (15 * bar_h) / 100;
    int red_h    = bar_h - green_h - yellow_h - orange_h;

    int bottom = bar_y + bar_h;
    fill_rect(fb, bar_x, bottom - green_h,                           bar_w, green_h,  COLOR_GREEN);
    fill_rect(fb, bar_x, bottom - green_h - yellow_h,                bar_w, yellow_h, COLOR_YELLOW);
    fill_rect(fb, bar_x, bottom - green_h - yellow_h - orange_h,     bar_w, orange_h, COLOR_ORANGE);
    fill_rect(fb, bar_x, bar_y,                                      bar_w, red_h,    COLOR_RED);

    // Mask unfilled portion with black
    int fill_h = (level * bar_h) / 100;
    int empty_h = bar_h - fill_h;
    if (empty_h > 0)
        fill_rect(fb, bar_x, bar_y, bar_w, empty_h, COLOR_BLACK);

    // Border
    fill_rect(fb, bar_x,           bar_y,          bar_w, 1,     COLOR_GRAY);
    fill_rect(fb, bar_x,           bar_y + bar_h-1, bar_w, 1,    COLOR_GRAY);
    fill_rect(fb, bar_x,           bar_y,          1,     bar_h, COLOR_GRAY);
    fill_rect(fb, bar_x + bar_w-1, bar_y,          1,     bar_h, COLOR_GRAY);

    // Value text centered below bar, fixed 4-char width
    char val[8];
    snprintf(val, sizeof(val), "%3u%%", level);
    int val_x = bar_x + (bar_w - (int)strlen(val) * 8) / 2;
    fill_rect(fb, bar_x, VU_VAL_Y, bar_w, 9, COLOR_DARK_GRAY);
    draw_str(fb, val_x, VU_VAL_Y, val, COLOR_WHITE, COLOR_DARK_GRAY, 1);
}

static void draw_separators(uint16_t *fb) {
    fill_rect(fb, 4, SEP1_Y, 312, 1, COLOR_GRAY);
    fill_rect(fb, 4, SEP2_Y, 312, 1, COLOR_GRAY);
}

static void draw_meters(uint16_t *fb) {
    draw_buf(fb);
    draw_asrc(fb);
    draw_vbar(fb, VU_L_X, VU_BAR_Y, VU_BAR_W, VU_BAR_H, level_left,  "L");
    draw_vbar(fb, VU_R_X, VU_BAR_Y, VU_BAR_W, VU_BAR_H, level_right, "R");
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void audio_ui_init(void) {
    dirty_all = true;
}

void audio_ui_draw(uint16_t *fb, int fb_width, int fb_height) {
    (void)fb_width; (void)fb_height;
    fill_rect(fb, 0, 0, 320, 240, COLOR_DARK_GRAY);
    draw_title(fb);
    draw_state(fb);
    draw_volume(fb);
    draw_separators(fb);
    draw_meters(fb);
    dirty_all = dirty_state = dirty_volume = dirty_meters = false;
}

void audio_ui_set_state(audio_ui_state_t state) {
    if (current_state != state) { current_state = state; dirty_state = true; }
}

void audio_ui_set_sample_rate(uint32_t sr) {
    if (current_sample_rate != sr) { current_sample_rate = sr; dirty_state = true; }
}

void audio_ui_set_bit_depth(uint8_t bits) {
    if (current_bit_depth != bits) { current_bit_depth = bits; dirty_state = true; }
}

void audio_ui_set_channels(uint8_t ch) {
    if (current_channels != ch) { current_channels = ch; dirty_state = true; }
}

void audio_ui_set_volume(uint8_t vol) {
    if (current_volume != vol) { current_volume = vol; dirty_volume = true; }
}

void audio_ui_set_mute(bool muted) {
    if (current_mute != muted) { current_mute = muted; dirty_volume = true; }
}

void audio_ui_set_buf_fill(uint8_t pct) {
    if (current_buf_fill != pct) { current_buf_fill = pct; dirty_meters = true; }
}

void audio_ui_set_asrc_ratio(uint32_t ratio_x1000) {
    if (current_asrc_ratio != ratio_x1000) {
        current_asrc_ratio = ratio_x1000;
        dirty_meters = true;
    }
}

void audio_ui_set_level(uint8_t left, uint8_t right) {
    if (level_left != left || level_right != right) {
        level_left = left; level_right = right;
        dirty_meters = true;
    }
}

bool audio_ui_needs_update(void) {
    return dirty_all || dirty_state || dirty_volume || dirty_meters;
}

bool audio_ui_update(uint16_t *fb, int fb_width, int fb_height) {
    (void)fb_width; (void)fb_height;
    if (dirty_all) {
        audio_ui_draw(fb, 320, 240);
        return true;
    }
    bool updated = false;
    if (dirty_state)  { draw_state(fb);   dirty_state  = false; updated = true; }
    if (dirty_volume) { draw_volume(fb);  dirty_volume = false; updated = true; }
    if (dirty_meters) { draw_meters(fb);  dirty_meters = false; updated = true; }
    return updated;
}
