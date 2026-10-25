# Volume and DAC output

Volume 0 is mute and 100 is unity PCM gain with both CS43131 PCM volume registers at 0 dB. The old configurable ceiling of 65 is removed. No digital amplification above unity is used: full-scale input peaks fit the signed 32-bit I2S slots without wrapping or clipping from a gain boost. A logarithmic -80-to-0 dB software curve keeps quiet settings approximately at their previous acoustic level after removing the fixed DAC attenuation. Saved volume numbers remain unchanged; startup remains paused.

The default CS43131 output is its 1.41 Vrms full-scale range with high-voltage mode disabled and adaptive Class H supply control. This is the highest range the datasheet supports across the full headphone load range. Actual delivered voltage/power depends on load and the board supply; this change has not been measured on hardware.

The chip also offers 2 Vrms for headphones of at least 600 ohms with VP at least 3.3 V. That mode is available explicitly through menuconfig → PearlPod → “2 Vrms output for headphones of at least 600 ohms” (`CONFIG_PEARL_HIGH_IMPEDANCE_OUTPUT`). It sets OUT_FS=11, +1dB_EN=1 and HV_EN=1 before headphone power-up. It is not the default for ordinary headphones, whose load is outside that mode's specified range. This mode is software-tested, not physically qualified on this board.

Evidence: [Cirrus CS43131 datasheet DS1155F2](https://statics.cirrus.com/pubs/proDatasheet/CS43131_DS1155F2.pdf), sections 4.2.1 and 4.3 (load/supply restrictions), 7.5.1 (OUT_FS and +1dB_EN), 7.5.3–4 (0.5 dB PCM attenuation) and 7.5.10 (Class H/HV_EN). The PCM5102 fallback has no software-controlled analog gain; unity PCM is its available firmware maximum. That board variant remains unverified.

Previously maximum 65 applied -11.25 dB software gain, an additional -16 dB in the DAC, and the1 V output range. Changing only the numeric ceiling to 100 would apply +15 dB digital gain and overflow peak I2S samples; the gain law and DAC settings must change together.

`tests/test_volume.py` compiles and executes the actual DAC initialization and PCM emission with silent register/I2S seams under ASan/UBSan. Both DAC modes, power-up ordering, unity registers, all 101 volume levels, mute, monotonic gain and signed full-scale peaks at 44.1/48/96/192 kHz pass. The production command test verifies 65 can reach 100 and clamps at 100/0. No audio was played and no firmware was flashed for these checks.

## Qualification — 2026-10-25

Clean firmware code revision: `5fe06e3` (`v0.3.1-23-g5fe06e3`), ESP-IDF v5.5.5 with the existing verified SPI ISR patch. `tools/check.sh` passes, including the new actual DAC/PCM tests under ASan/UBSan; `tests/test_media_commands.py` passes with explicit volume 65→100, upper clamp and mute assertions. Both DAC mode branches pass the silent native harness. The default 1.41 Vrms configuration passes the complete ESP32-S3 build and merged-image packaging; the optional high-impedance configuration has not been independently target-built or physically qualified.

| Local artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `dist/pearl-player.bin` | 1,899,648 | `38c33e31bfc7a44e938f732f5f8f2a9135c1b17c6346d69d62938e956dfff9dd` |
| `dist/pearl-player-merged.bin` | 1,965,184 | `15db5d0da073bec350a971f33c03bb0312a7807e4937f7e294e81747a4f2d43a` |

Artifacts stay local and ignored. This evidence-only documentation update follows the clean code build. No flash, audible output, release or deployment occurred.
