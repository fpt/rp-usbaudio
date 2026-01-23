// SPDX-License-Identifier: MIT
// I2S Audio Output for PCM5101 DAC using PIO and chained DMA

#include "i2s_audio.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "i2s_audio.pio.h"
#include "stats.h"

//--------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------

// DMA buffer size (in stereo samples)
// Larger buffer gives more margin during LCD DMA contention
#define DMA_BUFFER_SAMPLES 2048

// Software ring buffer size (in int16_t samples, so stereo pairs * 2)
// 16384 samples = ~170ms at 48kHz stereo, provides good margin
#define RING_BUFFER_SIZE 16384

// Number of DMA buffers for ping-pong
#define NUM_DMA_BUFFERS 2

//--------------------------------------------------------------------
// State
//--------------------------------------------------------------------

static PIO i2s_pio;
static uint i2s_sm;
static uint i2s_offset;

// Two DMA channels for chained operation
static int dma_chan[2];

// DMA buffers - each contains 2 words per stereo sample for 64 BCK format
static uint32_t dma_buffer[NUM_DMA_BUFFERS][DMA_BUFFER_SAMPLES * 2];

// Software audio ring buffer
static int16_t ring_buffer[RING_BUFFER_SIZE];
static volatile uint32_t ring_read_pos = 0;
static volatile uint32_t ring_write_pos = 0;
static volatile uint32_t ring_count = 0;

// Volume and mute
static uint8_t volume_level = 100;
static bool is_muted = false;

// Last sample (to avoid pops when buffer underruns)
static int16_t last_left = 0;
static int16_t last_right = 0;

// Sample rate
static uint32_t current_sample_rate = 48000;

// Playback state - wait for buffer to fill before starting DMA
static bool dma_started = false;
// Start threshold must exceed 2× DMA buffer size (8192 int16_t) so the ring
// buffer isn't empty after pre-filling both ping-pong buffers at startup.
#define BUFFER_START_THRESHOLD (RING_BUFFER_SIZE * 3 / 4)  // 12288: leaves ~4096 after pre-fill

// Debug stats
static volatile uint8_t stat_fifo_level_min = 8;
static volatile uint32_t stat_irq_last_time = 0;
static volatile uint32_t stat_irq_interval_us = 0;
static volatile uint32_t stat_irq_interval_min_us = UINT32_MAX;
static volatile uint32_t stat_irq_interval_max_us = 0;
static volatile uint32_t stat_empty_buffer_count = 0;

// Track if buffers were written to since last DMA completion
static volatile bool buffer_written[NUM_DMA_BUFFERS] = {false, false};

//--------------------------------------------------------------------
// Volume Scaling
//--------------------------------------------------------------------

static inline int16_t apply_volume(int16_t sample) {
    if (is_muted) {
        return 0;
    }
    return (int16_t)(((int32_t)sample * volume_level) / 100);
}

//--------------------------------------------------------------------
// Buffer Fill
//--------------------------------------------------------------------

static void fill_dma_buffer(uint32_t *buf) {
    bool had_underrun = false;

    for (uint32_t i = 0; i < DMA_BUFFER_SAMPLES; i++) {
        int16_t left, right;

        if (ring_count >= 2) {
            left = apply_volume(ring_buffer[ring_read_pos]);
            right = apply_volume(ring_buffer[(ring_read_pos + 1) % RING_BUFFER_SIZE]);
            ring_read_pos = (ring_read_pos + 2) % RING_BUFFER_SIZE;
            ring_count -= 2;
            last_left = left;
            last_right = right;
        } else {
            // Buffer underrun - fade to silence to avoid pop
            last_left = last_left * 15 / 16;
            last_right = last_right * 15 / 16;
            left = last_left;
            right = last_right;
            had_underrun = true;
        }

        // Pack each channel into separate 32-bit word (64 BCK format)
        buf[i * 2]     = (uint32_t)(uint16_t)left << 16;
        buf[i * 2 + 1] = (uint32_t)(uint16_t)right << 16;
    }

    if (had_underrun) {
        stats_record_underrun();
    }
}

//--------------------------------------------------------------------
// DMA Interrupt Handler
//--------------------------------------------------------------------

