// SPDX-License-Identifier: MIT
// Splash screen display before GUD session starts

#ifndef _SPLASH_H_
#define _SPLASH_H_

#include <stdint.h>

// RGB565 color definitions
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_YELLOW  0xFFE0
#define COLOR_GRAY    0x8410
#define COLOR_DARK_GRAY 0x4208

// Draw a character at position (x, y) with given colors
// scale: 1 = 8x8, 2 = 16x16, etc.
void splash_draw_char(uint16_t *fb, int fb_width, int fb_height,
                      int x, int y, char c,
                      uint16_t fg_color, uint16_t bg_color, int scale);

// Draw a string at position (x, y)
void splash_draw_string(uint16_t *fb, int fb_width, int fb_height,
                        int x, int y, const char *str,
                        uint16_t fg_color, uint16_t bg_color, int scale);

// Draw the splash screen showing device info
void splash_draw(uint16_t *fb, int fb_width, int fb_height);

#endif /* _SPLASH_H_ */
