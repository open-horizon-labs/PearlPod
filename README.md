# PearlPod

Offline anime-themed music firmware for Listener’s CS43131 FakePod Nano. The installed player supports MP3, FLAC and WAV, nested microSD albums, touchscreen playback, artwork and physical volume controls. Music playback stays offline; WiFi setup is an explicit action. Sync and playlist management are deferred.

## Everyday controls

Tap Browse, choose a collection, then tap a track. The header’s library button opens Albums, Artists, Folders, Playlists, Rescan card and WiFi. Playing shows the current track and artwork; use previous, pause/play and next on screen. Browse albums by their cover thumbnails; swipe vertically to scroll. Swipe left for the next album or track page, and right from tracks or Now Playing to return to albums. Previous/next page buttons remain available in the header. Larger albums have page controls. Your place is remembered when you return. Previous/next stay within the selected collection, including a playlist’s exact order.

Short press the power/BOOT button (GPIO0) to lower volume, or the screen-lock button (GPIO48) to raise it. Hold the power/BOOT button for about two seconds to stop playback and sleep; release, then hold again to wake. Hold the screen-lock button to turn the display off/on while music continues. Both holds darken the screen, but only the power/BOOT button sleeps playback. Sleep uses ESP32 deep sleep on this board’s RTC-capable GPIO0; it does not disconnect the battery. Standby draw is not measured. The screen also sleeps after 45 seconds without interaction; tap to wake it without selecting a track. When playback is paused and idle for five minutes, the player sleeps automatically unless WiFi, library work or a USB host is active.

Volume and the selected track survive restart. Resume is paused at the beginning of the remembered track.

## Music and artwork

Put music under the card’s `music/` directory. Albums group by album and album-artist tags, with artist and directory fallbacks. Artists and Folders provide other ways to find music. MP3 ID3v1/v2.2/v2.3/v2.4, native/Ogg FLAC comments and WAV INFO supply tags on the device; preparation is optional. Disc/track tags order an album before natural filename sorting (`2` precedes `10`).

There are no fixed 255-folder/track, 2,048-track or 12-level limits. Indexes grow in PSRAM while reserving memory for playback. Paths must fit within 511 UTF-8 bytes. If indexing runs out of memory, scanning fails explicitly; rescanning retains the previous index. Skipped paths and playlist entries produce a visible count. Browsing never times out, and long names scroll.

Put M3U/M3U8 or XSPF playlists anywhere inside `music/`. Relative paths resolve beside the playlist; `/music/...` card paths and local `file:///...` URIs also work. Order and intentional duplicates are preserved. Network URLs, paths outside the music root, XML external entities and `xml:base` are not imported. Add files, then use Rescan card; playback pauses and the complete collection is freshly sorted.

Use `cover.jpg`, `cover.png`, `folder.jpg` or `folder.png` beside the audio, or embedded MP3/FLAC artwork. Images are limited to 400 KiB and 1,024 pixels per dimension; unavailable or unsupported art uses the illustrated fallback. Optional preparation produces a small 240×240 RGB565 cover and title sidecars without changing audio files:

```sh
.venv/bin/python tools/prepare_card.py /Volumes/YOUR_CARD
```

The audio pipeline decodes to 16-bit stereo and resamples to 48 kHz, including FLAC/WAV. It is not native-resolution hi-res playback. UTF-8 filenames are supported, but the supplied UI font does not cover every CJK character.

## Update over USB

Clone this private repository and run `tools/update.sh /dev/cu.usbmodemDEVICE`. It fetches the private GitHub release if local firmware is absent, verifies checksums, installs a pinned esptool in a local virtual environment if necessary, and flashes separate regions to preserve preferences. No ESP-IDF, Chinese phone number or OSHWhub download is needed. `tools/update.sh --check` verifies files without touching the device. Bluetooth/WiFi firmware OTA is not implemented.

## Build and flash

Install ESP-IDF v5.5.5, source its `export.sh`, then run:

```sh
tools/check.sh
tools/check_codecs.sh # Requires FFmpeg; checks real FLAC compatibility
tools/build.sh
tools/flash.sh /dev/cu.usbmodemDEVICE
```

