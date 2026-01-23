# Architecture Design

RP2350 USB Audio DAC — UAC1 asynchronous speaker with selectable audio output and LCD backend.

## Overview

The device presents itself to the host as a **USB Audio Class 1.0 (UAC1) asynchronous speaker**. Audio received over USB is resampled through a software ASRC, then written to the selected audio output backend. An LCD shows real-time status including buffer health, ASRC ratio, and VU meters.

The audio output and LCD are selected at **compile time** via CMake flags:

| Flag | Default | Effect |
|------|---------|--------|
| `AUDIO_PDM=ON` | OFF | PDM on GP18 (64× oversampled sigma-delta) |
| `BOARD_096=ON` | OFF | 160×80 ST7735S LCD instead of 320×240 ST7789T3 |

```
USB Host
   │  UAC1 isochronous OUT  (48 bytes/ms @ 48 kHz)
   │  UAC1 feedback IN      (3 bytes every 4 ms)
   ▼
┌──────────────────────────────────────────────┐
│  usb_audio.c  (pico-extras USB device stack) │
│  ┌──────────────┐   ┌────────────────────┐   │
│  │ _as_audio_   │   │ _as_sync_packet    │   │
│  │ packet()     │   │ → _feedback_value  │   │
│  │              │   │   (nominal rate)   │   │
│  └──────┬───────┘   └────────────────────┘   │
│         │ asrc_update_buffer_level()          │
│         │ asrc_process()  ←── asrc.c         │
│         │                                     │
│         ▼ audio_out_write()  (audio_out.h)   │
└──────────────────────────────────────────────┘
           │
           ├── [I2S] i2s_audio.c  (PIO + chained DMA)
           │         ring_buffer[16384] → PCM5101 (GP2/3/4)
           │
           └── [PDM] pdm_audio.c  (PIO + chained DMA + SDM)
                     ring_buffer[16384] → RC filter (GP18)

main loop (every 80 ms)
└── audio_out_update()         // no-op for I2S; SDM refill for PDM
└── audio_ui.c → lcd.c
      ├── [2.8"] lcd_028.c  SPI DMA @ 40 MHz, DMA_IRQ_0
      └── [0.96"] lcd_096.c  blocking SPI @ 10 MHz
```

## USB Stack — pico-extras

The project uses the **pico-extras `usb_device` library** rather than TinyUSB. This stack is fully interrupt-driven: USB SOF and endpoint interrupts are handled in hardware without a polling task. The main loop is free to block (e.g. during LCD SPI transfers) without affecting audio timing.

Key pico-extras types used:

| Type | Role |
|------|------|
| `usb_device` | Root device object, holds descriptor pointers |
| `usb_interface` | Audio Control (AC) and Audio Streaming (AS) interfaces |
| `usb_endpoint` | EP1 OUT (audio data), EP2 IN (feedback) |
| `usb_transfer` / `usb_transfer_type` | Callback-based transfer handling |

## USB Descriptors (usb_audio.c)

```
Device  bcdUSB=1.10  VID=0x2e8a  PID=0xfedd

Configuration
├── Interface 0  AudioControl
│   └── CS Header → InputTerminal(1) → FeatureUnit(2) → OutputTerminal(3)
│       Controls: Mute, Volume (−90 dB … 0 dB, 1 dB steps)
└── Interface 1  AudioStreaming  alt=0 (idle) / alt=1 (active)
    ├── CS AS General   (PCM, terminal link=1, delay=1)
    ├── CS Format Type I  16-bit stereo, 44100 Hz / 48000 Hz
    ├── EP 0x01  Isochronous Asynchronous OUT  (196 bytes max)
    │   └── CS Endpoint  (sampling freq control)
    └── EP 0x82  Isochronous Feedback IN  (3 bytes, bRefresh=2 → every 4 ms)
```

`bmAttributes = 5` on EP1 declares **asynchronous** mode: the device controls the rate and informs the host via the feedback endpoint.

## Audio Backend Abstraction (audio_out.h)

`audio_out.h` is a header-only macro alias layer. Including it instead of `i2s_audio.h` or `pdm_audio.h` gives backend-agnostic names throughout `usb_audio.c`, `main.c`, and `stats.c`:

| Abstract name | I2S target | PDM target |
|---------------|------------|------------|
| `audio_out_init` | `i2s_audio_init` | `pdm_audio_init` |
| `audio_out_write` | `i2s_audio_write` | `pdm_audio_write` |
| `audio_out_get_buffer_count` | `i2s_audio_get_buffer_count` | `pdm_audio_get_buffer_count` |
| `audio_out_update` | `i2s_audio_update` (no-op) | `pdm_audio_update` (SDM refill) |
| `AUDIO_OUT_RING_BUFFER_SIZE` | `I2S_RING_BUFFER_SIZE` | `PDM_RING_BUFFER_SIZE` |
| `audio_out_stats_t` | `i2s_audio_stats_t` | `pdm_audio_stats_t` |
| … | … | … |