static void dma_irq_handler(void) {
    // Track IRQ timing
    uint32_t now = time_us_32();
    if (stat_irq_last_time != 0) {
        stat_irq_interval_us = now - stat_irq_last_time;
        if (stat_irq_interval_us < stat_irq_interval_min_us) {
            stat_irq_interval_min_us = stat_irq_interval_us;
        }
        if (stat_irq_interval_us > stat_irq_interval_max_us) {
            stat_irq_interval_max_us = stat_irq_interval_us;
        }
    }
    stat_irq_last_time = now;

    // Track FIFO level
    uint8_t fifo_level = pio_sm_get_tx_fifo_level(i2s_pio, i2s_sm);
    if (fifo_level < stat_fifo_level_min) {
        stat_fifo_level_min = fifo_level;
    }

    // Check which channel finished and refill its buffer
    for (int i = 0; i < 2; i++) {
        if (dma_hw->ints1 & (1u << dma_chan[i])) {
            dma_hw->ints1 = 1u << dma_chan[i];  // Clear interrupt

            // Track if buffer had new data written
            if (!buffer_written[i]) {
                stat_empty_buffer_count++;
            }
            buffer_written[i] = false;

            fill_dma_buffer(dma_buffer[i]);      // Refill this buffer
            // Reset read address for next time this channel runs
            dma_channel_set_read_addr(dma_chan[i], dma_buffer[i], false);
        }
    }
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void i2s_audio_init(uint32_t sample_rate) {
    current_sample_rate = sample_rate;

    // Choose PIO and state machine
    i2s_pio = pio0;
    i2s_sm = pio_claim_unused_sm(i2s_pio, true);

    // Load PIO program
    i2s_offset = pio_add_program(i2s_pio, &i2s_audio_program);

    // Initialize PIO
    i2s_audio_program_init(i2s_pio, i2s_sm, i2s_offset, I2S_PIN_DIN,
                           I2S_PIN_BCLK, sample_rate);

    // Claim two DMA channels
    dma_chan[0] = dma_claim_unused_channel(true);
    dma_chan[1] = dma_claim_unused_channel(true);

    // Configure DMA channel 0 (high priority for audio)
    dma_channel_config c0 = dma_channel_get_default_config(dma_chan[0]);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(i2s_pio, i2s_sm, true));
    channel_config_set_chain_to(&c0, dma_chan[1]);  // Chain to channel 1
    channel_config_set_high_priority(&c0, true);    // High priority for audio

    dma_channel_configure(dma_chan[0], &c0,
                          &i2s_pio->txf[i2s_sm],
                          dma_buffer[0],
                          DMA_BUFFER_SAMPLES * 2,
                          false);

    // Configure DMA channel 1 (high priority for audio)
    dma_channel_config c1 = dma_channel_get_default_config(dma_chan[1]);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_dreq(&c1, pio_get_dreq(i2s_pio, i2s_sm, true));
    channel_config_set_chain_to(&c1, dma_chan[0]);  // Chain to channel 0
    channel_config_set_high_priority(&c1, true);    // High priority for audio

    dma_channel_configure(dma_chan[1], &c1,
                          &i2s_pio->txf[i2s_sm],
                          dma_buffer[1],
                          DMA_BUFFER_SAMPLES * 2,
                          false);

    // Setup DMA interrupts (use IRQ 1 since LCD uses IRQ 0)
    dma_channel_set_irq1_enabled(dma_chan[0], true);
    dma_channel_set_irq1_enabled(dma_chan[1], true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    // Initialize buffers with silence
    memset(dma_buffer, 0, sizeof(dma_buffer));
    memset(ring_buffer, 0, sizeof(ring_buffer));

    // Don't start DMA yet - wait for buffer to fill
    dma_started = false;
}

void i2s_audio_set_sample_rate(uint32_t sample_rate) {
    current_sample_rate = sample_rate;
    i2s_audio_program_set_sample_rate(i2s_pio, i2s_sm, sample_rate);
}

uint32_t i2s_audio_write(const int16_t *samples, uint32_t sample_count) {
    uint32_t written = 0;

    uint32_t irq_state = save_and_disable_interrupts();

    while (written < sample_count && ring_count < RING_BUFFER_SIZE - 2) {
        ring_buffer[ring_write_pos] = samples[written];
        ring_write_pos = (ring_write_pos + 1) % RING_BUFFER_SIZE;
        ring_count++;
        written++;
    }

    // Mark that data was written (for stats tracking)
    if (written > 0) {
        buffer_written[0] = true;
        buffer_written[1] = true;
    }

    // Start DMA when buffer reaches threshold
    if (!dma_started && ring_count >= BUFFER_START_THRESHOLD) {
        dma_started = true;
        // Pre-fill DMA buffers before starting
        fill_dma_buffer(dma_buffer[0]);
        fill_dma_buffer(dma_buffer[1]);
        dma_channel_start(dma_chan[0]);
    }

    restore_interrupts(irq_state);

    return written;
}

bool i2s_audio_can_write(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    bool can_write = ring_count < RING_BUFFER_SIZE - 2;
    restore_interrupts(irq_state);
    return can_write;
}

uint32_t i2s_audio_get_free_count(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t free_count = RING_BUFFER_SIZE - ring_count;
    restore_interrupts(irq_state);
    return free_count;
}

void i2s_audio_set_volume(uint8_t volume) {
    if (volume > 100) {
        volume = 100;
    }
    volume_level = volume;
}

void i2s_audio_set_mute(bool mute) {
    is_muted = mute;
}

uint32_t i2s_audio_get_buffer_count(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t count = ring_count;
    restore_interrupts(irq_state);
    return count;
}

void i2s_audio_clear_buffer(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    ring_read_pos = 0;
    ring_write_pos = 0;
    ring_count = 0;
    dma_started = false;
    restore_interrupts(irq_state);
}

void i2s_audio_update(void) {
    // No-op for DMA-based I2S
}

void i2s_audio_get_stats(i2s_audio_stats_t *stats) {
    uint32_t irq_state = save_and_disable_interrupts();

    stats->fifo_level = pio_sm_get_tx_fifo_level(i2s_pio, i2s_sm);
    stats->fifo_level_min = stat_fifo_level_min;
    stats->irq_interval_us = stat_irq_interval_us;
    stats->irq_interval_min_us = (stat_irq_interval_min_us == UINT32_MAX) ? 0 : stat_irq_interval_min_us;
    stats->irq_interval_max_us = stat_irq_interval_max_us;
    stats->empty_buffer_count = stat_empty_buffer_count;

    // Reset min/max tracking
    stat_fifo_level_min = 8;
    stat_irq_interval_min_us = UINT32_MAX;
    stat_irq_interval_max_us = 0;
    stat_empty_buffer_count = 0;

    restore_interrupts(irq_state);
}
