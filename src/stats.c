// SPDX-License-Identifier: MIT
// Statistics module - periodic UART debug output

#include "stats.h"

#include <stdio.h>

#include "pico/time.h"

#include "asrc.h"
#include "audio_out.h"
#include "usb_audio.h"

//--------------------------------------------------------------------
// Configuration
//--------------------------------------------------------------------

#define STATS_INTERVAL_MS 2000

//--------------------------------------------------------------------
// State
//--------------------------------------------------------------------

static uint32_t last_stats_time    = 0;
static uint32_t underrun_count     = 0;
static uint32_t total_written      = 0;
static uint32_t written_since_last = 0;

//--------------------------------------------------------------------
// Public API
//--------------------------------------------------------------------

void stats_init(void) {
    last_stats_time    = to_ms_since_boot(get_absolute_time());
    underrun_count     = 0;
    total_written      = 0;
    written_since_last = 0;
}

void stats_record_underrun(void) {
    underrun_count++;
}

void stats_record_samples_written(uint32_t count) {
    total_written      += count;
    written_since_last += count;
}

void stats_task(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_stats_time < STATS_INTERVAL_MS) {
        return;
    }
    uint32_t elapsed = now - last_stats_time;
    last_stats_time = now;

    uint32_t ring_count = audio_out_get_buffer_count();
    uint32_t ring_free  = audio_out_get_free_count();
    uint32_t total_ring = ring_count + ring_free;
    uint32_t fill_pct   = (total_ring > 0) ? (ring_count * 100 / total_ring) : 0;

    uint32_t sps = (written_since_last * 1000) / elapsed;
    written_since_last = 0;

    audio_out_stats_t aout;
    audio_out_get_stats(&aout);

    uint32_t asrc_ratio = asrc_get_ratio_x1000();

    printf("[STATS] buf=%lu%% (%lu/%lu) sps=%lu sr=%lu %s vol=%u%s und=%lu\n",
           (unsigned long)fill_pct,
           (unsigned long)ring_count, (unsigned long)total_ring,
           (unsigned long)sps,
           (unsigned long)usb_audio_get_sample_rate(),
           usb_audio_is_streaming() ? "PLAY" : "STOP",
           usb_audio_get_volume(),
           usb_audio_get_mute() ? " MUTE" : "",
           (unsigned long)underrun_count);

    printf("        fifo=%u (min=%u) irq=%luus (%lu-%lu) empty=%lu asrc=%lu.%03lu\n",
           aout.fifo_level, aout.fifo_level_min,
           (unsigned long)aout.irq_interval_us,
           (unsigned long)aout.irq_interval_min_us,
           (unsigned long)aout.irq_interval_max_us,
           (unsigned long)aout.empty_buffer_count,
           (unsigned long)(asrc_ratio / 1000),
           (unsigned long)(asrc_ratio % 1000));
}