## Audio Data Path

Each USB audio packet (~48–49 stereo frames at 48 kHz) arrives at `_as_audio_packet()`:

```
USB packet (4 bytes/frame, stereo 16-bit PCM)
    │
    ├─ peak tracking  (_peak_left / _peak_right for VU display)
    │
    ├─ asrc_update_buffer_level(buf_count, AUDIO_OUT_RING_BUFFER_SIZE)
    │   → adjusts current_ratio_x10000 proportionally
    │
    └─ asrc_process(in, n, asrc_out, 256)
        → linear-interpolated resampling
        └─ audio_out_write(asrc_out, out_count)
            → ring_buffer[]  (critical section, interrupts disabled)
```

### ASRC — Clock Drift Compensation

USB host and RP2350 run from independent clocks. Without compensation, the audio ring buffer would slowly overflow or underflow.

**Dual mechanism:**

1. **USB feedback endpoint** (`_as_sync_packet`): returns the nominal sample rate as a 10.14 fixed-point value every 4 ms. This keeps the host sending at approximately the right rate.

   ```
   _feedback_value = (sample_rate << 14) / 1000   (UAC1 10.14 format)
   ```

2. **ASRC** (`asrc.c`): a software resampler that adjusts its output rate based on the ring buffer fill level, targeting 50% fill. This is the active correction loop.

   ```
   error      = 50% − fill%
   adjustment = error × 0.04%   (clamped to ±2%)
   ratio      += (target_ratio − ratio) / 16   (exponential smoothing)
   ```

   The ratio is applied as a 16.16 fixed-point step in a linear interpolator across consecutive stereo frames, with inter-buffer continuity maintained via `prev_left/prev_right`.

   The ratio is updated every 8 USB packets (~8 ms), with a ±2% maximum range (980–1020 × 0.001).

## I2S Backend (i2s_audio.c)

**PIO program** (`i2s_audio.pio`): generates BCLK, LRCK, and DOUT in 64-BCK I2S format (32 bits per channel, 16-bit audio in the MSBs). Runs on PIO0.

**DMA**: two channels chained in ping-pong on **DMA_IRQ_1**. Each buffer holds 2048 stereo frames. When one buffer completes, the IRQ handler refills it from the ring buffer.

```
ring_buffer[16384 int16_t]   ~170 ms at 48 kHz stereo
    ↓ fill_dma_buffer() (called from DMA_IRQ_1)
dma_buffer[0][4096]  ←→  dma_buffer[1][4096]   chained ping-pong
    ↓
PIO TX FIFO  →  BCLK/LRCK/DOUT  →  PCM5101
```

DMA starts only after the ring buffer reaches 50% fill (`BUFFER_START_THRESHOLD = 8192`).

On underrun, the last sample is faded to silence (`× 15/16` per sample) to avoid clicks.

| Signal | GPIO |
|--------|------|
| BCLK   | GP2  |
| LRCK   | GP3  |
| DIN    | GP4  |

## PDM Backend (pdm_audio.c)

**Oversampling:** `PDM_OVERSAMPLE = 64` (defined in `pdm_audio.h`). Each PCM sample produces 64 PDM bits = 2 × 32-bit PIO words.

**Bit rate** at 48 kHz: 48000 × 64 = 3.072 MHz. At 115.2 MHz sys clock: clkdiv = 37.5 (exactly representable in the PIO 8-bit fractional divider).

**SDM**: 4th-order Direct Form 2 sigma-delta modulator, ported from `tierneytim/Pico-USB-audio`. Runs in the main loop via `pdm_audio_update()` — not in the DMA IRQ — to avoid blocking USB SOF interrupts (~10 ms per fill at 115 MHz).

**DMA**: two channels chained in ping-pong on **DMA_IRQ_1** (same IRQ as I2S; only one backend is compiled).

```
ring_buffer[16384 int16_t]   ~170 ms at 48 kHz stereo
    ↓ fill_dma_buffer() (called from main loop via pdm_audio_update)
      runs SDM 2× per PCM frame → 2 uint32_t PDM words per frame
dma_buffer[0][8192]  ←→  dma_buffer[1][8192]   chained ping-pong (~85 ms each)
    ↓
PIO TX FIFO  →  single-bit output  →  RC filter  →  speaker
```

| Signal | GPIO | Notes |
|--------|------|-------|
| PDM OUT | GP18 | RC low-pass (fc ≈ 20 kHz) + DC-blocking cap required |

To change oversampling ratio, edit `#define PDM_OVERSAMPLE` in `pdm_audio.h`. Must be a multiple of 32.

## LCD Backends

