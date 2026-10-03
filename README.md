# Listener Player

Offline anime-themed music firmware for Listener’s CS43131 FakePod Nano. The installed player supports MP3, FLAC and WAV, nested microSD albums, touchscreen playback, artwork and physical volume controls. WiFi is deferred.

## Everyday controls

Tap Albums, choose a directory, then tap a track. Playing shows the current track and artwork; use previous, pause/play and next on screen. Larger albums have page controls. Previous/next stay within the selected album.

Short press volume-up or volume-down to change volume. Hold volume-up for about two seconds to stop playback and sleep; release, then hold again to wake. Hold volume-down to turn the display off/on while music continues. Both holds darken the screen, but only volume-up sleeps playback. Sleep uses ESP32 light sleep rather than cutting battery power; standby draw is not measured.

Volume and the selected track survive restart. Resume is paused at the beginning of the remembered track.

## Music and artwork

Put music under the card’s `music/` directory. Each directory containing audio becomes an album; nested directories work. Limits are 256 albums, 2,048 tracks and 12 directory levels. Tracks sort naturally by filename, so `2` precedes `10`.

Use `cover.jpg`, `cover.png`, `folder.jpg` or `folder.png` beside the audio, or embedded MP3/FLAC artwork. Images are limited to 400 KiB and 1,024 pixels per dimension; unavailable or unsupported art uses the illustrated fallback. Optional preparation produces a small 240×240 RGB565 cover and title sidecars without changing audio files:

```sh
.venv/bin/python tools/prepare_card.py /Volumes/YOUR_CARD
```

The audio pipeline decodes to 16-bit stereo and resamples to 48 kHz, including FLAC/WAV. It is not native-resolution hi-res playback. UTF-8 filenames are supported, but the supplied UI font does not cover every CJK character.

## Build and flash

Install ESP-IDF v5.5.5, source its `export.sh`, then run:

```sh
tools/check.sh
tools/build.sh
tools/flash.sh /dev/cu.usbmodemDEVICE
```

`dist/pearl-player-merged.bin` is the complete image for flash offset zero. `dist/SHA256SUMS` identifies the packaged artifacts. Button GPIOs and the volume cap are configurable through menuconfig. This build has been tested on the CS43131 unit; the PCM5102 variant is not verified.

A full original flash backup is kept locally at `backups/factory-020000000001.bin`. To restore with ESP-IDF’s Python environment:

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodemDEVICE write_flash 0 backups/factory-020000000001.bin
```

See [hardware evidence](docs/HARDWARE.md), [verification](docs/VERIFICATION.md) and [design](DESIGN.md). Published upstream source was a display/touch demo; this repository implements the player and preserves the original demo under `docs/`.
