// SPDX-License-Identifier: MIT
// PDM (Delta-Sigma Modulation) audio output for single GPIO
//
// Architecture:
//   - Software ring buffer holds stereo PCM samples from USB
//   - Chained DMA feeds a buffer of 32-bit PDM words to the PIO TX FIFO
//   - DMA IRQ sets a pending flag; the main loop calls pdm_audio_update()
//     which refills the completed buffer (SDM runs in main loop, not IRQ)
//   - PIO shifts each bit out at (sample_rate * PDM_OVERSAMPLE) Hz
//
// Key design point: fill_dma_buffer() runs the 4th-order SDM for 4096
// frames × PDM_WORDS_PER_FRAME SDM calls. Running it in the DMA IRQ
// would block USB SOF interrupts; moving it to the main loop keeps
// USB IRQ latency low.
//
// Ported sigma-delta modulator from tierneytim/Pico-USB-audio (SDM.cpp),
// 4th-order Direct Form 2.

#include "pdm_audio.h"

#include <string.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#include "pdm_audio.pio.h"
#include "stats.h"

//--------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------

// DMA buffer size in PCM frames (stereo sample pairs).
// 4096 frames at 48 kHz → ~85 ms per DMA buffer, regardless of PDM_OVERSAMPLE.
// Each frame produces PDM_WORDS_PER_FRAME uint32_t PDM words, so the actual
// DMA transfer count is DMA_BUFFER_FRAMES * PDM_WORDS_PER_FRAME.
// This must exceed the longest main-loop blocking call (lcd_update at
// 10 MHz SPI ≈ 20 ms) so the DMA never completes while the main loop
// is blocked and cannot call pdm_audio_update().
#define DMA_BUFFER_FRAMES    4096
#define PDM_WORDS_PER_FRAME  (PDM_OVERSAMPLE / 32)
#define DMA_BUFFER_WORDS     (DMA_BUFFER_FRAMES * PDM_WORDS_PER_FRAME)

// Software ring buffer (stereo int16_t pairs) - size defined in pdm_audio.h
#define RING_BUFFER_SIZE PDM_RING_BUFFER_SIZE

#define NUM_DMA_BUFFERS 2

//--------------------------------------------------------------------
// 4th-order sigma-delta modulator (Direct Form 2)
// Ported from SDM::o4_os32_df2() in tierneytim/Pico-USB-audio
//--------------------------------------------------------------------

typedef struct {
    int32_t w[4];
    int32_t vmin;       // -32767 * 3  → output ~1.1 V pk-pk
    int32_t pos_error;  //  32767 * 3 * 2
} sdm_state_t;

static void sdm_reset(sdm_state_t *s) {
    s->w[0] = s->w[1] = s->w[2] = s->w[3] = 0;
    s->vmin      = -32767 * 3;
    s->pos_error =  32767 * 3 * 2;
}

// Produce one 32-bit PDM word from one 16-bit PCM sample.
// Bit 0 is the first output bit (LSB-first PIO shift).
static uint32_t sdm_process(sdm_state_t *s, int16_t sig) {
    uint32_t out = 0;
    int32_t d    = s->vmin - sig;
    int64_t test = (int64_t)s->vmin * 1024;

    for (int j = 0; j < 32; j++) {
        int32_t wn   = d + 4*(s->w[0] + s->w[2]) - 6*s->w[1] - s->w[3];
        // etmp must be int64_t: at high signal levels w[0] reaches ~750k,
        // and -3035 * 750000 = -2.28e9 which overflows int32_t (max ±2.15e9),
        // producing incorrect quantiser decisions and audible distortion.
        int64_t etmp = (int64_t)(-3035)*s->w[0] + (int64_t)3477*s->w[1]
                     - (int64_t)1809 *s->w[2] + (int64_t) 359*s->w[3]
                     + (int64_t)1024 *wn;
        s->w[3] = s->w[2];
        s->w[2] = s->w[1];
        s->w[1] = s->w[0];
        s->w[0] = wn;

        if (etmp < test) {
            s->w[0] += s->pos_error;
            out += (1u << j);
        }
    }
    return out;
}

//--------------------------------------------------------------------
// State
//--------------------------------------------------------------------

static PIO    pdm_pio;
static uint   pdm_sm;
static uint   pdm_offset;

static int      dma_chan[NUM_DMA_BUFFERS];
static uint32_t dma_buffer[NUM_DMA_BUFFERS][DMA_BUFFER_WORDS];

// Software ring buffer (stereo int16_t, interleaved L/R)
static int16_t          ring_buffer[RING_BUFFER_SIZE];
static volatile uint32_t ring_read_pos  = 0;
static volatile uint32_t ring_write_pos = 0;
static volatile uint32_t ring_count     = 0;