Firmware binaries are [private release assets](https://github.com/muness/PearlPod/releases/tag/v0.1.0), excluded from Git history. Run `tools/fetch-firmware.sh` (requires GitHub CLI signed in with repository access) to download and verify them. `dist/pearl-player-merged.bin` is the complete image for flash offset zero. `dist/SHA256SUMS` identifies the packaged artifacts. Button GPIOs and the volume cap are configurable through menuconfig. This build has been tested on the CS43131 unit; the PCM5102 variant is not verified.

A full original flash backup is kept locally at `backups/factory-020000000001.bin`. To restore with ESP-IDF’s Python environment:

```sh
python -m esptool --chip esp32s3 --port /dev/cu.usbmodemDEVICE write_flash 0 backups/factory-020000000001.bin
```

See [hardware evidence](docs/HARDWARE.md), [verification](docs/VERIFICATION.md) and [design](DESIGN.md). Published upstream source was a display/touch demo; this repository implements the player and preserves the original demo under `docs/`.

For local UI verification after IDF has fetched managed components, run `tools/render_ui.sh /tmp/pearl-ui-check`. It builds the actual LVGL screen, exercises pointer gestures and produces 460×460 PNG captures. Checked-in previews under `docs/ui/after` use fixture album/track names.

## WiFi setup

Open Browse → the header library button → WiFi → Set up WiFi. Join the temporary PearlPod network using the password shown on the player, then open http://192.168.4.1. Find networks or enter a hidden network name, enter its password, and save. The page reports connection progress. PearlPod remembers up to four networks in device storage; passwords never appear in diagnostic output. Use Connect for diagnostics to select the strongest nearby saved network, or Turn WiFi off when finished. Setup stays available during a failed connection until its five-minute timeout. Diagnostic connections last fifteen minutes; their read-only `http://<player-IP>/status` endpoint reports playback, power, memory and connection health. Sessions shut the radio down automatically when they expire. WiFi is always off at boot and is stopped before standby. Only 2.4 GHz networks are supported.

No music synchronization or playlist-management service is included yet. Existing local playlist-file playback remains available. See [WiFi design and verification](docs/WIFI-EXECUTION.md) for borrowed patterns, limits and evidence.

WiFi hardware status: the first image exposed a task-stack overflow. USB-window recovery restored device-tested c9e6efd, confirmed by the user and playback diagnostics. The current local firmware adds bounded WiFi sessions, crash-delayed networking, deep sleep and a revised RAM budget. Hardware evidence and remaining checks are in [power verification](docs/POWER-EXECUTION.md). See [verification](docs/VERIFICATION.md).

For a boot loop, run `tools/recover.sh`: it automatically retries up to twenty times and restores the bundled last device-verified application, preserving preferences. See [recovery instructions and observed evidence](docs/RECOVERY.md).

## WiFi credentials on microSD

Copy [the example](examples/wifi.toml.example) to `wifi.toml` at the card root, beside the `music/` folder. Edit it with one to four `[[networks]]` entries, then insert the card and start the player. Use `ssid` and `password` strings for each network; `password = ""` explicitly selects an open network. TOML literal strings in single quotes are useful for passwords containing backslashes.

```toml
config_version = 1

[[networks]]
ssid = "Home"
password = "Your password here"

[[networks]]
ssid = "Another network"
password = 'Another password here'
hidden = true
```

The complete valid file replaces the saved network list in one storage operation. The player removes `wifi.toml` only after a successful import; failed parsing or storage preserves the file and previous networks. If removal fails, it reports that the imported file remains. WiFi stays off at boot; open WiFi → Connect saved network when needed. A single `[wlan]` section is also accepted in place of the repeated entries, using the familiar Pi field names; this is a PearlPod schema, not a general Raspberry Pi configuration importer. Encrypted Pi passwords and unrelated Pi settings are unsupported. The file is limited to 8 KiB, SSIDs to 32 UTF-8 bytes, and passphrases to 8–63 bytes or empty. Malformed, duplicate or excess entries reject the whole import. Never commit a real credentials file.

The current device is on the recovered working firmware; microSD import is included in the locally built candidate package. See [fact-check and implementation evidence](docs/CARD-WIFI.md).
