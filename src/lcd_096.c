// SPDX-License-Identifier: MIT
// ST7735S LCD driver for Waveshare RP2350-LCD-0.96 (160x80)
// Based on Waveshare sample code: RP2350-LCD-0/C/lib/LCD/LCD_0in96.c

#include "lcd.h"

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

//--------------------------------------------------------------------
// ST7735S Column/Row address offsets for this panel
// The physical memory starts at (1, 26) relative to pixel (0, 0)
//--------------------------------------------------------------------

#define LCD_X_OFFSET 1
#define LCD_Y_OFFSET 26

//--------------------------------------------------------------------
// Backlight PWM
//--------------------------------------------------------------------

#define PWM_FREQ  10000
#define PWM_WRAP  1000

static uint pwm_slice_num;
static uint pwm_channel;

//--------------------------------------------------------------------
// Low-level helpers
//--------------------------------------------------------------------

static void send_cmd(uint8_t cmd) {
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI, &cmd, 1);
    gpio_put(LCD_PIN_CS, 1);
}

static void send_data(uint8_t data) {
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);
    spi_write_blocking(LCD_SPI, &data, 1);
    gpio_put(LCD_PIN_CS, 1);
}

// Set write window — applies hardware offsets internally
static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    x0 += LCD_X_OFFSET;
    x1 += LCD_X_OFFSET;
    y0 += LCD_Y_OFFSET;
    y1 += LCD_Y_OFFSET;

    send_cmd(0x2A);  // CASET
    send_data(x0 >> 8);
    send_data(x0 & 0xFF);
    send_data(x1 >> 8);
    send_data(x1 & 0xFF);

    send_cmd(0x2B);  // RASET
    send_data(y0 >> 8);
    send_data(y0 & 0xFF);
    send_data(y1 >> 8);
    send_data(y1 & 0xFF);

    send_cmd(0x2C);  // RAMWR
}

// Send pixel data (RGB565 values, big-endian over SPI)
// RP2350 is little-endian so bytes must be swapped before sending.
static void send_pixels(const uint16_t *data, uint32_t count) {
    // Use a small row buffer to batch byte-swapped writes
    uint8_t buf[LCD_WIDTH * 2];

    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_CS, 0);

    while (count > 0) {
        uint32_t chunk = count < LCD_WIDTH ? count : LCD_WIDTH;
        for (uint32_t i = 0; i < chunk; i++) {
            buf[i * 2]     = data[i] >> 8;
            buf[i * 2 + 1] = data[i] & 0xFF;
        }
        spi_write_blocking(LCD_SPI, buf, chunk * 2);
        data  += chunk;
        count -= chunk;
    }

    gpio_put(LCD_PIN_CS, 1);
}

//--------------------------------------------------------------------
// ST7735S register initialisation (from Waveshare RP2350-LCD-0 sample)
//--------------------------------------------------------------------

