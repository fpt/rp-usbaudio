# Troubleshooting

Common issues and solutions.

## USB Audio Issues

### Device shows "Ready" but never "Playing"

**Symptoms:** LCD stays yellow, host sees the device but audio doesn't start.

**Causes / solutions:**
- Host may be holding the interface at alt=0. Try selecting the device explicitly in sound settings.
- Check `make monitor` output for `USB Audio: alt=1 streaming=1` — if missing, the host isn't activating the stream.
- Some hosts require a brief delay after enumeration. Unplug and reconnect.

---

### Audio cuts out or stutters

**Symptoms:** BUF bar drops to 0%, audio glitches, STATS shows `und=N` incrementing.

**Causes:**
1. ASRC not converging — BUF bar drifting steadily. Check the ASRC bar; if it's pegged at an extreme the ratio gain may need tuning.
2. Main loop blocked too long — unlikely unless something was added that blocks for >85 ms.
3. USB host sending less data than expected.

**Check via UART stats:**
```
[STATS] buf=2% (327/16384) sps=95840 ...   ← buffer draining (sps too low)
[STATS] buf=98% (16057/16384) sps=96180 ... ← buffer filling (sps too high)
```
A healthy run shows `buf` near 50% and `sps` close to the sample rate × 2 (stereo).

---

### No sound but BUF bar is healthy

**Symptoms:** BUF bar near 50%, VU meters moving, but no audio output.

- **I2S:** Check PCM5101 wiring (GP2/3/4). Verify the DAC power supply and XSMT pin (shutdown, active low on some modules).
- **PDM:** Check GP18 external RC filter. Verify DC-blocking cap is in series. Test with a multimeter — GP18 should show ~0.5 × Vcc average when playing a non-silent signal.

---

### Volume control has no effect

Volume is applied in software inside `fill_dma_buffer()`. If the host sends a volume command and the LCD shows the correct percentage but audio level is unchanged, check that `audio_out_set_volume()` is being called (verify in `audio_cmd_packet` in `usb_audio.c`).

---

## PDM-Specific Issues

### PDM output sounds distorted or clipping

The 4th-order SDM has a headroom limit. Ensure USB host volume is not at 100% for loud content — try 70–80%.

---

### PDM: brief silence at stream start (~85 ms)

**Expected behavior.** The DMA starts playing zeroed buffers until `pdm_audio_update()` fills them from the main loop. This is intentional — pre-filling inside the USB IRQ would block USB SOF interrupts for ~10 ms.

---

### PDM output has a constant DC click when stream starts/stops

Ensure `audio_out_clear_buffer()` is called when a new stream begins (it is, inside `as_set_alternate()` in `usb_audio.c`). The underrun fade-to-silence in `fill_dma_buffer()` handles the end of stream gracefully.

---

## LCD Issues

### 2.8" LCD: blank or white screen

- Verify SPI1 pins: SCK=GP10, MOSI=GP11, CS=GP13, DC=GP14, RST=GP15.
- Check backlight: BL=GP16 should be driven by PWM — confirm it's not floating.
- The ST7789T3 reset sequence requires 200 ms delays — ensure no early initialisation is cutting them short.

---

### 0.96" LCD: content appears shifted or off-screen

The ST7735S physical memory starts at offset (1, 26). If you see content shifted, verify `lcd_096.c` is being compiled (not `lcd_028.c`) — confirm `-DBOARD_096` is set.

---

### 0.96" LCD: backlight dim or off

BL is GP25 on the 0.96" board. Confirm `LCD_PIN_BL 25` in `lcd.h` under `#ifdef BOARD_096` and that the PWM slice is being configured correctly for GP25 (slice 12, channel B on RP2350).

---

## Build Issues

### Docker image not found

```
make docker-build
```

---

### pico-sdk not found

Ensure `../pico-sdk` exists relative to the project directory, or pass `-DPICO_SDK_PATH=` to cmake. The Makefile mounts `$(PICO_SDK)` which defaults to `../pico-sdk`.

---

### PIO header not generated (undefined `pdm_audio_program` etc.)

Ensure `pico_generate_pio_header()` is present in `CMakeLists.txt` for the selected backend. A `make clean` followed by a fresh build resolves stale cmake cache issues.

---

## Checking Device Enumeration

```bash
# macOS
system_profiler SPUSBDataType | grep -A 10 "RP2350"

# Linux
lsusb -v -d 2e8a:fedd

# Windows
# Device Manager → Sound, video and game controllers → RP2350 USB Audio DAC
```

Expected: `bcdUSB 1.10`, `idVendor 0x2e8a`, `idProduct 0xfedd`, two interfaces (AudioControl + AudioStreaming).
