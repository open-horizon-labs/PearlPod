# Typed media qualification — 2026-10-25

Code/build revision: `3c90f41bf34f75d19545830b35df7ef326468fec` (clean `v0.3.1-21-g3c90f41`). Private shared code pin: `bdeeeda6b27f2d513601aba2fb4c45ec54485237`. ESP-IDF v5.5.5 with the repository's verified SDK SPI ISR patch. No hardware was connected, flashed or played audibly.

| Check | Result |
| --- | --- |
| `tools/check.sh` | PASS: 12 sanitizer-enabled native executables, portal JS, 2 preparation and 3 recovery Python cases, production standby orchestration. Includes existing 2,607-track music fixture and new typed overlay, split-book timeline, per-item durable completion, orphan exclusion, malformed/backup catalogs and injected overlay allocation failures. |
| `tools/check_media.sh` | PASS: actual production selection/seek/command routines; actual Pearl transport adapter with deterministic HTTP seams plus real shared coordinator and threaded durable writer. Interruption resumes with Range/If-Range; pending 503 never reports unsupported; only 404 permits fallback; SHA/ETag, cancellation, cache validation, owned corrupt-file repair, repeated foreign-file protection, decoder-active publication guard and catalog-last activation pass. |
| Silent decoder seek | PASS: 4 actual minimp3 callback/index cases (mono/stereo × CBR/VBR). PCM at 1.5 seconds and restart matches the full silent decode exactly. |
| `tools/check_captive.sh` | PASS: captive A/AAAA/HTTPS, malformed/compressed/truncated packets and 10,000 malformed inputs; open setup/hostile SSID portal regression. |
| `tools/check_codecs.sh` | PASS: 56 FLAC/WAV reference/decode/damage cases across 44.1/48/96/192 kHz and 16/24-bit sources. |
| `tools/check_sync.sh` | PASS: actual SDK SPI ISR regression, health-aware discovery, short TCP writes/backpressure/disconnect, journal/reset recovery, progress/ETA, asynchronous writer and lyrics. |
| `tools/render_ui.sh /tmp/pearl-media-ui` | PASS: 33 production LVGL scenes plus actual gesture/control assertions. Includes populated books/shows/episodes, paused/finished spoken playback and chapters. Neutral representative captures are in `docs/ui/media/`; inspected chapter heading, spoken controls and episode title layout. |
| `tools/build.sh` | PASS: application and merged ESP32-S3 images. App partition 70% free. |

The HTTP native seam controls headers/body interruptions; it is not a live ESP network test. The library service independently tests its real HTTP catalog/object endpoints and validates its mixed catalog through this pinned C parser. Physical headphone listening, actual SD timing, power-cut durability and device usability still need later hardware qualification. MP3 preparation is required for spoken content; no AAC/M4B decoder support is claimed. Large typed catalogs can exceed available RAM; candidate failure keeps the prior in-memory library, without truncation. Retained generations consume card space until household cleanup. See [behavior and boundaries](MEDIA-LIBRARY.md).

## Local artifacts

Artifacts remain ignored/local under `dist/`; they were not published as a release.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `pearl-player.bin` | 1,899,616 | `6033403a4f3338aa40099eb5f4aa19c0f98062fdcd8dfa702729d4a514c37bae` |
| `pearl-player-merged.bin` | 1,965,152 | `fe7ad834c9b8f1f943dfc312262a34e694bc01e780625a1e7edaeb2b03c2f4bb` |
| `bootloader.bin` | 19,936 | `5375ba743ed0d7edf134e8450ebdec7a437d32d5a9bf14c060814df85fc5a202` |
| `partition-table.bin` | 3,072 | `fabcbebf733457e9706b46544b2a1cb9672a8e1ce626f5dc7d7e6fb4738d5b0e` |

This evidence-only documentation commit follows the clean code build; its own hash is not embedded into these artifacts.
