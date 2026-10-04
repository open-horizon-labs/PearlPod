# PearlPod

PearlPod turns the **CS43131 FakePod Nano** into an offline music player: albums, artwork, playlists and lyrics on microSD, with touchscreen playback and two volume buttons. It is part of [HiPhi.audio](https://hiphi.audio/pearlpod.html).

**Alpha firmware, tested on one physical CS43131 unit.** The PCM5102 variant is unverified. Confirm the DAC variant with the seller before buying.

[Flash in your browser](https://hiphi.audio/flash/pearlpod/) · [Downloads and release notes](https://github.com/open-horizon-labs/pearlpod-releases/releases) · [Order hardware](https://www.tindie.com/products/johnson/fakepod-nano-cnc-aluminum-amoled-audio-player/)

## Start listening

1. Install with desktop Chrome or Edge and a USB data cable using the flash page above.
2. Put MP3, FLAC or WAV files under `music/` on microSD. Ordinary folders such as `music/Artist/Album/01 - Title.mp3` work.
3. Insert the card, open Browse, choose an album and tap a song. After changing files manually, choose **Rescan card** from the library menu.

Boot loads a saved library index. A missing or invalid index triggers a scan; a changed sync rebuilds it. See [index behavior](docs/LIBRARY-INDEX.md).

## Controls

Browse by Albums, Artists, Folders or Playlists. Swipe vertically to scroll, left for the next page, or right from tracks and Now Playing to return to albums. Page buttons are also available. Previous and next follow the selected collection’s order.

| Button | Short press | Hold about two seconds |
| --- | --- | --- |
| Power / BOOT (GPIO0) | Volume down | Stop playback and sleep; release, then hold again to wake |
| Screen lock (GPIO48) | Volume up | Turn the display off or on while music continues |

Volume and the selected track survive restart. Playback resumes paused at the beginning of that track.

The screen sleeps after 45 seconds without interaction; tap to wake without selecting a song. Paused playback sleeps after five idle minutes. An active USB serial host blocks both automatic sleep paths; WiFi and library work also block player sleep. Deep sleep does not disconnect the battery, and standby draw has not been measured.

## Artwork, playlists and lyrics

Use embedded MP3/FLAC artwork or a neighboring `cover.jpg`, `cover.png`, `folder.jpg` or `folder.png`. Supported images are at most 400 KiB and 1,024 pixels per dimension. Missing artwork uses the neutral fallback.

Put M3U, M3U8 or XSPF playlists inside `music/`. Relative paths resolve beside the playlist; order and intentional duplicates are preserved. Network URLs and paths outside the music root are skipped. For lyrics, put an LRC file beside its song with the same filename stem. Open **Lyrics** from Now Playing when lyrics are present. Timed lyric appearance still needs a physical visual check.

Album and artist tags organize the library; disc and track tags set album order, followed by natural filename sorting. UTF-8 paths are supported up to 511 bytes, but the UI font does not cover every CJK character. Library capacity depends on available memory. A failed rescan retains the previous index and reports skipped entries.

Audio is decoded to **16-bit stereo at 48 kHz**, including FLAC and WAV; higher-resolution sources are resampled. Optional [card preparation](tools/prepare_card.py) makes smaller covers and title sidecars without changing audio files.

## WiFi and Plex sync

WiFi stays off at startup. Open Browse → the library menu → WiFi → **Set up WiFi**, then join the open setup network shown on the player. The captive page should open automatically; otherwise visit `http://192.168.4.1`. Select your home network, enter its password and save. PearlPod remembers up to four 2.4 GHz networks.

You can also import credentials from a root-level `wifi.toml` using [the example](examples/wifi.toml.example). A successful import replaces the saved network list and removes the file; a failed import preserves both. See [file format and limits](docs/CARD-WIFI.md).

**Sync now** asks a separately configured home runner to export the selected Plex profile’s `PP:` playlists. The prefix is stripped on the card. The runner converts existing library files with FFmpeg, embeds artwork, carries over supplied lyrics and transfers files through lftp. Songs shared by playlists are stored once. The player shows the playlist, current song, progress and estimated time remaining. Completed playlists are checkpointed individually and reused after an interruption. See [runner setup](syncer/README.md).

Setup, diagnostics and sync have no PearlPod login. Use them on your trusted home network. **Connect for diagnostics** exposes a read-only `/status` endpoint at the player’s LAN address. Setup expires after five inactive minutes and diagnostics after fifteen minutes; the radio turns off when the session ends. See [WiFi verification](docs/WIFI-EXECUTION.md) and [sync evidence](docs/SYNC-IMPLEMENTATION.md).

## Personalize the player

Choose [Midnight, Sakura or Sunburst](https://github.com/open-horizon-labs/pearlpod-releases/tree/main/theme-packs). These packs change colors and greetings. Edit the name in `Person.toml` and restart. Custom packs can also supply welcome and farewell pictures and phrases; see [installation and format](docs/THEME-PACKS.md). This repository’s `theme-packs/Default` contains synthetic artwork for tests.

## Update and recover

From a clone of this repository, run:

```sh
tools/update.sh /dev/cu.usbmodemDEVICE
```

The updater downloads released firmware when needed, verifies checksums and preserves preferences. It installs a pinned esptool in a local virtual environment if necessary. `tools/update.sh --check` checks the package without flashing. Wireless firmware updates are not implemented.

For a boot loop, use `tools/recover.sh`. It retries up to twenty times to catch the USB connection and restores the last device-verified application while preserving preferences. It refuses ambiguous USB targets unless you specify a port. See [recovery instructions](docs/RECOVERY.md). Keep a local factory-flash backup before replacing the original firmware.

## Build locally

Install ESP-IDF **v5.5.5** and source its `export.sh`. Host checks also need a C compiler, Node.js and a `.venv` Python environment with Pillow; codec checks need FFmpeg.

```sh
python3 -m venv .venv
.venv/bin/pip install Pillow
tools/check.sh
tools/check_codecs.sh
tools/build.sh
tools/flash.sh /dev/cu.usbmodemDEVICE
```

Binaries belong in [GitHub releases](https://github.com/open-horizon-labs/pearlpod-releases/releases). `tools/fetch-firmware.sh` downloads them with GitHub CLI. The merged image flashes at offset zero; packaged checksums identify each artifact. Button GPIOs and the volume cap are configurable through menuconfig.

See [hardware evidence](docs/HARDWARE.md), [verification](docs/VERIFICATION.md) and [design](DESIGN.md). `tools/render_ui.sh /tmp/pearl-ui-check` renders the production LVGL interface with fixture music after IDF has fetched managed components.

## License

Firmware source is available under [PolyForm Noncommercial 1.0.0](LICENSE), free for noncommercial use. [Contact Open Horizon Labs](https://hiphi.audio/bespoke.html) about commercial use. Dependencies retain their own terms; see [third-party licenses](THIRD_PARTY.md).
