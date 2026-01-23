# Development Roadmap

## Completed

### v0.1 — PWM prototype
Basic USB audio with PWM output to buzzer. TinyUSB-based, polling main loop. Proved the USB audio class negotiation and descriptor structure.

### v0.2 — I2S + pico-extras
Replaced TinyUSB with pico-extras `usb_device` stack (interrupt-driven). Added PIO+DMA I2S driver targeting PCM5101 DAC. Added proportional feedback + ASRC for clock drift. Added 320×240 ST7789 LCD status display.

### v0.3 — PDM backend
Added 4th-order delta-sigma PDM output for the Waveshare RP2350-LCD-0.96 board (single-wire, RC filter only). Shared ring buffer and ASRC with I2S.

### v0.4 — Multi-backend / multi-board
Unified both projects. `audio_out.h` abstraction layer selects I2S or PDM at compile time (`-DAUDIO_PDM`). `BOARD_096` flag selects the 160×80 ST7735S LCD and its layout. PDM oversampling increased from 32× to 64× (3.072 MHz @ 48 kHz).

---

## Potential Future Work

- **44.1 kHz PDM accuracy:** At 115.2 MHz sys clock, 44.1 kHz × 64 gives a non-integer clkdiv (~40.8). Could add a separate sys clock setting for 44.1 kHz or accept the small timing error (inaudible in practice).
- **Higher-order resampler:** Current ASRC uses linear interpolation. A polyphase or windowed-sinc filter would reduce resampling artifacts at the cost of CPU.
- **UAC2 support:** UAC1 is limited to 16-bit, 2-channel. UAC2 would allow 24-bit and higher sample rates, but requires a different USB stack configuration and host driver support on older OSes.
- **Hardware volume on PCM5101:** The PCM5101 supports hardware volume via a dedicated pin. Mapping the USB volume control to this would remove software volume scaling from the hot path.
