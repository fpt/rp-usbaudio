// SPDX-License-Identifier: MIT
// Asynchronous Sample Rate Converter (ASRC)

#include "asrc.h"

//--------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------

// Maximum ratio adjustment from 1.0 (±2%)
#define MAX_RATIO_ADJUST_X10000 200  // 200/10000 = 2.0%

// Proportional gain: adjustment per 1% buffer error (in x10000 units)
#define RATIO_GAIN_X10000 4  // 4/10000 = 0.04% per 1% error

// Smoothing factor (higher shift = slower, smoother response)
#define RATIO_SMOOTH_SHIFT 4  // Divide by 16 for smoother changes

// Rate limit: only update ratio every N calls to reduce jitter
#define RATIO_UPDATE_INTERVAL 8
static uint32_t update_counter = 0;

//--------------------------------------------------------------------
// State
//--------------------------------------------------------------------

// Current resampling ratio * 10000 for precision (10000 = 1.0)
static int32_t current_ratio_x10000 = 10000;

// Target buffer fill percentage
static uint8_t target_fill_pct = 50;

// Fractional accumulator for interpolation (16.16 fixed point)
static uint32_t frac_pos = 0;

// Previous stereo sample for interpolation
static int16_t prev_left = 0;
static int16_t prev_right = 0;

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void asrc_init(uint8_t target_pct) {
    target_fill_pct = target_pct;
    current_ratio_x10000 = 10000;
    frac_pos = 0;
    prev_left = 0;
    prev_right = 0;
    update_counter = 0;
}

void asrc_reset(void) {
    current_ratio_x10000 = 10000;
    frac_pos = 0;
    prev_left = 0;
    prev_right = 0;
    update_counter = 0;
}

void asrc_update_buffer_level(uint32_t buf_count, uint32_t buf_total) {
    if (buf_total == 0) return;

    // Rate limit updates to reduce jitter
    if (++update_counter < RATIO_UPDATE_INTERVAL) {
        return;
    }
    update_counter = 0;

    // Calculate current fill percentage (0-100)
    int32_t fill_pct = (buf_count * 100) / buf_total;

    // Calculate error: positive = buffer too low, negative = buffer too high
    int32_t error = (int32_t)target_fill_pct - fill_pct;

    // Proportional adjustment in x10000 units
    int32_t adjustment = error * RATIO_GAIN_X10000;

    // Clamp adjustment to ±2%
    if (adjustment > MAX_RATIO_ADJUST_X10000) adjustment = MAX_RATIO_ADJUST_X10000;
    if (adjustment < -MAX_RATIO_ADJUST_X10000) adjustment = -MAX_RATIO_ADJUST_X10000;

    int32_t target_ratio = 10000 + adjustment;

    // Exponential smoothing using bit shift
    // With SHIFT=4, we move 1/16 (6.25%) toward target each update
    int32_t delta = target_ratio - current_ratio_x10000;
    current_ratio_x10000 += delta >> RATIO_SMOOTH_SHIFT;
}

uint32_t asrc_process(const int16_t *in, uint32_t in_samples,
                      int16_t *out, uint32_t out_max_samples) {
    if (in_samples == 0 || out_max_samples == 0) return 0;

    // Input is stereo interleaved, so in_samples should be even
    uint32_t in_frames = in_samples / 2;
    uint32_t out_frames = 0;
    uint32_t max_out_frames = out_max_samples / 2;

    // Step size in 16.16 fixed point
    // ratio > 1.0 means more output, so step < 1.0
    // step = 1.0 / ratio = 10000 / ratio_x10000 (in 16.16 fixed point)
    // step = (10000 << 16) / ratio_x10000
    uint32_t step = (10000UL << 16) / (uint32_t)current_ratio_x10000;

    uint32_t in_idx = 0;

    while (out_frames < max_out_frames) {
        // Get integer part of position
        uint32_t pos_int = frac_pos >> 16;

        // Check if we've consumed all input
        if (pos_int >= in_frames) {
            // Adjust frac_pos for next call
            frac_pos -= (in_frames << 16);
            break;
        }

        // Get fractional part for interpolation (use 8 bits to avoid overflow)
        // Full 16-bit frac * 16-bit sample diff can overflow int32
        uint32_t frac = (frac_pos >> 8) & 0xFF;  // 0-255

        // Get current and next samples for interpolation
        int16_t curr_left, curr_right, next_left, next_right;

        if (pos_int == 0) {
            // Interpolate between previous buffer's last sample and current buffer's first
            curr_left = prev_left;
            curr_right = prev_right;
            next_left = in[0];
            next_right = in[1];
        } else {
            // Interpolate between in[pos_int-1] and in[pos_int]
            curr_left = in[(pos_int - 1) * 2];
            curr_right = in[(pos_int - 1) * 2 + 1];
            next_left = in[pos_int * 2];
            next_right = in[pos_int * 2 + 1];
        }

        // Linear interpolation (8-bit frac, no overflow possible)
        int32_t out_left = curr_left + (((next_left - curr_left) * (int32_t)frac) >> 8);
        int32_t out_right = curr_right + (((next_right - curr_right) * (int32_t)frac) >> 8);

        out[out_frames * 2] = (int16_t)out_left;
        out[out_frames * 2 + 1] = (int16_t)out_right;
        out_frames++;

        // Advance position
        frac_pos += step;
    }

    // Save last input sample for next call's interpolation
    if (in_frames > 0) {
        prev_left = in[(in_frames - 1) * 2];
        prev_right = in[(in_frames - 1) * 2 + 1];
    }

    return out_frames * 2;  // Return sample count (stereo)
}

uint32_t asrc_get_ratio_x1000(void) {
    return (uint32_t)(current_ratio_x10000 / 10);
}