static uint8_t volume_level = 100;
static bool    is_muted     = false;

// Last sample (for underrun fade-to-silence)
static int16_t last_left  = 0;
static int16_t last_right = 0;

static uint32_t current_sample_rate = 48000;

// Delay DMA start until ring buffer has a modest cushion.
// With 4096-sample DMA buffers the main loop handles all fills;
// there is no pre-fill at start so a quarter-full ring is sufficient.
static bool dma_started = false;
#define BUFFER_START_THRESHOLD (RING_BUFFER_SIZE / 4)

// Sigma-delta modulator state (lives across buffer fills)
static sdm_state_t sdm;

// TPDF dither: breaks SDM idle-tone patterns during silence.
// Uses a Galois LFSR (period 2^32-1) for low-cost pseudo-random generation.
// tpdf_dither() returns a triangular-distributed value in {-1, 0, 0, +1}
// (sum of two independent 1-bit uniform draws), equivalent to ±1 LSB TPDF.
static uint32_t dither_lfsr = 0xACE1CAFEu;

static inline int16_t tpdf_dither(void) {
    dither_lfsr = (dither_lfsr >> 1) ^ (-(dither_lfsr & 1u) & 0xB4BCD35Cu);
    return (int16_t)((int32_t)(dither_lfsr & 1u) - (int32_t)((dither_lfsr >> 1) & 1u));
}

// Per-buffer tracking for stats
static volatile bool buffer_written[NUM_DMA_BUFFERS] = {false, false};

// Pending DMA buffer refill flags (set by IRQ, cleared by pdm_audio_update)
static volatile uint8_t dma_pending_mask = 0;

// Debug stats
static volatile uint8_t  stat_fifo_level_min         = 8;
static volatile uint32_t stat_irq_last_time          = 0;
static volatile uint32_t stat_irq_interval_us        = 0;
static volatile uint32_t stat_irq_interval_min_us    = UINT32_MAX;
static volatile uint32_t stat_irq_interval_max_us    = 0;
static volatile uint32_t stat_empty_buffer_count     = 0;

//--------------------------------------------------------------------
// Volume
//--------------------------------------------------------------------

static inline int16_t apply_volume(int16_t sample) {
    if (is_muted) return 0;
    return (int16_t)(((int32_t)sample * volume_level) / 100);
}

//--------------------------------------------------------------------
// Buffer fill  (called from main loop via pdm_audio_update)
//--------------------------------------------------------------------

static void fill_dma_buffer(uint32_t *buf) {
    bool had_underrun = false;

    for (uint32_t i = 0; i < DMA_BUFFER_FRAMES; i++) {
        int16_t left, right;
        bool underrun;

        // Atomically read one stereo frame and update ring pointers.
        // This brief critical section prevents the USB IRQ (pdm_audio_write /
        // pdm_audio_clear_buffer) from corrupting ring_count between the
        // ring_count >= 2 check and the ring_count -= 2 update. Without this,
        // a USB IRQ that resets ring_count = 0 mid-check would cause ring_count
        // to underflow to 0xFFFFFFFE, replaying stale ring buffer data as audio.
        // The SDM computation (the expensive part) runs after restore_interrupts.
        uint32_t irq_state = save_and_disable_interrupts();
        if (ring_count >= 2) {
            left  = apply_volume(ring_buffer[ring_read_pos]);
            right = apply_volume(ring_buffer[(ring_read_pos + 1) % RING_BUFFER_SIZE]);
            ring_read_pos = (ring_read_pos + 2) % RING_BUFFER_SIZE;
            ring_count   -= 2;
            last_left  = left;
            last_right = right;
            underrun = false;
        } else {
            underrun = true;
        }
        restore_interrupts(irq_state);

        if (underrun) {
            // Buffer underrun: fade to silence to avoid a pop
            last_left  = last_left  * 15 / 16;
            last_right = last_right * 15 / 16;
            left  = last_left;
            right = last_right;
            had_underrun = true;
        }

        // Mix stereo to mono, add TPDF dither to break SDM idle tones,
        // then run SDM PDM_WORDS_PER_FRAME times to produce PDM_OVERSAMPLE bits.
        int16_t mono = (int16_t)(((int32_t)left + right) / 2 + tpdf_dither());
        for (uint32_t w = 0; w < PDM_WORDS_PER_FRAME; w++) {
            buf[i * PDM_WORDS_PER_FRAME + w] = sdm_process(&sdm, mono);
        }
    }

    if (had_underrun) {
        stats_record_underrun();
    }
}

