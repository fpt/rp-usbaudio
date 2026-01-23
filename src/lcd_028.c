// SPDX-License-Identifier: MIT
// ST7789 LCD driver for GUD display
// Based on rp2350-trackpad bsp_st7789.c

#include "lcd.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

//--------------------------------------------------------------------
// ST7789 Commands
//--------------------------------------------------------------------

#define ST7789_NOP       0x00
#define ST7789_SWRESET   0x01
#define ST7789_SLPIN     0x10
#define ST7789_SLPOUT    0x11
#define ST7789_PTLON     0x12
#define ST7789_NORON     0x13
#define ST7789_INVOFF    0x20
#define ST7789_INVON     0x21
#define ST7789_DISPOFF   0x28
#define ST7789_DISPON    0x29
#define ST7789_CASET     0x2A
#define ST7789_RASET     0x2B
#define ST7789_RAMWR     0x2C
#define ST7789_MADCTL    0x36
#define ST7789_COLMOD    0x3A

//--------------------------------------------------------------------
// Backlight PWM Configuration
//--------------------------------------------------------------------

#define PWM_FREQ    10000
#define PWM_WRAP    1000

//--------------------------------------------------------------------
// Internal State
//--------------------------------------------------------------------

static int dma_channel = -1;
static volatile bool dma_busy = false;
static uint pwm_slice_num;
static uint pwm_channel;

// Chunked update state
static struct {
    bool active;
    uint32_t x, y, width, height;
    const uint16_t *data;
    uint32_t rows_per_chunk;
    uint32_t current_row;
} chunked_state = {0};

//--------------------------------------------------------------------
// Low-level SPI functions
//--------------------------------------------------------------------

static void lcd_write_cmd(uint8_t cmd) {
    gpio_put(LCD_PIN_CS, 0);
    gpio_put(LCD_PIN_DC, 0);
    spi_write_blocking(LCD_SPI, &cmd, 1);
    gpio_put(LCD_PIN_CS, 1);
}

static void lcd_write_data(uint8_t data) {
    sleep_us(100);
    gpio_put(LCD_PIN_CS, 0);
    gpio_put(LCD_PIN_DC, 1);
    spi_write_blocking(LCD_SPI, &data, 1);
    gpio_put(LCD_PIN_CS, 1);
}

static void lcd_write_data_buf(const uint8_t *data, size_t len) {
    gpio_put(LCD_PIN_CS, 0);
    gpio_put(LCD_PIN_DC, 1);
    spi_write_blocking(LCD_SPI, data, len);
    gpio_put(LCD_PIN_CS, 1);
}

