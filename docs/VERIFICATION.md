# Verification

ESP-IDF v5.5.5 builds and packages the complete image. Esptool verifies written data hashes. Host checks use AddressSanitizer and UndefinedBehaviorSanitizer for directory indexing, natural ordering, album boundaries, artwork payload parsing and button/volume behavior. Python checks verify RGB565 preparation and missing-card errors. Real two-second 44.1 kHz stereo MP3, FLAC and WAV fixtures decoded to expected frame counts with the vendored decoders.

Device checks exercised card scanning, DAC identification, playback progression, pause/resume, same-track restart, next/previous, persisted volume and selected-track recovery. UI ready was approximately 0.8 seconds after application timer origin; library ready approximately 0.9 seconds on the current card. This is not a power-on stopwatch measurement. The user confirmed rendering, touchscreen track selection, headphone audio, volume buttons and long-hold sleep/wake.

An independent code review found a large-album coordinate overflow. Pagination now limits each track page to 32 rows and retains absolute track selection; the reviewer verified the correction. The final build includes this fix and the asynchronous artwork decoder. No physical large-library or sustained artwork stress test has been performed, and album-art appearance on a populated cover library still needs observation.

Recovery is the saved full factory image. Hardware pin and DAC uncertainties were retired through source, original binary inspection and device checks. Remaining limitations: unmeasured standby draw, fixed 16-bit/48 kHz audio pipeline, limited CJK glyph coverage, and Listener’s own preference/usability judgment. Her voluntary independent use remains the outcome to observe.

UI refinement: local host harness compiles the actual LVGL implementation at 460×460 and decodes real artwork through the same loading function. Simulated pointer input verifies album selection, horizontal page/back swipes, suppression of track selection during vertical drag, restored scroll and central play/pause. A generation test rejects old-track artwork after a track change. Captures under docs/ui/after use fixture music names, not the physical card. The two bounded visual rounds inspected browse, track list, playing/paused and error layouts. Physical gesture feel on this iteration remains unverified.

The UI update was built locally and flashed through ESP-IDF's normal segmented flash path, preserving NVS. Esptool verified flash data hashes. No hosted builds or pull requests were added.

## WiFi addition — pending device recovery verification

October 3, 2026: ESP-IDF v5.5.5 local build, packaged firmware checksums, catalog/metadata/artwork ASan/UBSan tests, 2,607-track/305-album/2,300-track-folder fixture, WiFi selection/hysteresis tests, hostile-SSID DOM test, and actual LVGL host rendering pass. The first WiFi image flashed successfully with the standalone USB updater and preserved NVS, but the device subsequently boot-looped. The captured USB log reports stack overflow in `wifi`. The corrected worker frame is 656 bytes, scan completion 1,168 bytes and the largest network frame is below the enforced 2,048-byte warning limit. The corrected build and distribution checksums pass.

Automatic USB reset and connection failed while the device was looping. A manual BOOT-button USB reconnect was requested. The corrected image has not yet been successfully flashed; device startup, repeated physical scans, provisioning/connection, off/standby and playback after WiFi remain unverified. Do not treat host success as hardware recovery. `docs/WIFI-EXECUTION.md` records the exact failure, correction and remaining checks.

## Recovery confirmation and microSD provisioning candidate

October 3, 2026: a 50 ms USB-port polling script caught the player’s intermittent enumeration and successfully flashed the app from c9e6efd at 0x10000 using usb_reset. Esptool verified its data hash; the user confirmed the player flashed and worked. Subsequent USB diagnostics showed UI readiness at 802 ms and library readiness at 1,131 ms on the application timer, two albums/two tracks, and ready=1 with playback progressing. These are application-relative measurements, not elapsed time from pressing power.

The reusable recovery tool defaults to twenty attempts, restricts selection to this board’s USB serial identity, verifies the bundled image checksum and preserves NVS. Host tests check wrong-device exclusion, stopping after success, app-only writes and bounded behavior when no device exists. `--check` detects the recovered player. The actual successful recovery used the earlier script; a twenty-flash run was not needed.

The microSD provisioning candidate builds with ESP-IDF v5.5.5. ASan/UBSan tests cover real TOML parsing, multiple networks, Pi-style wlan, Unicode/quoting/BOM/CRLF, byte limits, duplicate/invalid/oversized/deeply nested input, unchanged outputs on failure, and commit-before-delete. The WiFi worker frame is 656 bytes and import wrapper 432 bytes; imported parser/wrapper source is also compiled with a 2,048-byte frame warning limit. The package is locally built and checksum checked. The device remains on recovered c9e6efd; on-card import, NVS persistence across real resets, physical connection and corrected WiFi runtime need device verification before claiming those paths work.

## Power-management revision

The revised local firmware boots and scans WiFi without the earlier stack overflow. App-only flash hashes were verified. Device evidence shows UI readiness around 0.81 seconds, library around 1.14 seconds, automatic screen sleep at 45 seconds, multiple setup/scan/off cycles with internal memory reclaimed, and uninterrupted playback progression while asleep and across radio cycles. [Power execution](POWER-EXECUTION.md) records exact RAM measurements, defaults, source patterns and remaining physical/network checks. Earlier pending notes above describe the recovery history rather than the currently installed build.