### 2.8" ST7789T3 — lcd_028.c (default)

320×240, SPI1 at 40 MHz. Uses **SPI DMA** (DMA_IRQ_0) for non-blocking transfers. The main loop calls `lcd_update_wait()` before the next update.

| Signal | GPIO |
|--------|------|
| SCK    | GP10 |
| MOSI   | GP11 |
| CS     | GP13 |
| DC     | GP14 |
| RST    | GP15 |
| BL     | GP16 (PWM) |

### 0.96" ST7735S — lcd_096.c (BOARD_096)

160×80, SPI1 at 10 MHz. Uses **blocking SPI** writes. `lcd_update_wait()` and `lcd_is_busy()` are no-ops. Physical memory starts at offset (1, 26) — `set_window()` applies this automatically. The blocking transfer for a full frame (~2.6 ms) is well within the 85 ms PDM DMA buffer, so no special handling is needed.

| Signal | GPIO |
|--------|------|
| SCK    | GP10 |
| MOSI   | GP11 |
| CS     | GP9  |
| DC     | GP8  |
| RST    | GP12 |
| BL     | GP25 (PWM) |

## Display UI

Updated every 80 ms. Uses dirty flags to redraw only changed regions. Layout adapts to screen size via `LCD_WIDTH`/`LCD_HEIGHT` from `lcd.h`.

**2.8" layout (320×240):** large VU bars (50 px tall), wide status region, BUF/ASRC bars.

**0.96" layout (160×80):** compact horizontal bars for BUF, ASRC, L, R — all 9 px tall, two-character labels.

**BUF bar**: green when near 50%, orange when drifting, red when far off. White center marker at 50%.

**ASRC bar**: center-anchored, moves right when ratio > 1.000 (buffer draining) or left when < 1.000.

**VU bars**: green → yellow → orange → red.

## Main Loop (main.c)

```
init:
  [PDM only] set_sys_clock_khz(115200)
  lcd_init → splash → audio_out_init(48000) → stats → usb_audio → draw UI

loop every 80 ms:
  audio_out_update()        // no-op for I2S; SDM DMA refill for PDM
  stats_task()              // UART stats every 2 s
  gather state from usb_audio / audio_out / asrc
  audio_ui_set_*()          // update dirty flags
  if needs_update:
      lcd_update_wait()     // wait for previous SPI DMA (2.8" only)
      audio_ui_update()     // redraw dirty regions to framebuffer
      lcd_update()          // start SPI DMA (2.8") or blocking write (0.96")
  watchdog_update()
```

The pico-extras USB stack handles audio entirely in interrupt context — the main loop never needs to call a USB task function.

## Build System

Docker image (`rp2350-usbaudio`) contains the ARM cross-compiler and clones **pico-extras** into `/pico-extras` at build time.

```
cmake -DPICO_SDK_PATH=/pico-sdk -DPICO_EXTRAS_PATH=/pico-extras \
      [-DAUDIO_PDM=ON] [-DBOARD_096=ON]
```

pico-extras provides `usb_device`, LUFA USB descriptor types (vendored in `src/lufa/`), and `pico_extras_import.cmake`.

## File Map

| File | Purpose |
|------|---------|
| `src/main.c` | Entry point, main loop, UI orchestration |
| `src/usb_audio.c/h` | UAC1 device: descriptors, endpoint callbacks, volume/mute/rate control |
| `src/audio_out.h` | Compile-time backend alias layer (no .c needed) |
| `src/i2s_audio.c/h` | PIO+DMA I2S driver for PCM5101 |
| `src/i2s_audio.pio` | PIO program (BCLK/LRCK/DOUT generation) |
| `src/pdm_audio.c/h` | PIO+DMA PDM driver with 4th-order SDM |
| `src/pdm_audio.pio` | PIO program (single-bit output) |
| `src/asrc.c/h` | Proportional software resampler |
| `src/audio_ui.h` | UI interface (state setters, draw API) |
| `src/audio_ui_028.c` | UI layout for 320×240 |
| `src/audio_ui_096.c` | UI layout for 160×80 |
| `src/lcd.h` | LCD interface + board-conditional pin/size constants |
| `src/lcd_028.c` | ST7789T3 SPI+DMA driver |
| `src/lcd_096.c` | ST7735S blocking SPI driver |
| `src/splash.c/h` | Boot splash screen |
| `src/font.c/h` | 8×8 bitmap font |
| `src/stats.c/h` | Periodic UART stats output |
| `src/lufa/` | Vendored LUFA USB descriptor type headers |
| `CMakeLists.txt` | Build config: audio/LCD backend selection, PIO codegen |
| `Dockerfile` | Build environment (clones pico-extras) |
| `Makefile` | `make build` / `build-pdm` / `build-096` / `deploy` / `monitor` |
