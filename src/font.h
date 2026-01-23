// SPDX-License-Identifier: MIT
// Simple 8x8 bitmap font for status display

#ifndef _FONT_H_
#define _FONT_H_

#include <stdint.h>

// 8x8 pixel font
#define FONT_WIDTH  8
#define FONT_HEIGHT 8

// Get font bitmap for ASCII character (32-127)
// Returns pointer to 8 bytes, each byte is one row (MSB = left pixel)
const uint8_t *font_get_char(char c);

#endif /* _FONT_H_ */
