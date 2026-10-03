# Sync implementation and acceptance evidence

The local candidate uses existing NAS music, ffmpeg preparation and lftp delivery. It does not generate replacement music or modify source files. The local sync candidate was flashed app-only to the verified PearlPod USB identity, with esptool hash verification. The player boots, preserves all 54 manually copied tracks and preferences, decodes existing FLAC without error and handles pause. The physical sync acceptance is currently blocked by absent saved WiFi credentials, rather than USB availability. The actual setup AP starts and scans eight visible networks without a reset. During AP setup the USB memory check reports 26,523 free internal bytes, 25,836 minimum internal bytes and an 18,432-byte largest internal block; these are AP measurements, not a full-sync memory budget. A host socket harness is useful evidence for the shared FTP code, not a substitute for the device.

## Executed flow

The host authenticates to Plex as `selected-profile`, selects audio playlists with the `PP:` prefix and strips that prefix on the player. Exact media-part paths map from `/share/Media/Music` to the read-only `/Volumes/MUSIC` mount. The selected real playlist is `PP: PearlPod Sync Test` (12345), with Beautiful Trauma and What About Us by P!nk. The Plex creation API removed an attempted duplicate; the exporter preserves whatever ordered entries Plex actually returns.

A background publisher checks Plex every minute, prepares distinct selected tracks and publishes a completed catalog atomically on the host. Lossless sources become 256 kbps, 48 kHz stereo MP3 through ffmpeg; MP3 sources use stream copy. ID3v2.3 identity, genre and available year are retained. Album covers are resized and embedded, with separate cover objects as well. Source sidecars and embedded lyrics are carried when available, with supported timed sources normalized to LRC. No lyrics are invented or downloaded from external lyric services.

Paused playback → Your library → Sync now connects saved WiFi, discovers `_pearlpod-sync._tcp` (or uses the configured console fallback), opens a bounded anonymous FTP session on 2121/PASV 2024, then posts `/sync` to the host. The host uploads through lftp and writes the catalog and ready marker last. The Pod verifies object hashes, sizes and a complete candidate library before committing active/previous catalog hashes in one NVS blob. Only activated managed entries join manually copied music. Staged and retired objects stay hidden. Networking is absent from boot and shuts down after sync.

## Verification completed locally

- Real NAS preparation: two FLAC sources converted by ffmpeg, with embedded JPEG artwork and carried lyrics. NAS originals remained untouched.
- Whole host flow: the service prepared those sources, accepted HTTP `/sync` with 202, and completed lftp upload to a socket harness built from the same native FTP source used by firmware. Zero available space produces `card_full` before audio upload; malformed trigger bodies reject; repeat sync leaves audio timestamps untouched. This verifies host orchestration and shared protocol code; it is not an on-device transfer.
- Actual firmware metadata and artwork C code parses the real prepared MP3s under AddressSanitizer/UBSan. The test no longer generates an audio tone.
- Shared native FTP code: unchanged-object upload skipped; truncated/interrupted file retried; simulated write-capacity failure published no ready marker; pre-existing files retained; path traversal rejected. These are host fault injections rather than real FAT/card failures.
- Actual managed loader/library C code: real prepared catalog yields two tracks, one album, one ordered two-entry playlist and two lyric associations. NVS commit failure leaves old activation intact; malformed/deep catalog rejects; successful subsequent activation retains the previous hash. Cleanup preserves active/previous references and manual files, removes retired objects only after they leave both selections, and fails closed on damaged retained catalogs. A valid empty selection activates without deleting manual files.
- Host mDNS advertisement is discoverable with port 8787 and FTP transport; discovery on the ESP32 remains a device check. Existing library, metadata, WiFi, power and recovery host checks pass. LVGL render/interaction checks pass including entering the lyric surface. ESP-IDF v5.5.5 local build passes.
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

Configure a saved 2.4 GHz network, then verify an actual Sync now, playlist order, artwork, lyrics and WiFi shutdown. The app-only flash and existing FLAC decoder/pause checks pass. Boot time, physical controls and audible lyric alignment still require observation on this build. Measure peak internal RAM and task stack during repeated sync and decoder/lyric timing against audible output. Test cancellation, an unavailable host, bad uploads, real card-full behavior and controlled power cuts before enabling unattended operation.

The current implementation retransfers interrupted whole files; it has no upload resume or block delta. Capacity preflight and conservative active/previous-reference cleanup are implemented and host-tested. The capacity reserve is conservative rather than a guarantee against concurrent card writes; real FAT cluster/capacity failure remains a device test. Small historical catalogs remain retained. Embedding means a tag/cover edit changes MP3 bytes and requires replacement upload; playlist/lyric-only edits preserve audio objects. Language variants are carried, but the UI selects the first variant. Japanese font coverage is incomplete. Timed lyrics follow the decoder clock, with physical DMA/MP3 alignment unmeasured; no word timing is claimed. Charging-only nightly wake remains gated on a truthful charging signal.

## Final execution checklist and review

The selected mechanism remains HTTP-triggered lftp into a temporary receiver. Scope is manual Plex sync, art and available lyrics. Charging automation, Roon ingestion and word-level karaoke remain outside this execution. The review verdict is **Adjust until physical acceptance**: host implementation and failure paths are checked; hardware results are not inferred from socket shims. There is no frame or authority change.

