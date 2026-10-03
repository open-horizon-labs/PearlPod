# Verification

ESP-IDF v5.5.5 builds and packages the complete image. Esptool verifies written data hashes. Host checks use AddressSanitizer and UndefinedBehaviorSanitizer for directory indexing, natural ordering, album boundaries, artwork payload parsing and button/volume behavior. Python checks verify RGB565 preparation and missing-card errors. Real two-second 44.1 kHz stereo MP3, FLAC and WAV fixtures decoded to expected frame counts with the vendored decoders.

Device checks exercised card scanning, DAC identification, playback progression, pause/resume, same-track restart, next/previous, persisted volume and selected-track recovery. UI ready was approximately 0.8 seconds after application timer origin; library ready approximately 0.9 seconds on the current card. This is not a power-on stopwatch measurement. The user confirmed rendering, touchscreen track selection, headphone audio, volume buttons and long-hold sleep/wake.

An independent code review found a large-album coordinate overflow. Pagination now limits each track page to 32 rows and retains absolute track selection; the reviewer verified the correction. The final build includes this fix and the asynchronous artwork decoder. No physical large-library or sustained artwork stress test has been performed, and album-art appearance on a populated cover library still needs observation.

Recovery is the saved full factory image. Hardware pin and DAC uncertainties were retired through source, original binary inspection and device checks. Remaining limitations: unmeasured standby draw, fixed 16-bit/48 kHz audio pipeline, limited CJK glyph coverage, and Listener’s own preference/usability judgment. Her voluntary independent use remains the outcome to observe.
