// SPDX-License-Identifier: MIT
// RP2350 USB Audio DAC - Main entry point
//
// USB Audio Class 1.0 (UAC1) speaker.
// Audio output: I2S to PCM5101 DAC (default) or PDM (-DAUDIO_PDM).
// Uses pico-extras USB device stack (interrupt-driven) + ASRC feedback.
// Board: Waveshare RP2350-Touch-LCD-2.8

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "asrc.h"
#include "audio_out.h"
#include "audio_ui.h"
#include "lcd.h"
#include "splash.h"
#include "stats.h"
#include "usb_audio.h"

//--------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------

#define USE_WATCHDOG        1
#define WATCHDOG_TIMEOUT_MS 8000
#define DEFAULT_BRIGHTNESS  90
#define UI_UPDATE_MS        80   // ~12 fps for smooth VU meters

//--------------------------------------------------------------------
// Framebuffer (320x240 RGB565)
//--------------------------------------------------------------------

static uint16_t framebuffer[LCD_WIDTH * LCD_HEIGHT];

//--------------------------------------------------------------------
// Main
//--------------------------------------------------------------------

int main(void) {
#ifdef AUDIO_PDM
    // 115.2 MHz → exact 48 kHz PDM bit-rate (clkdiv = 115200000 / (48000 × 32) = 75.0)
    set_sys_clock_khz(115200, false);
#endif

    stdio_init_all();

#ifdef AUDIO_PDM
    printf("\n\nRP2350 USB Audio DAC (PDM + UAC1)\n\n");
#else
    printf("\n\nRP2350 USB Audio DAC (I2S + UAC1)\n\n");
#endif

#if USE_WATCHDOG
    if (watchdog_caused_reboot())
        printf("WARNING: Rebooted by watchdog!\n");
#endif

    lcd_init();
    lcd_set_backlight(DEFAULT_BRIGHTNESS);

    splash_draw(framebuffer, LCD_WIDTH, LCD_HEIGHT);
    lcd_update(0, 0, LCD_WIDTH, LCD_HEIGHT, framebuffer,
               LCD_WIDTH * LCD_HEIGHT * 2);
    lcd_update_wait();

    audio_out_init(48000);
    audio_out_set_volume(100);

    stats_init();
    usb_audio_init();

#if USE_WATCHDOG
    watchdog_enable(WATCHDOG_TIMEOUT_MS, false);
#endif

    audio_ui_init();
    audio_ui_draw(framebuffer, LCD_WIDTH, LCD_HEIGHT);
    lcd_update(0, 0, LCD_WIDTH, LCD_HEIGHT, framebuffer,
               LCD_WIDTH * LCD_HEIGHT * 2);
    lcd_update_wait();

    uint32_t last_ui_update = 0;

    while (1) {
        audio_out_update();  // no-op for I2S; refills DMA buffers for PDM
        stats_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_ui_update >= UI_UPDATE_MS) {
            last_ui_update = now;

            // Gather state
            bool streaming = usb_audio_is_streaming();

            audio_ui_set_state(streaming ? AUDIO_UI_STREAMING : AUDIO_UI_CONNECTED);
            audio_ui_set_sample_rate(usb_audio_get_sample_rate());
            audio_ui_set_bit_depth(16);
            audio_ui_set_channels(2);
            audio_ui_set_volume(usb_audio_get_volume());
            audio_ui_set_mute(usb_audio_get_mute());

            // Buffer fill %
            uint32_t buf = audio_out_get_buffer_count();
            audio_ui_set_buf_fill((uint8_t)(buf * 100 / AUDIO_OUT_RING_BUFFER_SIZE));

            // ASRC ratio and VU levels (only meaningful while streaming)
            audio_ui_set_asrc_ratio(asrc_get_ratio_x1000());
            uint8_t left, right;
            usb_audio_get_levels(&left, &right);
            audio_ui_set_level(left, right);

            // Push to LCD: wait for previous DMA transfer, then redraw dirty regions
            if (audio_ui_needs_update()) {
                lcd_update_wait();
                audio_ui_update(framebuffer, LCD_WIDTH, LCD_HEIGHT);
                lcd_update(0, 0, LCD_WIDTH, LCD_HEIGHT, framebuffer,
                           LCD_WIDTH * LCD_HEIGHT * 2);
            }
        }

#if USE_WATCHDOG
        watchdog_update();
#endif
    }

    return 0;
}
