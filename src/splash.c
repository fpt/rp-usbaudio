// SPDX-License-Identifier: MIT
// Splash screen display before GUD session starts

#include "splash.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

void splash_draw_char(uint16_t *fb, int fb_width, int fb_height,
                      int x, int y, char c,
                      uint16_t fg_color, uint16_t bg_color, int scale) {
    const uint8_t *bitmap = font_get_char(c);

    for (int row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = bitmap[row];
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint16_t color = (bits & 0x80) ? fg_color : bg_color;
            bits <<= 1;

            // Draw scaled pixel
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int px = x + col * scale + sx;
                    int py = y + row * scale + sy;
                    if (px >= 0 && px < fb_width && py >= 0 && py < fb_height) {
                        fb[py * fb_width + px] = color;
                    }
                }
            }
        }
    }
}

void splash_draw_string(uint16_t *fb, int fb_width, int fb_height,
                        int x, int y, const char *str,
                        uint16_t fg_color, uint16_t bg_color, int scale) {
    int char_width = FONT_WIDTH * scale;

    while (*str) {
        splash_draw_char(fb, fb_width, fb_height, x, y, *str,
                         fg_color, bg_color, scale);
        x += char_width;
        str++;
    }
}

// Helper to draw centered string
static void draw_centered(uint16_t *fb, int fb_width, int fb_height,
                          int y, const char *str,
                          uint16_t fg_color, uint16_t bg_color, int scale) {
    int char_width = FONT_WIDTH * scale;
    int str_width = strlen(str) * char_width;
    int x = (fb_width - str_width) / 2;
    splash_draw_string(fb, fb_width, fb_height, x, y, str,
                       fg_color, bg_color, scale);
}

// Helper to fill rectangle
static void fill_rect(uint16_t *fb, int fb_width, int fb_height,
                      int x, int y, int w, int h, uint16_t color) {
    for (int py = y; py < y + h && py < fb_height; py++) {
        for (int px = x; px < x + w && px < fb_width; px++) {
            if (px >= 0 && py >= 0) {
                fb[py * fb_width + px] = color;
            }
        }
    }
}

void splash_draw(uint16_t *fb, int fb_width, int fb_height) {
    // Fill background with dark gray
    for (int i = 0; i < fb_width * fb_height; i++) {
        fb[i] = COLOR_DARK_GRAY;
    }

    int y = 40;
    int line_height_large = FONT_HEIGHT * 3 + 8;
    int line_height_small = FONT_HEIGHT * 2 + 4;

    // Title
    draw_centered(fb, fb_width, fb_height, y, "GUD Display",
                  COLOR_WHITE, COLOR_DARK_GRAY, 3);
    y += line_height_large;

    // Subtitle
    draw_centered(fb, fb_width, fb_height, y, "RP2350 + Audio",
                  COLOR_CYAN, COLOR_DARK_GRAY, 2);
    y += line_height_small + 20;

    // Separator line
    fill_rect(fb, fb_width, fb_height,
              20, y, fb_width - 40, 2, COLOR_GRAY);
    y += 20;

    // Resolution info
    char buf[32];
    snprintf(buf, sizeof(buf), "Resolution: %dx%d", fb_width, fb_height);
    draw_centered(fb, fb_width, fb_height, y, buf,
                  COLOR_WHITE, COLOR_DARK_GRAY, 1);
    y += line_height_small;

    // Format info
    draw_centered(fb, fb_width, fb_height, y, "Format: RGB565",
                  COLOR_WHITE, COLOR_DARK_GRAY, 1);
    y += line_height_small;

    // USB info
    draw_centered(fb, fb_width, fb_height, y, "USB: 16d0:10a9",
                  COLOR_WHITE, COLOR_DARK_GRAY, 1);
    y += line_height_small + 30;

    // Status message
    draw_centered(fb, fb_width, fb_height, y, "Waiting for host...",
                  COLOR_YELLOW, COLOR_DARK_GRAY, 2);
    y += line_height_small + 10;

    // Hint
    draw_centered(fb, fb_width, fb_height, y + 20, "Connect to Linux with",
                  COLOR_GRAY, COLOR_DARK_GRAY, 1);
    draw_centered(fb, fb_width, fb_height, y + 40, "GUD driver (kernel 5.13+)",
                  COLOR_GRAY, COLOR_DARK_GRAY, 1);
}
