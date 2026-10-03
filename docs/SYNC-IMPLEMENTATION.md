# Sync implementation and acceptance evidence

The local candidate uses existing NAS music, ffmpeg preparation and lftp delivery. It does not generate replacement music or modify source files. No firmware containing these changes has yet been flashed; USB enumeration was absent during the final verification. A host socket harness is useful evidence for the shared FTP code, not a substitute for the device.

## Executed flow

The host authenticates to Plex as `selected-profile`, selects audio playlists with the `PP:` prefix and strips that prefix on the player. Exact media-part paths map from `/share/Media/Music` to the read-only `/Volumes/MUSIC` mount. The selected real playlist is `PP: PearlPod Sync Test` (12345), with Beautiful Trauma and What About Us by P!nk. The Plex creation API removed an attempted duplicate; the exporter preserves whatever ordered entries Plex actually returns.

A background publisher checks Plex every minute, prepares distinct selected tracks and publishes a completed catalog atomically on the host. Lossless sources become 256 kbps, 48 kHz stereo MP3 through ffmpeg; MP3 sources use stream copy. ID3v2.3 identity, genre and available year are retained. Album covers are resized and embedded, with separate cover objects as well. Source sidecars and embedded lyrics are carried when available, with supported timed sources normalized to LRC. No lyrics are invented or downloaded from external lyric services.

Paused playback → Your library → Sync now connects saved WiFi, discovers `_pearlpod-sync._tcp` (or uses the configured console fallback), opens a bounded anonymous FTP session on 2121/PASV 2024, then posts `/sync` to the host. The host uploads through lftp and writes the catalog and ready marker last. The Pod verifies object hashes, sizes and a complete candidate library before committing active/previous catalog hashes in one NVS blob. Only activated managed entries join manually copied music. Staged and retired objects stay hidden. Networking is absent from boot and shuts down after sync.

## Verification completed locally

- Real NAS preparation: two FLAC sources converted by ffmpeg, with embedded JPEG artwork and carried lyrics. NAS originals remained untouched.
- Whole host flow: the service prepared those sources, accepted HTTP `/sync` with 202, and completed lftp upload to a socket harness built from the same native FTP source used by firmware. This verifies host orchestration and shared protocol code; it is not an on-device transfer.
- Actual firmware metadata and artwork C code parses the real prepared MP3s under AddressSanitizer/UBSan. The test no longer generates an audio tone.
- Shared native FTP code: unchanged-object upload skipped; truncated/interrupted file retried; simulated write-capacity failure published no ready marker; pre-existing files retained; path traversal rejected. These are host fault injections rather than real FAT/card failures.
- Actual managed loader/library C code: real prepared catalog yields two tracks, one album, one ordered two-entry playlist and two lyric associations. NVS commit failure leaves old activation intact; malformed/deep catalog rejects; successful subsequent activation retains the previous hash.
- Existing library, metadata, WiFi, power and recovery host checks pass. LVGL render/interaction checks pass including entering the lyric surface. ESP-IDF v5.5.5 local build passes.
- An earlier local Linux image built and passed Python import/lftp smoke checks. The final container rebuild stalled in the local Docker daemon and remains unverified. No NAS container or nightly automation has been deployed.

## Reproduce

With host dependencies installed and a real prepared cache present, run `tools/check_sync.sh`. It compiles the shared native FTP code, lyric parser and metadata/art parser with sanitizers. Without a local publication, the real-media check skips rather than creating music. Use `tools/check.sh` for existing regressions and `tools/render_ui.sh /tmp/pearl-ui-check` for LVGL interaction checks.

The macOS managed-loader harness uses CommonCrypto only in its test shim:

```sh
cc -DPEARL_MANAGED_HOST -DPEARL_MANAGED_ROOT='"/tmp/pearl-managed-tests/music/.pearl"' -Wno-deprecated-declarations -fsanitize=address,undefined -I tests/managed_host -I main -I "$IDF_PATH/components/json/cJSON" tests/managed_host/check.c main/managed.c main/library.c main/metadata.c main/playlist.c "$IDF_PATH/components/json/cJSON/cJSON.c" -o /tmp/pearl-managed-check
.sync-venv/bin/python tests/test_managed.py
```

This check currently expects the verified two-track local playlist. It copies links into its private `/tmp/pearl-managed-tests` directory and removes only that directory afterward. Tokens, caches, firmware binaries and local captures remain excluded from Git.

## Device acceptance still required

Reconnect USB, flash the application preserving preferences, then verify boot time, physical volume controls, existing playback, mDNS discovery, an actual Sync now, playlist order, artwork, lyrics and WiFi shutdown. Measure peak internal RAM and task stack during repeated sync and decoder/lyric timing against audible output. Test cancellation, an unavailable host, bad uploads, real card-full behavior and controlled power cuts before enabling unattended operation.

The current implementation retransfers interrupted whole files; it has no upload resume or block delta. Old objects are retained: free-space preflight and reference-safe garbage collection remain follow-ups for large unattended libraries. Embedding means a tag/cover edit changes MP3 bytes and requires replacement upload; playlist/lyric-only edits preserve audio objects. Language variants are carried, but the UI selects the first variant. Japanese font coverage is incomplete. Timed lyrics follow the decoder clock, with physical DMA/MP3 alignment unmeasured; no word timing is claimed. Charging-only nightly wake remains gated on a truthful charging signal.
