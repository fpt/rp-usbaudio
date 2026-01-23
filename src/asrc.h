// SPDX-License-Identifier: MIT
// Asynchronous Sample Rate Converter (ASRC)
//
// Simple resampler that adjusts playback rate based on buffer level.
// Uses linear interpolation to stretch/compress audio to match
// the actual I2S clock rate.

#ifndef _ASRC_H_
#define _ASRC_H_

#include <stdbool.h>
#include <stdint.h>

// Initialize ASRC
// target_fill_pct: target buffer fill percentage (e.g., 50)
void asrc_init(uint8_t target_fill_pct);

// Process audio samples through ASRC
// Input: interleaved stereo samples from USB
// Output: interleaved stereo samples for I2S (may be more or fewer)
// Returns: number of output samples written
//
// The ratio is automatically adjusted based on downstream buffer level.
uint32_t asrc_process(const int16_t *in, uint32_t in_samples,
                      int16_t *out, uint32_t out_max_samples);

// Update ASRC with current buffer status
// Call this periodically (e.g., every audio_task) to adjust ratio
// buf_count: current samples in downstream buffer
// buf_total: total buffer capacity
void asrc_update_buffer_level(uint32_t buf_count, uint32_t buf_total);

// Get current resampling ratio (for debugging)
// Returns ratio * 1000 (e.g., 1000 = 1.0, 1005 = 1.005)
uint32_t asrc_get_ratio_x1000(void);

// Reset ASRC state (call on stream start/stop)
void asrc_reset(void);

#endif /* _ASRC_H_ */
