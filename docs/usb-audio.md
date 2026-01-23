# USB Audio Implementation

This document covers the USB Audio Class 2.0 (UAC2) implementation using TinyUSB.

**Current Phase:** Phase 1 - See [roadmap.md](roadmap.md) for development phases.

## Overview

The device implements a UAC2 speaker (sink) with:
- Stereo 16-bit PCM audio
- Sample rates: 44100 Hz and 48000 Hz
- Asynchronous transfer mode with feedback endpoint
- Feature unit for volume and mute control

## TinyUSB Configuration

Key settings in `tusb_config.h`:

```c
#define CFG_TUD_AUDIO                   1
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT   1
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_RX  2    // 16-bit
#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX          2    // Stereo
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP            1    // Feedback endpoint
```

## Feedback Endpoint

### Phase 1 Workaround: AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED

The feedback endpoint tells the host how fast the device is consuming audio data. TinyUSB provides several methods:

| Method | Description | Phase 1 Status |
|--------|-------------|----------------|
| `AUDIO_FEEDBACK_METHOD_FIFO_COUNT` | Calculate rate from FIFO fill level | **BROKEN** - stalls after ~60 packets |
| `AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED` | Report fixed sample rate | **WORKAROUND** - used currently |
| `AUDIO_FEEDBACK_METHOD_FREQUENCY_FLOAT` | Report measured rate | **IDEAL** - for Phase 2 with I2S DAC |

**Current implementation (Phase 1):**

```c
void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf,
                                   audio_feedback_params_t* feedback_param) {
    (void)func_id;
    (void)alt_itf;
    feedback_param->method = AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED;
    feedback_param->sample_freq = current_sample_rate;
}
```

### Why FIFO Counting Fails

The FIFO counting method calculates the feedback value based on how full the receive FIFO is. This seems logical but fails in practice because:

1. The calculation is sensitive to timing jitter
2. Small errors accumulate and cause the host to throttle
3. Eventually the host stops sending data entirely

### Why Fixed Frequency is a Workaround

With fixed frequency feedback, we tell the host "I consume exactly 48000 samples/second" regardless of actual consumption. This works because:

- PWM output rate roughly matches the declared rate
- Small mismatches are absorbed by the buffer

However, this is not ideal because:
- No actual rate measurement
- Buffer will slowly drift (grow or shrink)
- Long playback may eventually cause underrun/overflow

### Future: Proper Async Feedback (Phase 2)

With an I2S DAC like PCM5122 that has its own master clock:

```
USB Audio → I2S Buffer → PCM5122 (consumes at its own clock rate)
                              ↓
                        Count actual samples consumed
                              ↓
                        Calculate true sample rate
                              ↓
                        Report via FREQUENCY_FLOAT feedback
```

This creates a true closed-loop system where the host adjusts send rate based on actual DAC consumption.

### Alternative: Adaptive Playback (Phase 3)

Instead of adjusting host rate, adjust device playback rate:
- Monitor buffer fill level
- Speed up playback if buffer growing
- Slow down playback if buffer shrinking
- Use sample interpolation for smooth rate changes

See [roadmap.md](roadmap.md) for details.

## Audio Data Flow

```
USB Host → tud_audio_rx_done_post_read_cb() → tud_audio_read() → PWM buffer → Buzzer
```

### Reading Audio Data

Audio data must be read promptly to prevent FIFO overflow:

```c
static void audio_task(void) {
    // Read ALL available data - don't wait for buffer space
    uint16_t bytes_read = tud_audio_read(spk_buf, sizeof(spk_buf));

    if (bytes_read > 0) {
        // Process and forward to PWM audio
        uint32_t samples = bytes_read / 2;  // 16-bit samples
        pwm_audio_write(spk_buf, samples);
    }
}
```

**Important:** Always read from USB even if the PWM buffer is full. Failing to read causes the USB FIFO to overflow and streaming to fail.

## Volume Control

Volume is reported in 1/256 dB units:
- Range: -12800 to 0 (i.e., -50dB to 0dB)
- Resolution: 256 (1dB steps)

```c
// Convert from USB volume (dB * 256) to percentage (0-100)
int16_t vol_db256 = volume[0];  // e.g., -6400 = -25dB
uint8_t vol_pct = (vol_db256 + 12800) * 100 / 12800;  // → 50%
```

## USB Descriptors

The USB descriptor structure:

```
Device Descriptor
└── Configuration Descriptor
    └── Interface Association Descriptor (Audio)
        ├── Audio Control Interface
        │   ├── Clock Source Entity
        │   ├── Input Terminal (USB streaming)
        │   ├── Feature Unit (volume/mute)
        │   └── Output Terminal (speaker)
        └── Audio Streaming Interface
            ├── Alt Setting 0 (zero bandwidth)
            └── Alt Setting 1 (streaming)
                ├── AS Interface Descriptor
                ├── Format Descriptor (PCM, 16-bit, stereo)
                ├── Data Endpoint (OUT)
                └── Feedback Endpoint (IN)
```

## Callbacks Reference

| Callback | Purpose |
|----------|---------|
| `tud_audio_rx_done_post_read_cb` | Called when audio data arrives |
| `tud_audio_set_itf_cb` | Called when alt setting changes (start/stop streaming) |
| `tud_audio_get_req_entity_cb` | Handle GET requests (volume, sample rate) |
| `tud_audio_set_req_entity_cb` | Handle SET requests (volume, sample rate) |
| `tud_audio_feedback_params_cb` | Configure feedback mechanism |

## References

- TinyUSB UAC2 example: `pico-sdk/lib/tinyusb/examples/device/uac2_speaker_fb/`
- USB Audio Class 2.0 specification
- TinyUSB audio class: `pico-sdk/lib/tinyusb/src/class/audio/`