//--------------------------------------------------------------------
// DMA IRQ handler
//--------------------------------------------------------------------

static void dma_irq_handler(void) {
    // Track IRQ timing for stats
    uint32_t now = time_us_32();
    if (stat_irq_last_time != 0) {
        stat_irq_interval_us = now - stat_irq_last_time;
        if (stat_irq_interval_us < stat_irq_interval_min_us)
            stat_irq_interval_min_us = stat_irq_interval_us;
        if (stat_irq_interval_us > stat_irq_interval_max_us)
            stat_irq_interval_max_us = stat_irq_interval_us;
    }
    stat_irq_last_time = now;

    // Track FIFO level
    uint8_t fifo_level = pio_sm_get_tx_fifo_level(pdm_pio, pdm_sm);
    if (fifo_level < stat_fifo_level_min)
        stat_fifo_level_min = fifo_level;

    for (int i = 0; i < NUM_DMA_BUFFERS; i++) {
        if (dma_hw->ints1 & (1u << dma_chan[i])) {
            dma_hw->ints1 = 1u << dma_chan[i];  // Clear interrupt

            if (!buffer_written[i])
                stat_empty_buffer_count++;
            buffer_written[i] = false;

            // Immediately reset the read address to the start of the buffer.
            // If the main loop is delayed (e.g. blocked in lcd_update's
            // 20 ms spi_write_blocking call) and the DMA chain fires this
            // channel again before pdm_audio_update() runs, the DMA will
            // re-play the old (silent) buffer contents instead of reading
            // past the end of the buffer into arbitrary memory.
            dma_channel_set_read_addr(dma_chan[i], dma_buffer[i], false);

            // Signal the main loop to refill this buffer.
            // fill_dma_buffer() takes ~5 ms (SDM 1024×32 at 115 MHz);
            // running it here would block USB SOF interrupts.
            dma_pending_mask |= (1u << i);
        }
    }
}

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void pdm_audio_init(uint32_t sample_rate) {
    current_sample_rate = sample_rate;

    sdm_reset(&sdm);

    // PIO setup on pio0 (pio1 reserved for any future use)
    pdm_pio    = pio0;
    pdm_sm     = pio_claim_unused_sm(pdm_pio, true);
    pdm_offset = pio_add_program(pdm_pio, &pdm_audio_program);
    pdm_audio_program_init(pdm_pio, pdm_sm, pdm_offset, PDM_PIN_OUT, sample_rate);

    // Claim two DMA channels for chained ping-pong
    dma_chan[0] = dma_claim_unused_channel(true);
    dma_chan[1] = dma_claim_unused_channel(true);

    dma_channel_config c0 = dma_channel_get_default_config(dma_chan[0]);
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_32);
    channel_config_set_read_increment(&c0, true);
    channel_config_set_write_increment(&c0, false);
    channel_config_set_dreq(&c0, pio_get_dreq(pdm_pio, pdm_sm, true));
    channel_config_set_chain_to(&c0, dma_chan[1]);
    channel_config_set_high_priority(&c0, true);
    dma_channel_configure(dma_chan[0], &c0,
                          &pdm_pio->txf[pdm_sm],
                          dma_buffer[0],
                          DMA_BUFFER_WORDS,
                          false);

    dma_channel_config c1 = dma_channel_get_default_config(dma_chan[1]);
    channel_config_set_transfer_data_size(&c1, DMA_SIZE_32);
    channel_config_set_read_increment(&c1, true);
    channel_config_set_write_increment(&c1, false);
    channel_config_set_dreq(&c1, pio_get_dreq(pdm_pio, pdm_sm, true));
    channel_config_set_chain_to(&c1, dma_chan[0]);
    channel_config_set_high_priority(&c1, true);
    dma_channel_configure(dma_chan[1], &c1,
                          &pdm_pio->txf[pdm_sm],
                          dma_buffer[1],
                          DMA_BUFFER_WORDS,
                          false);

    // Use DMA_IRQ_1 (IRQ_0 used by LCD DMA)
    dma_channel_set_irq1_enabled(dma_chan[0], true);
    dma_channel_set_irq1_enabled(dma_chan[1], true);
    irq_set_exclusive_handler(DMA_IRQ_1, dma_irq_handler);
    irq_set_enabled(DMA_IRQ_1, true);

    memset(dma_buffer,   0, sizeof(dma_buffer));
    memset(ring_buffer,  0, sizeof(ring_buffer));

    dma_started = false;
}

void pdm_audio_set_sample_rate(uint32_t sample_rate) {
    current_sample_rate = sample_rate;
    pdm_audio_program_set_sample_rate(pdm_pio, pdm_sm, sample_rate);
}

