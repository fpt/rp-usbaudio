// SPDX-License-Identifier: MIT
// PDM (Delta-Sigma Modulation) audio output - single GPIO, PIO + DMA driven

#ifndef _PDM_AUDIO_H_
#define _PDM_AUDIO_H_

#include <stdbool.h>
#include <stdint.h>

//--------------------------------------------------------------------
// Pin Configuration
//--------------------------------------------------------------------

// Single GPIO for PDM output (RC low-pass filter + DC-blocking cap required)
#define PDM_PIN_OUT  18

// Oversampling ratio: PDM bits output per PCM sample.
// Must be a multiple of 32 (one PIO word = 32 PDM bits).
//   32× → 1.536 MHz bit-rate @ 48 kHz, clkdiv=75.0 @ 115.2 MHz sys
//   64× → 3.072 MHz bit-rate @ 48 kHz, clkdiv=37.5 @ 115.2 MHz sys
#define PDM_OVERSAMPLE 32

// Ring buffer size in int16_t units (stereo, so half this many frames).
// Exposed so the USB feedback calculation can compare fill level to midpoint.
#define PDM_RING_BUFFER_SIZE 16384

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

// Initialize PDM audio output
// sample_rate: audio sample rate in Hz (e.g., 44100, 48000)
// Note: Call set_sys_clock_khz(115200, false) before this for exact
//       48 kHz timing (clkdiv = 75.0 exactly).
void pdm_audio_init(uint32_t sample_rate);

// Set sample rate (can be called at runtime)
void pdm_audio_set_sample_rate(uint32_t sample_rate);

// Write stereo audio samples to PDM output
// Samples are 16-bit signed, interleaved L/R; mixed to mono internally.
// Returns number of samples consumed.
uint32_t pdm_audio_write(const int16_t *samples, uint32_t sample_count);

// Check if PDM audio buffer has space for more samples
bool pdm_audio_can_write(void);

// Get number of samples that can be written
uint32_t pdm_audio_get_free_count(void);

// Lockless approximation — safe to call from any interrupt context.
// Uses a single volatile 32-bit read (atomic on Cortex-M); value may be
// slightly stale but is fine for the proportional feedback controller.
uint32_t pdm_audio_get_free_count_approx(void);

// Set volume (0-100)
void pdm_audio_set_volume(uint8_t volume);

// Mute/unmute
void pdm_audio_set_mute(bool mute);

// Get current ring buffer sample count (for debugging)
uint32_t pdm_audio_get_buffer_count(void);

// Clear the ring buffer (call when a new USB stream starts).
// The DMA is not stopped; it will produce a brief silence underrun until
// new USB samples refill the buffer.
void pdm_audio_clear_buffer(void);

// Process pending DMA buffer refills (call every main-loop iteration).
// The DMA IRQ sets a pending flag; this function runs the SDM to refill
// the completed buffer(s) and resets the DMA read address. Keeping the
// heavy SDM work out of the IRQ handler prevents USB SOF interrupt
// starvation (~5 ms per fill at 115 MHz would block USB for 5 ms).
void pdm_audio_update(void);

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
} pdm_audio_stats_t;

// Get and reset debug stats
void pdm_audio_get_stats(pdm_audio_stats_t *stats);

#endif /* _PDM_AUDIO_H_ */
