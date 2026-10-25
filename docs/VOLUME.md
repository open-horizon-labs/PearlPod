# Volume and DAC output

Volume 0 is mute and 100 is unity PCM gain with both CS43131 PCM volume registers at 0 dB. The old configurable ceiling of65 is removed. No digital amplification above unity is used: full-scale input peaks fit the signed32-bit I2S slots without wrapping or clipping from a gain boost. A logarithmic -80-to-0 dB software curve keeps quiet settings approximately at their previous acoustic level after removing the fixed DAC attenuation. Saved volume numbers remain unchanged; startup remains paused.

The default CS43131 output is its1.41 Vrms full-scale range with high-voltage mode disabled and adaptive Class H supply control. This is the highest range the datasheet supports across the full headphone load range. Actual delivered voltage/power depends on load and the board supply; this change has not been measured on hardware.

The chip also offers2 Vrms for headphones of at least600 ohms with VP at least3.3 V. That mode is available explicitly through menuconfig → PearlPod → “2 Vrms output for headphones of at least600 ohms” (`CONFIG_PEARL_HIGH_IMPEDANCE_OUTPUT`). It sets OUT_FS=11, +1dB_EN=1 and HV_EN=1 before headphone power-up. It is not the default for ordinary headphones, whose load is outside that mode's specified range. This mode is software-tested, not physically qualified on this board.

Evidence: [Cirrus CS43131 datasheet DS1155F2](https://statics.cirrus.com/pubs/proDatasheet/CS43131_DS1155F2.pdf), sections4.2.1 and4.3 (load/supply restrictions),7.5.1 (OUT_FS and +1dB_EN),7.5.3–4 (0.5 dB PCM attenuation) and7.5.10 (Class H/HV_EN). The PCM5102 fallback has no software-controlled analog gain; unity PCM is its available firmware maximum. That board variant remains unverified.

Previously maximum65 applied -11.25 dB software gain, an additional -16 dB in the DAC, and the1 V output range. Changing only the numeric ceiling to100 would apply +15 dB digital gain and overflow peak I2S samples; the gain law and DAC settings must change together.

`tests/test_volume.py` compiles and executes the actual DAC initialization and PCM emission with silent register/I2S seams under ASan/UBSan. Both DAC modes, power-up ordering, unity registers, all101 volume levels, mute, monotonic gain and signed full-scale peaks at44.1/48/96/192 kHz pass. The production command test verifies65 can reach100 and clamps at100/0. No audio was played and no firmware was flashed for these checks.
