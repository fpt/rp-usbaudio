// SPDX-License-Identifier: MIT
// USB Audio Class 1.0 (UAC1) interface
// Uses pico-extras USB device stack with proportional feedback.

#ifndef _USB_AUDIO_H_
#define _USB_AUDIO_H_

#include <stdbool.h>
#include <stdint.h>

// Initialize USB audio device (call after i2s_audio_init)
void usb_audio_init(void);

// State accessors for UI
bool     usb_audio_is_streaming(void);
uint32_t usb_audio_get_sample_rate(void);
uint8_t  usb_audio_get_volume(void);
bool     usb_audio_get_mute(void);
void     usb_audio_get_levels(uint8_t *left, uint8_t *right);

#endif /* _USB_AUDIO_H_ */