// Fast window set - sends all params in batch without per-byte delays
static void lcd_set_window_fast(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Apply Y offset (for displays where memory doesn't start at 0)
    y0 += LCD_Y_OFFSET;
    y1 += LCD_Y_OFFSET;

    uint8_t caset_data[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t raset_data[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};

    // Column address set (CASET)
    lcd_write_cmd(ST7789_CASET);
    lcd_write_data_buf(caset_data, 4);

    // Row address set (RASET)
    lcd_write_cmd(ST7789_RASET);
    lcd_write_data_buf(raset_data, 4);

    // Memory write command
    lcd_write_cmd(ST7789_RAMWR);
}

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    // Apply Y offset (for displays where memory doesn't start at 0)
    y0 += LCD_Y_OFFSET;
    y1 += LCD_Y_OFFSET;

    // Column address set (CASET)
    lcd_write_cmd(ST7789_CASET);
    lcd_write_data(x0 >> 8);
    lcd_write_data(x0 & 0xFF);
    lcd_write_data(x1 >> 8);
    lcd_write_data(x1 & 0xFF);

    // Row address set (RASET)
    lcd_write_cmd(ST7789_RASET);
    lcd_write_data(y0 >> 8);
    lcd_write_data(y0 & 0xFF);
    lcd_write_data(y1 >> 8);
    lcd_write_data(y1 & 0xFF);

    // Memory write command
    lcd_write_cmd(ST7789_RAMWR);
}

//--------------------------------------------------------------------
// DMA Handler
//--------------------------------------------------------------------

static void dma_handler(void) {
    dma_hw->ints0 = 1u << dma_channel;
    sleep_us(50);
    gpio_put(LCD_PIN_CS, 1);
    dma_busy = false;
}

//--------------------------------------------------------------------
// LCD Register Initialization (from rp2350-trackpad)
//--------------------------------------------------------------------

static void lcd_reg_init(void) {
    // Display on
    lcd_write_cmd(0x29);
    sleep_ms(10);

    // Sleep out
    lcd_write_cmd(0x11);
    sleep_ms(10);

    // Memory Data Access Control (MADCTL) - set rotation
    // 0x00 = 0 deg, 0x60 = 90 deg CW, 0xC0 = 180 deg, 0xA0 = 270 deg CW
    lcd_write_cmd(0x36);
#if LCD_ROTATION == 1
    lcd_write_data(0x60);  // 90 deg clockwise (MV | MX)
#elif LCD_ROTATION == 2
    lcd_write_data(0xC0);  // 180 deg (MY | MX)
#elif LCD_ROTATION == 3
    lcd_write_data(0xA0);  // 270 deg clockwise (MV | MY)
#else
    lcd_write_data(0x00);  // 0 deg (default)
#endif

    // Interface Pixel Format - 16bit/pixel
    lcd_write_cmd(0x3A);
    lcd_write_data(0x05);

    // RGB Interface Signal Control
    lcd_write_cmd(0xB0);
    lcd_write_data(0x00);
    lcd_write_data(0xE8);

    // Porch Setting
    lcd_write_cmd(0xB2);
    lcd_write_data(0x0C);
    lcd_write_data(0x0C);
    lcd_write_data(0x00);
    lcd_write_data(0x33);
    lcd_write_data(0x33);

    // Gate Control - VGH=14.97V, VGL=-7.67V
    lcd_write_cmd(0xB7);
    lcd_write_data(0x75);

    // VCOM Setting
    lcd_write_cmd(0xBB);
    lcd_write_data(0x1A);

    // LCM Control
    lcd_write_cmd(0xC0);
    lcd_write_data(0x2C);

    // VDV and VRH Command Enable
    lcd_write_cmd(0xC2);
    lcd_write_data(0x01);
    lcd_write_data(0xFF);

    // VRH Set
    lcd_write_cmd(0xC3);
    lcd_write_data(0x13);

    // VDV Set
    lcd_write_cmd(0xC4);
    lcd_write_data(0x20);

    // Frame Rate Control
    lcd_write_cmd(0xC6);
    lcd_write_data(0x0F);

    // Power Control 1
    lcd_write_cmd(0xD0);
    lcd_write_data(0xA4);
    lcd_write_data(0xA1);

    // ???
    lcd_write_cmd(0xD6);
    lcd_write_data(0xA1);

    // Positive Voltage Gamma Control
    lcd_write_cmd(0xE0);
    lcd_write_data(0xD0);
    lcd_write_data(0x0D);
    lcd_write_data(0x14);
    lcd_write_data(0x0D);
    lcd_write_data(0x0D);
    lcd_write_data(0x09);
    lcd_write_data(0x38);
    lcd_write_data(0x44);
    lcd_write_data(0x4E);
    lcd_write_data(0x3A);
    lcd_write_data(0x17);
    lcd_write_data(0x18);
    lcd_write_data(0x2F);
    lcd_write_data(0x30);

    // Negative Voltage Gamma Control
    lcd_write_cmd(0xE1);
    lcd_write_data(0xD0);
    lcd_write_data(0x09);
    lcd_write_data(0x0F);
    lcd_write_data(0x08);
    lcd_write_data(0x07);
    lcd_write_data(0x14);
    lcd_write_data(0x37);
    lcd_write_data(0x44);
    lcd_write_data(0x4D);
    lcd_write_data(0x38);
    lcd_write_data(0x15);
    lcd_write_data(0x16);
    lcd_write_data(0x2C);
    lcd_write_data(0x2E);

    // Display Inversion On
    lcd_write_cmd(0x21);

    // Display On
    lcd_write_cmd(0x29);

    // Memory Write
    lcd_write_cmd(0x2C);
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void lcd_init(void) {
    // Initialize SPI with Mode 3 (CPOL=1, CPHA=1)
    spi_init(LCD_SPI, LCD_SPI_FREQ);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(LCD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(LCD_PIN_MOSI, GPIO_FUNC_SPI);

    // Initialize control pins
    gpio_init(LCD_PIN_CS);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);

    gpio_init(LCD_PIN_DC);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);

    gpio_init(LCD_PIN_RST);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);

    // Initialize backlight PWM
    float sys_clk = clock_get_hz(clk_sys);
    gpio_set_function(LCD_PIN_BL, GPIO_FUNC_PWM);
    pwm_slice_num = pwm_gpio_to_slice_num(LCD_PIN_BL);
    pwm_channel = pwm_gpio_to_channel(LCD_PIN_BL);
    pwm_set_clkdiv(pwm_slice_num, sys_clk / (PWM_FREQ * PWM_WRAP));
    pwm_set_wrap(pwm_slice_num, PWM_WRAP);
    pwm_set_chan_level(pwm_slice_num, pwm_channel, 0);
    pwm_set_enabled(pwm_slice_num, true);

    // Initialize DMA (low priority to avoid starving audio DMA)
    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(LCD_SPI, true));
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_high_priority(&c, false);  // Low priority for LCD
    dma_channel_configure(dma_channel, &c, &spi_get_hw(LCD_SPI)->dr, NULL, 0,
                          false);

    // Setup DMA interrupt
    dma_channel_set_irq0_enabled(dma_channel, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Hardware reset
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(50);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(50);

    // Initialize LCD registers
    lcd_reg_init();

    // Set default backlight
    lcd_set_backlight(100);
}

