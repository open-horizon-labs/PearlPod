# Sync implementation and acceptance evidence

The local candidate uses existing NAS music, ffmpeg preparation and lftp delivery. It does not generate replacement music or modify source files. The local sync candidate was flashed app-only to the verified PearlPod USB identity, with esptool hash verification. The player boots, preserves all 54 manually copied tracks and preferences, decodes existing FLAC without error and handles pause. The physical sync acceptance is currently blocked by absent saved WiFi credentials, rather than USB availability. The actual setup AP starts and scans eight visible networks without a reset. During AP setup the USB memory check reports 26,523 free internal bytes, 25,836 minimum internal bytes and an 18,432-byte largest internal block; these are AP measurements, not a full-sync memory budget. A host socket harness is useful evidence for the shared FTP code, not a substitute for the device.

## Executed flow

The host authenticates to Plex as `selected-profile`, selects audio playlists with the `PP:` prefix and strips that prefix on the player. Exact media-part paths map from `/share/Media/Music` to the read-only `/Volumes/MUSIC` mount. The selected real playlist is `PP: PearlPod Sync Test` (12345), with Sample Track A and Sample Track B by Sample Artist. The Plex creation API removed an attempted duplicate; the exporter preserves whatever ordered entries Plex actually returns.

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

Physical power readback: `tx_before_qdbm=80 tx_after_qdbm=80 tx_result=0 ps_result=0` confirms the configured maximum was already 20 dBm and both explicit settings succeeded. Unchanged-library sync completed successfully with only 1784 received bytes (catalog/marker), zero music retransfers, and a rescan showing one managed album/two tracks. Transfer-stage elapsed was 7 seconds before verification; this does not include a separately timed verification duration.

## Bounded receive queue and AP selection

The FTP producer now copies into a 2 MiB PSRAM ring, and a separate SD worker owns the file, drains chunks up to 32 KiB and closes it before FTP returns 226. Full queues leave data in TCP for backpressure. The existing 8 KiB internal DMA staging and 16-sector SDMMC transactions remain in use. Cancellation discards pending data and closes the temporary object; worker errors fail the transfer. The queue exists only during sync and is freed on radio shutdown. Paused audio allocations remain intact because observed PSRAM headroom exceeds 6 MiB with the queue allocated.

Host ASan/UBSan integration and standalone wraparound/order/backpressure/abort/restart/write-error tests pass; the standalone writer also passed ThreadSanitizer. A probe using an existing NAS-exported MP3 transferred 6,788,976 bytes before a socket timeout; it did not complete readback verification and is not a successful benchmark. Its trace recorded 3.930 s of SD write time, a 0.241 s worst write, a 32,256-byte queue high-water mark and zero queue-full events. The temporary probe was confirmed absent afterward.

A full-album run exposed a console stack overflow while printing expanded diagnostics. The trace output buffer is now static and the console stack is 6 KiB. T-Dongle bridge.c association settings are ported explicitly: all-channel scan, signal sorting, radio measurement and BSS transition support, with CONFIG_ESP_WIFI_11KV_SUPPORT enabled. BSSID/channel/RSSI are included in sync traces. The previous default fast scan could join the first matching AP rather than the strongest AP for that SSID. The sync radio lease no longer expires while sync is active; transfers stop after three minutes without incoming bytes, or a two-hour total bound. The host delivery bound matches two hours.

On the October 3 full-album baseline, the weak channel-1 association delivered 1,491,232 bytes by 81.16 s (5.000 s connection), with RSSI around -82 to -85 dBm. The replacement joined BSSID 68:d7:9a:60:cc:84 on channel 6 at -68 dBm and delivered 8,494,566 bytes by 46.09 s (7.399 s connection). These are early cumulative observations, not completed-library throughput or maximum rates. Final run evidence follows when verification completes.

The larger library also exposed a pre-existing relative FTP path bug: removing the previous filename retained a trailing slash, and the next join added another slash. After enough objects, the path exceeded the managed-path bound. Joins now reuse an existing separator. A native receiver regression uploads 120 distinct relative SHA-named objects in one session and verifies every file plus the retained working directory.

The full-card regression exposed why earlier tests sometimes timed out: returning a temporary 451 for a confirmed storage failure allowed repeated lftp retries. Writer/close failures now return permanent 552; receive errors retain 451. The unchanged ten-second full-card deadline passes after this correction, along with the 120-object relative-path regression and the rest of the eight-test suite.

## Full albums measured October 3, 2026

