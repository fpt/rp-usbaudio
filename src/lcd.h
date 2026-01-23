// SPDX-License-Identifier: MIT
// LCD driver interface — board selected at build time via -DBOARD_096

#ifndef _LCD_H_
#define _LCD_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef BOARD_096
//--------------------------------------------------------------------
// Waveshare RP2350-LCD-0.96 : 160x80 ST7735S (landscape via MADCTL)
//--------------------------------------------------------------------

#define LCD_WIDTH   160
#define LCD_HEIGHT  80

#define LCD_SPI       spi1
#define LCD_SPI_FREQ  (10 * 1000 * 1000)

#define LCD_PIN_SCK   10
#define LCD_PIN_MOSI  11
#define LCD_PIN_CS    9
#define LCD_PIN_DC    8
#define LCD_PIN_RST   12
#define LCD_PIN_BL    25

#else
//--------------------------------------------------------------------
// Waveshare RP2350-Touch-LCD-2.8 : 320x240 ST7789T3 (landscape, 90° CW)
//--------------------------------------------------------------------

#define LCD_WIDTH   320
#define LCD_HEIGHT  240

// Rotation: 0=0deg, 1=90deg CW, 2=180deg, 3=270deg CW
#define LCD_ROTATION  1
#define LCD_Y_OFFSET  0

#define LCD_SPI       spi1
#define LCD_SPI_FREQ  (40 * 1000 * 1000)

#define LCD_PIN_SCK   10
#define LCD_PIN_MOSI  11
#define LCD_PIN_CS    13
#define LCD_PIN_DC    14
#define LCD_PIN_RST   15
#define LCD_PIN_BL    16

#endif /* BOARD_096 */

//--------------------------------------------------------------------
// API (identical for both boards)
//--------------------------------------------------------------------

void lcd_init(void);
void lcd_set_backlight(uint8_t brightness);
void lcd_update_wait(void);
bool lcd_is_busy(void);
void lcd_update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                const uint16_t *data, uint32_t len);

void lcd_update_chunked_start(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                               const uint16_t *data, uint32_t rows_per_chunk);
bool lcd_update_chunked_continue(void);
bool lcd_update_chunked_active(void);

#endif /* _LCD_H_ */
