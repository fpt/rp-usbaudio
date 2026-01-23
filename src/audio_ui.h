// SPDX-License-Identifier: MIT
// Audio status UI for Waveshare RP2350-Touch-LCD-2.8 (320x240)

#ifndef _AUDIO_UI_H_
#define _AUDIO_UI_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AUDIO_UI_DISCONNECTED,
    AUDIO_UI_CONNECTED,
    AUDIO_UI_STREAMING,
} audio_ui_state_t;

// Initialize and draw full screen
void audio_ui_init(void);
void audio_ui_draw(uint16_t *fb, int fb_width, int fb_height);

// Update state (sets dirty flags)
void audio_ui_set_state(audio_ui_state_t state);
void audio_ui_set_sample_rate(uint32_t sample_rate);
void audio_ui_set_bit_depth(uint8_t bits);
void audio_ui_set_channels(uint8_t channels);
void audio_ui_set_volume(uint8_t volume);
void audio_ui_set_mute(bool muted);
void audio_ui_set_buf_fill(uint8_t pct);          // 0-100, I2S ring buffer %
void audio_ui_set_asrc_ratio(uint32_t ratio_x1000); // 1000 = 1.000
void audio_ui_set_level(uint8_t left, uint8_t right);

// Redraw dirty regions; returns true if anything changed
bool audio_ui_update(uint16_t *fb, int fb_width, int fb_height);
bool audio_ui_needs_update(void);

#endif /* _AUDIO_UI_H_ */
