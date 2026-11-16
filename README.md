# PocketCentury

Android emulator for the **100-in-1 Retro Gaming Console (SY-909)**, a cheap 2.4" handheld
(MAME set `100in1rg`).

## Hardware (as worked out from the flash dumps)

- **CPU:** GeneralPlus SoC with a u'nSP 2.0 core. Its register set matches the GPCE4 family.
- **SPI flash 1** (`by25q80awt.bin`, 1 MB): a "GP3X" header, the boot loader, and the game code.
  Everything from byte `0x3000` on is encrypted with the same byte scheme MAME uses for `siddr`,
  but with key `0x4a`. It's mapped directly at word address `0x200000`.
- **SPI flash 2** (`p25d32.bin`, 4 MB): graphics and audio data. Read through the second SPI
  controller (`0x3090`), with chip select on port B bit 5.
- **LCD:** 320x240 MIPI-DBI controller on a 16-bit 8080 bus.
  Data on port A, WR = port B bit 9, RD = bit 8, D/C = bit 11, RESET = bit 10, CS = bit 12.
  The game renders in software and streams pixels to the panel.
- **Keys:** port A bits 7-15, active high, shared with the LCD bus.
- **Audio:** Timer B FIQ at about 9375 Hz. It mixes two software ADPCM channels into DAC channel 1.
- **Interrupts:** Timer C = IRQ2 (backlight PWM). INT2 time-base bits 0x01 and 0x04 = IRQ7.
  0x04 paces rendering, and its rate is a best guess of 256 Hz.

## Internal ROM: emulated at a high level

The SoC's mask ROM has never been dumped. The game code only depends on a small part of it,
and PocketCentury replaces that part:

| Internal ROM item | Replacement |
|---|---|
| Boot loader | Copies the boot block to RAM, sets SP from the header and jumps to the entry point |
| Interrupt vectors | Trampolines that push r3/r4 and jump through the GP3X header's vector table |
| `0x63ae` | Reads N words from flash into RAM (used to load RAM overlays) |
| `0x637e`, `0x6390` | Cache / SPI mode helpers, treated as no-ops |
| `0x639d`, `0x560a` | Never reached during testing. If they are hit, they're logged and return immediately |
| ADPCM delta table at `0x48d3` | **Reconstructed** as `nibble * 2^(row/2 - 4)`, fitted to the game's own audio data. Sound may not match the real hardware exactly |

## Speed

MAME's u'nSP cycle table is for the much slower 1.x cores. The emulated CPU runs 6 table-cycles
per real 96 MHz cycle, which lets the game hit its 256 Hz render pacing. Three accelerations keep
it fast enough for phones:

- Native fast paths for the hottest loops: pixel push, memcpy and memset. They check the exact
  opcodes first.
- Skipping straight to the next interrupt when the scheduler is idle.
- Running the interrupt sources on their own clock.

## Using it

1. Build the APK with GitHub Actions (artifact `PocketCentury-apk`).
2. Tap **Import ROM files**, then pick the MAME `100in1rg.zip` or both `.bin` dumps.
3. Tap **Play**. Back opens the menu: save/load state, fast-forward, reset.

Keys (from testing): D-pad, A = confirm, START. B, SELECT and C are the three remaining
hardware keys, and their real labels aren't confirmed yet.

## Desktop tools

- `desktop/probe`: headless runner with screenshots, scripted input, WAV capture and
  PC histogram. Build it with `make -C desktop`.
- `tools/dis.cpp`: u'nSP 2.0 disassembler.
- `tools/decrypt.py`: decrypts the SPI1 image.

## License

GPL-2.0-or-later. The u'nSP core and disassembler come from MAME and are GPL-2.0+.
Copyright Segher Boessenkool, Ryan Holtz and David Haywood.
