# Hardware Configuration

Pin assignments and hardware notes for supported boards.

## Supported Boards

| Board | CMake flag | Audio | LCD |
|-------|-----------|-------|-----|
| Waveshare RP2350-Touch-LCD-2.8 | *(default)* | I2S → PCM5101 DAC | ST7789T3 320×240 |
| Waveshare RP2350-LCD-0.96(-M) | `-DBOARD_096=ON` | PDM → RC filter | ST7735S 160×80 |

---

## Waveshare RP2350-Touch-LCD-2.8 (default)

### I2S Audio — GP2/3/4

| Signal | GPIO | Notes |
|--------|------|-------|
| BCLK   | GP2  | Bit clock |
| LRCK   | GP3  | Left/right clock (word select) |
| DIN    | GP4  | Serial data to PCM5101 |

PCM5101 is a slave-mode DAC — it derives all clocks from BCLK/LRCK, no MCLK required. I2S format: 64 BCK/frame, 16-bit audio left-justified in the MSBs of each 32-bit slot.

### LCD — SPI1

| Signal | GPIO | Notes |
|--------|------|-------|
| SCK    | GP10 | SPI1 clock, 40 MHz |
| MOSI   | GP11 | SPI1 TX |
| CS     | GP13 | Active low |
| DC     | GP14 | Data/command select |
| RST    | GP15 | Active low reset |
| BL     | GP16 | Backlight PWM |

ST7789T3 240×320 native, rotated 90° CW for 320×240 landscape. DMA-driven SPI transfers on DMA_IRQ_0.

### System Clock

Default RP2350 system clock (125 MHz or as set by pico-sdk). No override needed for I2S.

---

## Waveshare RP2350-LCD-0.96(-M) — `-DBOARD_096=ON -DAUDIO_PDM=ON`

### PDM Audio — GP18

| Signal | GPIO | Notes |
|--------|------|-------|
| PDM OUT | GP18 | Single-wire sigma-delta bitstream |

**External filtering required.** The PDM output is a 3.072 MHz single-bit stream — it must be low-pass filtered before driving a speaker or amplifier:

```
GP18 ──[10kΩ]──┬── audio out (to amplifier)
               │
             [4.7nF]
               │
              GND
```

Suggested values: R = 10 kΩ, C = 4.7 nF → f_c ≈ 3.4 kHz. Adjust C for desired bandwidth. A DC-blocking capacitor (10–100 µF) in series with the audio output is also recommended.

### LCD — SPI1

| Signal | GPIO | Notes |
|--------|------|-------|
| SCK    | GP10 | SPI1 clock, 10 MHz |
| MOSI   | GP11 | SPI1 TX |
| CS     | GP9  | Active low |
| DC     | GP8  | Data/command select |
| RST    | GP12 | Active low reset |
| BL     | GP25 | Backlight PWM |

ST7735S 160×80. Physical pixel memory starts at offset (1, 26) — applied automatically in `lcd_096.c`. Blocking SPI transfers (~2.6 ms per full frame).

### System Clock

`main.c` calls `set_sys_clock_khz(115200, false)` before `stdio_init_all()` when `AUDIO_PDM` is defined. This gives:

```
115200000 / (48000 × 64) = 37.5   (PDM clkdiv, exact in 8-bit fractional divider)
```

USB PLL remains at 48 MHz independently — USB is unaffected.

---

## UART Debug Output

| Signal | GPIO | Notes |
|--------|------|-------|
| TX     | GP0  | UART0, 115200 baud |
| RX     | GP1  | UART0 |

Use `make monitor` to connect. Stats are printed every 2 seconds:

```
[STATS] buf=51% (8362/16384) sps=96012 sr=48000 PLAY vol=80 asrc=1.001
        fifo=7 (min=6) irq=85333us (84998-85668) empty=0
```

---

## DMA Channel Allocation

| Channel | Owner | IRQ |
|---------|-------|-----|
| 0       | LCD SPI (2.8" only) | DMA_IRQ_0 |
| 1       | LCD SPI (2.8" only) | DMA_IRQ_0 |
| 2       | Audio ping (I2S or PDM) | DMA_IRQ_1 |
| 3       | Audio pong (I2S or PDM) | DMA_IRQ_1 |

The 0.96" board uses blocking SPI so claims no DMA channels for the LCD.
