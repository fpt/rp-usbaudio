# RP2350 USB Audio DAC

A USB Audio Class 2.0 (UAC2) speaker device for RP2350/Pico2 that outputs audio through a PWM-driven buzzer and displays status on an LCD.

## Features

- USB Audio Class 2.0 compliant (works with macOS, Windows, iOS, Linux)
- Stereo 16-bit audio at 44.1kHz or 48kHz
- Asynchronous mode with feedback endpoint for stable streaming
- Volume control and mute support
- LCD display showing:
  - Connection status (Disconnected/Connected/Streaming)
  - Sample rate and channel info
  - Volume level and mute state
  - Real-time audio level meters (L/R)
- PWM audio output through onboard buzzer

## Hardware Requirements

- RP2350/Pico2 board
- ST7789 LCD (240x280, 1.69")
- Buzzer on GPIO 2

### Pin Configuration

| Function | GPIO |
|----------|------|
| PWM Audio (Buzzer) | 2 |
| LCD DC | 8 |
| LCD CS | 9 |
| LCD CLK | 10 |
| LCD MOSI | 11 |
| LCD RST | 13 |
| LCD Backlight | 25 |

## Building

### Prerequisites

- Docker
- pico-sdk at `../pico-sdk`

### Build Commands

```bash
make build      # Build firmware
make deploy     # Copy .uf2 to device in BOOTSEL mode
make monitor    # Serial console (115200 baud)
make clean      # Clean build artifacts
```

## Usage

1. Build and flash the firmware
2. Connect the RP2350 to a computer/phone via USB
3. The device appears as "RP2350 USB Audio"
4. Select it as an audio output device
5. Play audio - you'll see the level meters move and hear sound from the buzzer

## Audio Quality

The PWM audio output has 8-bit resolution and is intended for simple audio feedback rather than high-fidelity playback. The buzzer produces recognizable music but with noticeable noise. For better quality, consider adding an I2S DAC.

## Development Status

**Current Phase:** Phase 1 (Basic USB Audio with PWM) - Completed

See [docs/roadmap.md](docs/roadmap.md) for planned development phases:
- Phase 1: Basic USB Audio with PWM output [COMPLETED]
- Phase 2: I2S DAC with PCM5122 for proper async feedback [PLANNED]
- Phase 3: Adaptive playback as alternative approach [PLANNED]

## Documentation

See the `docs/` directory for detailed information:

- [Development Roadmap](docs/roadmap.md) - Development phases and future plans
- [USB Audio Implementation](docs/usb-audio.md) - TinyUSB configuration and callbacks
- [Hardware Details](docs/hardware.md) - Pin assignments and hardware notes
- [Troubleshooting](docs/troubleshooting.md) - Common issues and solutions

## License

MIT License

## Acknowledgments

- [TinyUSB](https://github.com/hathach/tinyusb) for the USB stack
- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk)