static void lcd_reg_init(void) {
    send_cmd(0x11);  // Sleep exit
    sleep_ms(120);

    send_cmd(0x21);
    send_cmd(0x21);

    send_cmd(0xB1);
    send_data(0x05); send_data(0x3A); send_data(0x3A);

    send_cmd(0xB2);
    send_data(0x05); send_data(0x3A); send_data(0x3A);

    send_cmd(0xB3);
    send_data(0x05); send_data(0x3A); send_data(0x3A);
    send_data(0x05); send_data(0x3A); send_data(0x3A);

    send_cmd(0xB4);
    send_data(0x03);

    send_cmd(0xC0);
    send_data(0x62); send_data(0x02); send_data(0x04);

    send_cmd(0xC1);
    send_data(0xC0);

    send_cmd(0xC2);
    send_data(0x0D); send_data(0x00);

    send_cmd(0xC3);
    send_data(0x8D); send_data(0x6A);

    send_cmd(0xC4);
    send_data(0x8D); send_data(0xEE);

    send_cmd(0xC5);  // VCOM
    send_data(0x0E);

    send_cmd(0xE0);  // Positive gamma
    send_data(0x10); send_data(0x0E); send_data(0x02); send_data(0x03);
    send_data(0x0E); send_data(0x07); send_data(0x02); send_data(0x07);
    send_data(0x0A); send_data(0x12); send_data(0x27); send_data(0x37);
    send_data(0x00); send_data(0x0D); send_data(0x0E); send_data(0x10);

    send_cmd(0xE1);  // Negative gamma
    send_data(0x10); send_data(0x0E); send_data(0x03); send_data(0x03);
    send_data(0x0F); send_data(0x06); send_data(0x02); send_data(0x08);
    send_data(0x0A); send_data(0x13); send_data(0x26); send_data(0x36);
    send_data(0x00); send_data(0x0D); send_data(0x0E); send_data(0x10);

    send_cmd(0x3A);  // Pixel format: 16-bit RGB565
    send_data(0x05);

    send_cmd(0x36);  // MADCTL: MV+MX+BGR → landscape 160x80, rotated 180°
    send_data(0x68);

    send_cmd(0x29);  // Display on
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void lcd_init(void) {
    // SPI1 at 10 MHz, Mode 0 (CPOL=0, CPHA=0)
    spi_init(LCD_SPI, LCD_SPI_FREQ);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    gpio_set_function(LCD_PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);

    // Control pins
    gpio_init(LCD_PIN_CS);  gpio_set_dir(LCD_PIN_CS,  GPIO_OUT); gpio_put(LCD_PIN_CS,  1);
    gpio_init(LCD_PIN_DC);  gpio_set_dir(LCD_PIN_DC,  GPIO_OUT); gpio_put(LCD_PIN_DC,  0);
    gpio_init(LCD_PIN_RST); gpio_set_dir(LCD_PIN_RST, GPIO_OUT); gpio_put(LCD_PIN_RST, 1);

    // Backlight PWM on GPIO25 (slice 12, channel B)
    float sys_clk = (float)clock_get_hz(clk_sys);
    gpio_set_function(LCD_PIN_BL, GPIO_FUNC_PWM);
    pwm_slice_num = pwm_gpio_to_slice_num(LCD_PIN_BL);
    pwm_channel   = pwm_gpio_to_channel(LCD_PIN_BL);
    pwm_set_clkdiv(pwm_slice_num, sys_clk / (PWM_FREQ * PWM_WRAP));
    pwm_set_wrap(pwm_slice_num, PWM_WRAP);
    pwm_set_chan_level(pwm_slice_num, pwm_channel, 0);
    pwm_set_enabled(pwm_slice_num, true);

    // Hardware reset
    gpio_put(LCD_PIN_RST, 1); sleep_ms(200);
    gpio_put(LCD_PIN_RST, 0); sleep_ms(200);
    gpio_put(LCD_PIN_RST, 1); sleep_ms(200);

    lcd_reg_init();

    lcd_set_backlight(90);
}

void lcd_set_backlight(uint8_t brightness) {
    if (brightness > 100) brightness = 100;
    pwm_set_chan_level(pwm_slice_num, pwm_channel, PWM_WRAP / 100 * brightness);
}

void lcd_update_wait(void) {
    // No-op: all transfers are blocking
}

bool lcd_is_busy(void) {
    return false;
}

void lcd_update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                const uint16_t *data, uint32_t len) {
    (void)len;
    set_window(x, y, x + width - 1, y + height - 1);
    send_pixels(data, width * height);
}

//--------------------------------------------------------------------
// Chunked Update API (immediate execution — no real chunking needed)
//--------------------------------------------------------------------

static struct {
    bool active;
    uint32_t x, y, w, h;
    const uint16_t *data;
} chunk_state;

void lcd_update_chunked_start(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                               const uint16_t *data, uint32_t rows_per_chunk) {
    (void)rows_per_chunk;
    // Execute immediately — display is small enough for one blocking write
    lcd_update(x, y, width, height, data, width * height * 2);
    chunk_state.active = false;
}

bool lcd_update_chunked_continue(void) {
    return false;
}

bool lcd_update_chunked_active(void) {
    return false;
}