| Risk / acceptance check | Status and evidence | Tempting shortcut rejected |
|---|---|---|
| Correct source/profile and file identity | Retired locally by real Selected profile playlist and exact confined NAS paths | Admin title match or artist/title file guessing |
| Selection changes during preparation | Host test retains old publication when second source snapshot differs | Publish a mixture of edited selections |
| Unchanged files and partial retry | Shared FTP tests and real service no-op timestamps; whole partial file retry verified | Reupload all audio or append an incompatible full response |
| Card capacity and terminal failure | Missing-file budget test, actual inventory, zero-space service rejection and error marker | Delete active songs to make space or wait silently for 14 minutes |
| Retained/manual file protection | Actual C cleanup test covers previous-only references, retired object, non-managed files and damaged catalogs | Delete every file absent from the newest list |
| Empty selection | Host format-header publication and C empty-library activation pass | Treat any source error as an empty success |
| Lyric parsing and memory | Bounds/repeated stamps/bracket text/footer offsets/overflow tests; explicit PSRAM allocation and UI interaction pass | Whole text/cue buffers in internal RAM or discard bracketed lyric words |
| Boot, RAM, mDNS, audio and FAT cuts on actual Pod | Partially verified: real app flash, offline library and FLAC/pause checks pass; no saved WiFi blocks sync. Socket shims cannot retire network peak memory, audible alignment or FAT cuts | Claim hardware success from host tests |
| Container execution | Earlier image smoke checks passed; latest Docker API times out despite OrbStack reporting Running. No other workloads were restarted | Claim a successful final container rebuild |
| Nightly charging / Roon / word timing | Outside current manual Plex scope; truthful charging signal and exact Roon intake are not established | Add timer wake based on USB host presence or fuzzy Roon matching |

Run `.sync-venv/bin/python tools/check_sync_service.py --host <host-LAN-IP>` with the host service running for the real NAS HTTP/FTP acceptance check. This deliberately uses a temporary native FTP socket harness and makes no claim about a physical Pod. Tokens remain on the host. There is no generated music in the preparation checks.

## Sync feedback

Sync opens a dedicated 460×460 screen with PP: playlist guidance, a Start sync / Cancel sync action, elapsed time and byte counts from actual FTP writes. Once data arrives it reports time since the last write, and after 30 seconds without data it explicitly says it is waiting for the computer. No total or percentage is invented: transfer size is unknown on the device. Connection, discovery, waiting, verification, success, cancellation, WiFi loss, full card, server preparation/busy and timeout each have recovery copy. Existing managed catalog activation remains transactional.

Local firmware build and LVGL touch/render harness cover ready, waiting, receiving, failure and success screens. Native FTP sanitizer integration verifies received-byte counters and interrupted/full-card transfers. Physical screen and real WiFi sync acceptance remain separate from these checks.

## Transfer tracing and throughput corrections

`sync trace` reports connection/discovery/HTTP-trigger time, FTP-loop time and maximum, yield time, marker-check time, network-snapshot time, received bytes, read/empty-read counts, receive timing, write timing and maximum, final flush timing, FTP states and reply codes. These counters are cumulative microseconds and expose buffered-write stalls; they do not claim that `fwrite` bytes are durable before close. Host `/status` includes publication, inventory, staging and lftp wall times.

Corrections remove unconditional per-chunk sleep and gratuitous successful-send delays, make data sockets nonblocking with correct EOF handling, rate-limit completion-marker lookups, use a 32 KiB PSRAM stdio buffer, check close failure before returning transfer success, and turn off modem power saving during sync. A 64 KiB TCP receive window and expanded receive queues allow buffering during card stalls. The SDMMC host now uses 16-sector staging and a reusable 8 KiB internal DMA buffer: ESP-IDF v5.5.5 defaults unaligned/PSRAM transfers to one sector, repeatedly issuing card transactions. Card high-speed negotiation is enabled. Display draw buffers are reduced from two 24-row buffers to two 12-row buffers, reclaiming about 22 KiB of internal RAM.

Measured before the final memory-budget change: inventory improved from 8867 ms to 206 ms; one 32 KiB buffered write took 1995 ms before SD staging changes, versus 1139 ms on the next fresh-file run. These are individual stalls, not isolated card benchmarks. The latter run also logged WiFi allocation failure with a 1376-byte internal-memory minimum, prompting the display-buffer reduction. Final sustained throughput and repeated-session memory checks must be recorded from actual hardware; no predicted rate counts as evidence.

Final memory-budget run received 4,523,040 bytes by elapsed 61 s, after 29.800 s of WiFi connection and 0.280 s HTTP trigger time. At elapsed 76 s it had received 6,318,720 bytes. Internal free memory was about 26 KiB with a 21,708-byte minimum; no WiFi allocation failures were observed in this run. Maximum buffered-write duration was 372,347 us. FTP write-call time total was 4,371,934 us; receive-call time was 3,035,705 us, with 39,555 empty reads among 40,643 attempts. Most remaining transfer time is waiting for incoming data, not synchronous card-write work. A sender/over-the-air trace is still needed to attribute that wait; macOS packet capture was unavailable without elevated permissions. These measurements do not establish the hardware maximum.

Sync requests the maximum transmit-power setting accepted by ESP-IDF (`84` quarter-dBm units, mapped by the SDK to its 20 dBm tier), separate from modem-sleep disablement. `sync trace` reports before/after configured limits and both API return codes. This is a configured maximum, not measured RF output; the PHY limits and rate-dependent transmit power still apply. The build already configures a 20 dBm PHY maximum, so this may merely confirm an existing ceiling.

The final DMA-budget physical run completed successfully: 16,763,902 bytes received; cumulative FTP write-call time 13.188 s, receive-call time 7.551 s, flush/close time 1.313 s, worst write 0.890 s. This was a real NAS/Plex export, not generated music. An unchanged-library follow-up is required to check reuse and power-setting readback.
