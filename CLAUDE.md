# RP2350 USB Audio DAC

USB Audio Class 1.0 (UAC1) asynchronous speaker with selectable audio output and LCD backend. Two target boards supported via build flags.

**Full architecture:** [docs/DESIGN.md](docs/DESIGN.md)

## Project Structure

```
├── src/
│   ├── main.c              # Entry point and main loop
│   ├── usb_audio.c/h       # UAC1 USB device (pico-extras stack)
│   ├── audio_out.h         # Audio backend abstraction (macro aliases)
│   ├── i2s_audio.c/h       # I2S backend: PIO + chained DMA → PCM5101
│   ├── i2s_audio.pio       # PIO program (BCLK/LRCK/DOUT)
│   ├── pdm_audio.c/h       # PDM backend: 4th-order SDM, PIO + DMA
│   ├── pdm_audio.pio       # PIO program (single-bit output)
│   ├── asrc.c/h            # Software resampler for clock drift
│   ├── audio_ui.h          # UI interface
│   ├── audio_ui_028.c      # LCD status display for 320×240
│   ├── audio_ui_096.c      # LCD status display for 160×80
│   ├── lcd.h               # LCD interface (board-conditional constants)
│   ├── lcd_028.c           # ST7789T3 driver: SPI DMA, 320×240
│   ├── lcd_096.c           # ST7735S driver: blocking SPI, 160×80
│   ├── splash.c/h          # Boot splash screen
│   ├── font.c/h            # 8×8 bitmap font
│   ├── stats.c/h           # Periodic UART debug output
│   └── lufa/               # Vendored LUFA USB descriptor headers
├── docs/
│   ├── DESIGN.md           # Full architecture (read this first)
│   ├── hardware.md         # Pin assignments and hardware notes
│   ├── usb-audio.md        # USB audio protocol notes
│   ├── troubleshooting.md  # Common issues
│   └── roadmap.md          # Development history and future plans
├── CMakeLists.txt
├── Dockerfile              # Build env (clones pico-extras)
├── Makefile
├── pico_sdk_import.cmake
└── pico_extras_import.cmake
```

## Build

Requires Docker and `../pico-sdk`.

```bash
make build      # Build firmware, I2S backend, 2.8" LCD (default)
make build-pdm  # Build firmware, PDM backend, 2.8" LCD
make build-096  # Build firmware, PDM backend, 0.96" LCD
make deploy     # Flash .uf2 to board in BOOTSEL mode
make monitor    # UART serial console (115200)
make clean      # Remove build artifacts
make shell      # Shell inside Docker container
```

CMake flags are independent and can be combined freely:

| Flag | Default | Effect |
|------|---------|--------|
| `AUDIO_PDM=ON` | OFF | PDM output on GP18 instead of I2S |
| `BOARD_096=ON` | OFF | 160×80 ST7735S LCD instead of 320×240 ST7789T3 |

The Docker image clones pico-extras at `/pico-extras` automatically on first `make docker-build`.

## Hardware

### Waveshare RP2350-Touch-LCD-2.8 (default)
- **Audio:** PCM5101 via I2S — BCLK=GP2, LRCK=GP3, DIN=GP4
- **LCD:** ST7789T3 320×240 on SPI1 — SCK=GP10, MOSI=GP11, CS=GP13, DC=GP14, RST=GP15, BL=GP16

### Waveshare RP2350-LCD-0.96 (`-DBOARD_096`)
- **Audio:** PDM on GP18 (RC low-pass filter + DC-blocking cap required)
- **LCD:** ST7735S 160×80 on SPI1 — SCK=GP10, MOSI=GP11, CS=GP9, DC=GP8, RST=GP12, BL=GP25

### Common
- **Debug:** UART0 115200 baud

## USB

- **Class:** USB Audio Class 1.0, asynchronous mode
- **VID:PID:** 0x2e8a:0xfedd
- **Formats:** 16-bit stereo PCM, 44100 Hz or 48000 Hz
- **Controls:** Volume (−90…0 dB), Mute
- **Feedback:** EP 0x82, 3-byte 10.14 fixed-point, every 4 ms

## Key Design Points

- **USB stack:** pico-extras `usb_device` (interrupt-driven, no polling task)
- **Clock drift:** dual mechanism — nominal feedback to host + ASRC resampler targeting 50% ring buffer fill
- **Audio abstraction:** `audio_out.h` provides `audio_out_*` macros that resolve to either `i2s_audio_*` or `pdm_audio_*` at compile time
- **I2S DMA:** ping-pong on DMA_IRQ_1; LCD SPI uses DMA_IRQ_0 — no conflict
- **PDM SDM:** 4th-order delta-sigma at 64× oversampling (3.072 MHz @ 48 kHz); SDM runs in main loop, not IRQ, to avoid blocking USB SOF interrupts
- **Ring buffer:** 16384 int16_t (~170 ms at 48 kHz), DMA starts at 50% fill

## LCD Display

The display updates every 80 ms showing:

| Region | Content |
|--------|---------|
| Top | Status, sample rate, volume |
| BUF bar | Audio ring buffer fill % (target 50%) |
| ASRC bar | Resampler ratio deviation from 1.000 |
| L/R VU bars | Peak audio levels |