void lcd_set_backlight(uint8_t brightness) {
    if (brightness > 100) {
        brightness = 100;
    }
    pwm_set_chan_level(pwm_slice_num, pwm_channel, PWM_WRAP / 100 * brightness);
}

void lcd_update_wait(void) {
    while (dma_busy) {
        tight_loop_contents();
    }
}

bool lcd_is_busy(void) {
    return dma_busy;
}

void lcd_update(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                const uint16_t *data, uint32_t len) {
    // Wait for any previous DMA transfer
    lcd_update_wait();

    // Set the window
    lcd_set_window(x, y, x + width - 1, y + height - 1);

    // Start DMA transfer
    gpio_put(LCD_PIN_CS, 0);
    gpio_put(LCD_PIN_DC, 1);
    dma_busy = true;
    dma_channel_set_trans_count(dma_channel, len, false);
    dma_channel_set_read_addr(dma_channel, data, true);
}

//--------------------------------------------------------------------
// Chunked Update API
//--------------------------------------------------------------------

void lcd_update_chunked_start(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                               const uint16_t *data, uint32_t rows_per_chunk) {
    // Cancel any existing chunked update
    chunked_state.active = true;
    chunked_state.x = x;
    chunked_state.y = y;
    chunked_state.width = width;
    chunked_state.height = height;
    chunked_state.data = data;
    chunked_state.rows_per_chunk = rows_per_chunk;
    chunked_state.current_row = 0;
}

bool lcd_update_chunked_continue(void) {
    if (!chunked_state.active) {
        return false;
    }

    // If DMA is still busy from previous chunk, wait for next call
    if (dma_busy) {
        return true;
    }

    // Calculate how many rows to send in this chunk
    uint32_t remaining_rows = chunked_state.height - chunked_state.current_row;
    if (remaining_rows == 0) {
        chunked_state.active = false;
        return false;
    }

    uint32_t rows_to_send = (remaining_rows < chunked_state.rows_per_chunk)
                            ? remaining_rows : chunked_state.rows_per_chunk;

    // Calculate data offset and length
    uint32_t row_offset = chunked_state.current_row * chunked_state.width;
    uint32_t chunk_len = rows_to_send * chunked_state.width * 2;  // 2 bytes per pixel

    // Set window for this chunk (fast version without per-byte delays)
    uint32_t chunk_y = chunked_state.y + chunked_state.current_row;
    lcd_set_window_fast(chunked_state.x, chunk_y,
                        chunked_state.x + chunked_state.width - 1,
                        chunk_y + rows_to_send - 1);

    // Start DMA transfer for this chunk (CS/DC already set by lcd_set_window_fast)
    gpio_put(LCD_PIN_CS, 0);
    gpio_put(LCD_PIN_DC, 1);
    dma_busy = true;
    dma_channel_set_trans_count(dma_channel, chunk_len, false);
    dma_channel_set_read_addr(dma_channel, &chunked_state.data[row_offset], true);

    // Advance to next chunk
    chunked_state.current_row += rows_to_send;

    return (chunked_state.current_row < chunked_state.height);
}

bool lcd_update_chunked_active(void) {
    return chunked_state.active;
}