uint32_t pdm_audio_write(const int16_t *samples, uint32_t sample_count) {
    uint32_t written = 0;

    bool start_dma = false;

    uint32_t irq_state = save_and_disable_interrupts();

    while (written < sample_count && ring_count < RING_BUFFER_SIZE - 2) {
        ring_buffer[ring_write_pos] = samples[written];
        ring_write_pos = (ring_write_pos + 1) % RING_BUFFER_SIZE;
        ring_count++;
        written++;
    }

    if (written > 0) {
        buffer_written[0] = true;
        buffer_written[1] = true;
    }

    if (!dma_started && ring_count >= BUFFER_START_THRESHOLD) {
        dma_started = true;
        start_dma = true;
    }

    restore_interrupts(irq_state);
    // Critical section ends here — interrupts re-enabled before heavy SDM work.

    if (start_dma) {
        // Start DMA with the zeroed buffers from pdm_audio_init().
        // Do NOT pre-fill here: with 4096-sample buffers each fill takes
        // ~23 ms (SDM 4096×32 iterations at 115 MHz), and this code path
        // runs inside the USB IRQ — pre-filling would block USB SOF
        // interrupts for ~46 ms, causing the host to abort the stream.
        // The first ~85 ms of output will be silence (zeros) until
        // pdm_audio_update() fills the buffers from the main loop.
        dma_channel_start(dma_chan[0]);
    }

    if (written > 0)
        stats_record_samples_written(written);

    return written;
}

bool pdm_audio_can_write(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    bool can = ring_count < RING_BUFFER_SIZE - 2;
    restore_interrupts(irq_state);
    return can;
}

uint32_t pdm_audio_get_free_count(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t free = RING_BUFFER_SIZE - ring_count;
    restore_interrupts(irq_state);
    return free;
}

uint32_t pdm_audio_get_free_count_approx(void) {
    // Single volatile 32-bit read is atomic on Cortex-M; no lock needed.
    return RING_BUFFER_SIZE - ring_count;
}

void pdm_audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    volume_level = volume;
}

void pdm_audio_set_mute(bool mute) {
    is_muted = mute;
}

uint32_t pdm_audio_get_buffer_count(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t count = ring_count;
    restore_interrupts(irq_state);
    return count;
}

void pdm_audio_clear_buffer(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    ring_read_pos  = 0;
    ring_write_pos = 0;
    ring_count     = 0;
    last_left      = 0;
    last_right     = 0;
    // DMA is not stopped — it will briefly underrun (silence) until new
    // USB samples arrive and refill the buffer.
    restore_interrupts(irq_state);
}

void pdm_audio_update(void) {
    // Atomically grab and clear the pending mask.
    uint32_t irq_state = save_and_disable_interrupts();
    uint8_t pending = dma_pending_mask;
    dma_pending_mask = 0;
    restore_interrupts(irq_state);

    // Fill each completed DMA buffer and reset its read address.
    // This runs the heavy SDM computation (DMA_BUFFER_FRAMES × PDM_WORDS_PER_FRAME
    // × 32 SDM iterations) here in the main loop instead of inside the DMA IRQ,
    // keeping USB SOF interrupt latency low.
    // We have ~85 ms (one full DMA buffer at 48 kHz) before the DMA
    // chain cycles back to the same buffer, so main-loop timing is fine.
    for (int i = 0; i < NUM_DMA_BUFFERS; i++) {
        if (pending & (1u << i)) {
            fill_dma_buffer(dma_buffer[i]);
            dma_channel_set_read_addr(dma_chan[i], dma_buffer[i], false);
        }
    }
}

void pdm_audio_get_stats(pdm_audio_stats_t *stats) {
    uint32_t irq_state = save_and_disable_interrupts();

    stats->fifo_level            = pio_sm_get_tx_fifo_level(pdm_pio, pdm_sm);
    stats->fifo_level_min        = stat_fifo_level_min;
    stats->irq_interval_us       = stat_irq_interval_us;
    stats->irq_interval_min_us   = (stat_irq_interval_min_us == UINT32_MAX)
                                       ? 0 : stat_irq_interval_min_us;
    stats->irq_interval_max_us   = stat_irq_interval_max_us;
    stats->empty_buffer_count    = stat_empty_buffer_count;

    // Reset min/max tracking
    stat_fifo_level_min          = 8;
    stat_irq_interval_min_us     = UINT32_MAX;
    stat_irq_interval_max_us     = 0;
    stat_empty_buffer_count      = 0;

    restore_interrupts(irq_state);
}
