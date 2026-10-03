# Hardware evidence

Physical unit MAC 02:00:00:00:00:01, ESP32-S3 revision 0.2, embedded 8 MB PSRAM, 16 MB quad flash. Serial /dev/cu.usbmodemDEVICE. Read with esptool 4.12.0.

Factory full flash backup: backups/factory-020000000001.bin, 16777216 bytes, SHA256 2f2ec8ff739f0f6a111620a16195461349c3ca119452022c2bd75f45dd155097. This backup is deliberately gitignored.

Factory serial boot identifies CS43131 and SDHC 3840 MB. New firmware reads CS43131 ID 43 13 10 at I2C address 0x30. No DAC guess remains for this unit.

Upstream archive ESP32S3-IDF_AMOLED_LVGL-V8.zip from https://image.lceda.cn/oshwhub/project/attachments/ccd815066e144ae6b5677dd8359d2990.zip. Published project: https://oshwhub.com/planevina/fakepodnano-lossless-audioplayer. Archive contains a display/touch demo, not the shipped player application. Original main.c and sdkconfig are retained under docs/.

Display: AM200Q460460LK, 460×460 QSPI AMOLED, X gap 10. SPI2 CS8 CLK9 DATA10–13 RESET7. Touch CST820, SDA3 SCL2 RESET5 INT4. SDMMC CLK16 CMD17 D015 D114 D221 D318. I2S BCLK40 LRCLK38 DOUT39. CS43131 reset41. Upstream supplied pin map identifies button48 on CS43131 hardware and button47/mute48 on PCM5102 hardware.

Additional evidence: manufacturer's v1.3.2 CS43131 binary was segmented and disassembled using Espressif Xtensa objdump. Init function at virtual address 0x420187ec drives reset41; caller at 0x4201272f supplies 24.576 MHz and 48 kHz; setup configures ASP slave (0x40018 = 0x0c) and 32-bit slots. Cross-check: Cirrus CS43131 DS1155F2 §5.7 PCM pop-free power-up, §5.13, §7.6 interrupt readiness. https://statics.cirrus.com/pubs/proDatasheet/CS43131_DS1155F2.pdf

First physical runtime: UI ready at 790 ms after app timer origin, library ready at 909 ms, one album/two tracks. MP3 output advanced 2, 4, 7 seconds with no application error. These establish boot, card access, DAC configuration and decoding/DMA, not a listening or touch-validation claim.

Buttons currently configured GPIO0 up/power and GPIO48 down/lock. User verified touchscreen selection, visible rendering, audible 3.5 mm playback, both volume directions and the long-hold sleep/wake cycle. Both long holds darken the screen; down is display lock and up is playback sleep. Software shutdown currently uses light sleep with muted DAC and display off. Battery draw has not been measured.
