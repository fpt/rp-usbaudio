// SPDX-License-Identifier: MIT
// I2S Audio Output for PCM5101 DAC

#ifndef _I2S_AUDIO_H_
#define _I2S_AUDIO_H_

#include <stdbool.h>
#include <stdint.h>

//--------------------------------------------------------------------
// I2S Pin Configuration (Waveshare RP2350-Touch-LCD-2.8)
//--------------------------------------------------------------------

#define I2S_PIN_BCLK 2   // Bit Clock
#define I2S_PIN_LRCK 3   // Left/Right Clock (Word Select)
#define I2S_PIN_DIN  4   // Data In (to DAC)

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

// Ring buffer size (in int16_t units = stereo sample pairs * 2).
// Exposed so USB feedback can compare fill level to midpoint.
#define I2S_RING_BUFFER_SIZE 16384

// Initialize I2S audio output
// sample_rate: audio sample rate in Hz (e.g., 44100, 48000)
void i2s_audio_init(uint32_t sample_rate);

// Set sample rate (can be called at runtime)
void i2s_audio_set_sample_rate(uint32_t sample_rate);

// Write stereo audio samples to I2S output
// Samples are 16-bit signed, interleaved L/R
// Returns number of samples consumed
uint32_t i2s_audio_write(const int16_t *samples, uint32_t sample_count);

// Check if I2S audio buffer has space for more samples
bool i2s_audio_can_write(void);

// Get number of samples that can be written
uint32_t i2s_audio_get_free_count(void);

// Set volume (0-100)
void i2s_audio_set_volume(uint8_t volume);

// Mute/unmute
void i2s_audio_set_mute(bool mute);

// Get current buffer count (for debugging)
uint32_t i2s_audio_get_buffer_count(void);

// Clear the ring buffer (call when a new USB stream starts).
// Resets buffer pointers and stops DMA until new data arrives.
void i2s_audio_clear_buffer(void);

// Update I2S output (call frequently from main loop)
// This is a no-op for DMA-based I2S, but kept for API compatibility
void i2s_audio_update(void);

//--------------------------------------------------------------------
// Debug Statistics
//--------------------------------------------------------------------

typedef struct {
    uint8_t fifo_level;           // Current PIO TX FIFO level (0-8)
    uint8_t fifo_level_min;       // Min FIFO level since last read
    uint32_t irq_interval_us;     // Last DMA IRQ interval
    uint32_t irq_interval_min_us; // Min IRQ interval since last read
    uint32_t irq_interval_max_us; // Max IRQ interval since last read
    uint32_t empty_buffer_count;  // DMA buffers with no new data written
} i2s_audio_stats_t;

// Get and reset debug stats
void i2s_audio_get_stats(i2s_audio_stats_t *stats);

#endif /* _I2S_AUDIO_H_ */