The Selected profile PP: playlist expanded to 30 tracks: 30 MP3s, 60 LRC sidecars, one JPEG and one playlist, totaling 229,878,295 object bytes. The successful upload pass needed 85 objects/213,041,665 bytes and received 213,062,478 bytes including catalog/marker. Host inventory took 962.74 ms, staging 34.89 ms, lftp 318,736.31 ms and total delivery 319,735.34 ms. Payload throughput was approximately 0.668 MB/s (decimal). Connection took 7.000 s and trigger 0.713 s; cached publication checks took roughly 3–5.5 s. Association selected BSSID 02:00:00:00:00:02, channel 11, RSSI mostly -62 to -65 dBm during upload.

The worker recorded 159.158 s of write time plus 21.856 s of close/flush, worst write 0.886 s, queue high-water 2,093,664 bytes and 895 backpressure polls. Receive calls took 45.064 s; producer enqueue calls took 9.503 s. These work times overlap across tasks and must not be added as a serial wall-time breakdown. SD service averaged about 1.177 MB/s for this workload; this is not an isolated card benchmark. Internal minimum during upload was 14,136 bytes; verification later reached 10,588 bytes. No writer errors were reported.

The old activation phase was interrupted after more than six minutes of full media readback. It redundantly hashed MP3s in both object and track records, and hashed reused objects again. User explicitly rejected content verification for this home music player. Normal activation now checks the small catalog, object lengths, paths, metadata/playlist references and transactional NVS commit, without hashing media contents. Successful close/write results remain required. Pre-transfer garbage collection is removed so interrupted or completed-but-unactivated uploads can be reused on retry; conservative cleanup runs only after successful activation. Host activation/rollback, damaged catalog and retained/manual-file cleanup checks pass against the expanded export.

Remaining performance questions are measured independently in a future network-only RAM sink and SD-only sequential-write test. Current configuration has six static WiFi RX buffers and a six-frame receive aggregation window; the ESP-IDF iperf example uses sixteen and thirty-two respectively. Increasing them without reclaiming internal memory risks allocation failures. The measured upload is an interim result, not an accepted maximum.

The final activation retry exposed an 8 KiB sync-task stack overflow in library parsing. Metadata workspace now allocates from the catalog's PSRAM allocator; sync stack is 12 KiB, and radio shutdown happens immediately after upload, before parsing. Device activation succeeded with 4,120 bytes of stack headroom. The final follow-up sent 20,813 bytes (catalog/marker), completed around 36 s after start, and loaded two albums/30 tracks plus a 30-track playlist. Track zero decoded to four seconds without error and was paused; WiFi was confirmed disabled. Library parsing/activation still takes roughly 25–28 s and is a remaining optimization candidate; no media contents are hashed during that phase.

After interrupting the old readback and app flashing, one boot did not mount the card. A clean restart mounted it at 40 MHz and loaded the retained old library; the subsequent full catalog activation also succeeded. No filesystem corruption was established. Exact SD mount errors are now logged rather than hidden behind a generic Scan failure.

## Ordinary microSD naming supersedes object filenames

The user explicitly requires a normal music-player card layout. Format 2 exports `Artist/Album/NN - Title.mp3`, adjacent cover/lyric files and `Playlists/Name.m3u8` with relative entries. Hash-named preparation files remain host-only. FTP is rooted at `/sdcard/music`; catalogs/markers remain in `.pearl`. Bounded readable paths support spaces/Unicode and avoid FAT-reserved characters; collision suffixes are added only when required. Firmware accepts both catalog versions during migration, deduplicates ordinary scanned paths, and retires legacy hidden media only after successful format-2 activation. Normal media are not deleted when playlist selection changes yet; deletion ownership remains a separate task.

The actual 30-track, 93-file readable export passes the native FTP receiver and loader under sanitizers. The receiver test verifies every file length, every relative playlist target and unchanged-transfer mtimes. The migration check activates a real legacy catalog, switches to the readable catalog, confirms only legacy managed media are retired and retains readable/manual files. Main library, OOM, metadata, artwork, playlist, WiFi, recovery and power checks pass locally.

## Dedicated tracer and naming migration evidence

The unnecessary naming-copy run was stopped at the user's correction. The remaining migration reused 77 completed readable files, renamed 15 existing on-card media objects and uploaded only 1,717 bytes of small replacement sidecars/playlists, then catalog/marker. This rename completion took 8.51 seconds and the Pod reported successful activation/radio shutdown. No full naming-copy throughput result is claimed.

[SYNC-TRACER.md](SYNC-TRACER.md) specifies the new explicitly invoked SD-command tracer, dedicated filesystem sweep and RAM/combined FTP probes. Pinned SDK source narrows the earlier 8 KiB claim: it is the staged PSRAM/unaligned-path limit; aligned internal buffers may bypass it. Firmware builds and host sanitizer/protocol checks pass. Actual tracer flashing/measurements are pending absent USB enumeration. The approximately 0.668 MB/s full upload remains an interim measurement.
