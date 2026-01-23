// SPDX-License-Identifier: MIT
// Audio output backend abstraction
//
// Selects I2S (default) or PDM at compile time via -DAUDIO_PDM.
// Include this instead of i2s_audio.h or pdm_audio.h.

#ifndef _AUDIO_OUT_H_
#define _AUDIO_OUT_H_

#ifdef AUDIO_PDM
#  include "pdm_audio.h"
#  define AUDIO_OUT_RING_BUFFER_SIZE  PDM_RING_BUFFER_SIZE
#  define audio_out_init              pdm_audio_init
#  define audio_out_set_sample_rate   pdm_audio_set_sample_rate
#  define audio_out_write             pdm_audio_write
#  define audio_out_can_write         pdm_audio_can_write
#  define audio_out_get_free_count    pdm_audio_get_free_count
#  define audio_out_set_volume        pdm_audio_set_volume
#  define audio_out_set_mute          pdm_audio_set_mute
#  define audio_out_get_buffer_count  pdm_audio_get_buffer_count
#  define audio_out_clear_buffer      pdm_audio_clear_buffer
#  define audio_out_update            pdm_audio_update
typedef pdm_audio_stats_t audio_out_stats_t;
#  define audio_out_get_stats         pdm_audio_get_stats
#else
#  include "i2s_audio.h"
#  define AUDIO_OUT_RING_BUFFER_SIZE  I2S_RING_BUFFER_SIZE
#  define audio_out_init              i2s_audio_init
#  define audio_out_set_sample_rate   i2s_audio_set_sample_rate
#  define audio_out_write             i2s_audio_write
#  define audio_out_can_write         i2s_audio_can_write
#  define audio_out_get_free_count    i2s_audio_get_free_count
#  define audio_out_set_volume        i2s_audio_set_volume
#  define audio_out_set_mute          i2s_audio_set_mute
#  define audio_out_get_buffer_count  i2s_audio_get_buffer_count
#  define audio_out_clear_buffer      i2s_audio_clear_buffer
#  define audio_out_update            i2s_audio_update
typedef i2s_audio_stats_t audio_out_stats_t;
#  define audio_out_get_stats         i2s_audio_get_stats
#endif

#endif /* _AUDIO_OUT_H_ */
