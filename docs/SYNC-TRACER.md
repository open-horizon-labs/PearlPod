# Dedicated sync tracer

The tracer is part of the normal locally built firmware and starts only on an explicit console command. It hooks the mounted card's `do_transaction` callback, forwards the original command unchanged, and records command duration, opcode, requested payload size, R1 response and error. It does not patch ESP-IDF. Normal startup allocates no event history or diagnostic task; the disabled hook forwards directly. Starting tracing allocates a bounded 2,048-event history in PSRAM, approximately 64 KiB. Sixteen longest command events are retained separately. Cumulative duration/byte counters use 64 bits, including runs longer than the approximately 71-minute range of 32-bit microseconds. Aggregate counters/histograms continue when the event ring wraps; `events_overwritten` reports loss of old detailed events. Stop disables recording and retains the history for inspection.

## Fact check of the performance claim

| Claim | Status | Primary evidence | Correction |
|---|---|---|---|
| The old I²C-driver warning causes slow SD transfer | Unsupported | Pinned ESP-IDF v5.5.5 `components/driver/i2c/i2c.c`, startup constructor `check_i2c_driver_conflict`; PearlPod mounts the card with SDMMC | Treat it as a boot-time migration warning; no measured transfer-time attribution exists |
| 8 KiB caps every card transaction | Verified with edit | Pinned `components/sdmmc/sdmmc_cmd.c`, `sdmmc_write_sectors`, and `include/sd_protocol_types.h` | It caps the current staged PSRAM/unaligned path at 16 × 512-byte sectors. Aligned internal DMA-capable buffers can bypass staging and use a larger multi-block command |
| Increasing that staging size will solve the throughput gap | Unsupported | Existing workload counters establish stalls and backpressure, but do not isolate their cause | Measure transaction sizes, command time, status polling and network-only reception before attributing the gap |
| 40 MHz × four data wires corresponds to 20 MB/s | Verified with edit | Mounted card reports 40,000 kHz/four-bit SDR configuration; arithmetic gives 160 Mbit/s | This is raw bus capacity, before commands, card programming, filesystem work, CPU scheduling and network limitations; it is not measured file throughput |

High-risk unresolved claims: none. Claims requiring expert review: none identified. Source gaps: physical results for this new tracer are pending USB availability. Corrections to apply: describe 8 KiB as the current staged-path limit, and keep attribution/achievable throughput unproven until the dedicated measurements complete.

## Commands

- `trace start`: reset counters and enable command recording; refuses while sync or an SD sweep is active.
- `trace status`: print a versioned JSON snapshot with card configuration, internal/PSRAM heap, SD counters, command timing and histograms.
- `trace events 128`: export the last requested number of command events (maximum 2,048), then the sixteen longest retained events.
- `trace stop`: stop recording and print the final snapshot.
- `trace sd`: run the dedicated filesystem write sweep using bytes from an existing track. WiFi must be off, playback paused, and library scanning idle. Playback commands are suspended through the existing audio detach/attach mechanism during the sweep.
- `trace net http://HOST:8877`: start a bounded five-minute diagnostic FTP session using a transient trigger URL. It never changes the saved sync URL and never activates a catalog. The ordinary FTP/PSRAM writer and WiFi configuration are used. Only this explicit diagnostic session recognizes `/.pearl/.bench-ram` as a discard sink.
- `trace cancel`: cancel a sweep or network session. A long power hold also requests cancellation and waits for safe closure before sleeping.

`sd bench` remains an alias for the dedicated SD sweep. `sd trace` and `sync trace` retain the existing human-readable configuration/pipeline snapshots.

## Measurements

The SD sweep writes six temporary 16 MiB files sequentially, deleting each one after closure. Data are repeated from the first MiB of an existing song, loaded before measurement. The files are diagnostic binaries under `music/.pearl`, never songs or playlist members. The sweep compares normal 32 KiB buffered PSRAM writes, unbuffered 32 KiB PSRAM writes, and aligned internal 4/8/16/32 KiB buffers with unbuffered writes. Internal-memory allocation failure skips a mode. The mounted driver's shared DMA buffer and chunk settings are never replaced during the sweep. The SD hook reveals whether the internal path actually yields larger card commands; the caller's buffer size alone does not establish that.

Each sweep result reports completed bytes, full write-loop elapsed time, close time, copy time, explicit task-yield time, worst `fwrite`, command count, command-write/status time, errors and per-mode command size/latency histograms. Successful timing includes final closure. A yield occurs at most once per 20 ms of active writing, rather than after every chunk, and its elapsed time is recorded. An existing diagnostic file is not overwritten; remove a stale `.trace-sd.tmp` deliberately before retrying. No raw sectors are written and the card is never reformatted.

The host runner sends 32 MiB of real source bytes to the RAM discard sink, then to a temporary card file through the normal 2 MiB PSRAM queue and separate writer. It logs one-second serial snapshots and sender TCP metrics on macOS: congestion window, receiver-advertised send window, buffered bytes, RTT in **milliseconds**, retransmission bytes and retransmission packet count. The ABI offsets/units are checked against the installed macOS SDK `netinet/tcp.h`. Socket send completion and receiver drain/close are timed separately; the final reported MB/s includes the receiver's FTP 226 response. No media hash/readback is performed. Temporary card files are deleted, no catalog is uploaded, and diagnostic radio sessions are cancelled after each phase.

Run with the player's USB serial device connected, paused playback, and a completed export or NAS music file:

```sh
.flash-venv/bin/python tools/trace_sync.py /path/to/existing/music.mp3 \
  --host-ip 192.0.2.101 --output /tmp/pearl-tracer.jsonl
```

`--mode sd`, `--mode ram` and `--mode card` select individual phases; `--bytes` bounds network volume to at most 256 MiB. Default port 8877 avoids the ordinary sync service on 8787. Serial enumeration must find exactly one Espressif USB device or an explicit `--serial` is required. The tool records configuration and results; it makes no changes to the Plex/NAS source or selected playlists.

## Reading the evidence

Latency histogram upper bounds are 100, 500, 1,000, 5,000, 20,000, 100,000 and 500,000 microseconds, followed by overflow. Payload-size histogram upper bounds are 512, 1,024, 4,096, 8,192, 16,384, 32,768 and 65,536 bytes, followed by overflow. Zero-payload commands enter the latency histogram, not the size histogram. Byte counters describe requested command payloads; errors may mean a command did not transfer all those bytes.

`status_us` is time inside CMD13 transactions. `busy_statuses` counts successful CMD13 responses without READY_FOR_DATA/transfer state. `status_gap_us` measures gaps from the preceding write/status command to a subsequent CMD13: these include software/scheduler gaps and must not be called pure card-programming time. Event timestamps and responses allow inspecting the sequence. `record_us` approximates bookkeeping/lock time outside the forwarded driver call; it excludes serial printing, which never runs inside the SD hook.

WiFi/receive work, PSRAM enqueue work and SD writer work overlap. Their cumulative durations cannot be added as sequential wall time. SD command time is inside filesystem writes, rather than another independent stage. `internal_min` is the allocator minimum since boot, not a freshly reset per-phase minimum. Capture free/largest-block samples as well as that minimum. The RAM sink measures this firmware's FTP/socket receive path, rather than raw radio/iperf capability. The dedicated probes intentionally exclude NAS export, inventory and library activation; ordinary host delivery traces and activation measurements cover those phases separately.

## Validation and current device status

Local ESP-IDF v5.5.5 build succeeds. Compiler stack reports are 48 bytes for the SD hook, 448 bytes for a snapshot and 352 bytes for the sweep task function; these are individual static frames, not complete runtime stack high-water measurements. The complete sync check passes ten tests, and the dedicated tracer check passes two tests. Sanitizer-backed host checks verify transparent forwarding, error/response preservation, command classification, disabled recording, memory-allocation failure, reset/busy guards, bounded history, retained slow events and diagnostic-task allocation failure. macOS TCP ABI field offsets and RTT units are checked. Native FTP interoperability checks verify that the RAM sink discards only when explicitly armed, reports received bytes, and leaves subsequent normal writes functional; repeated relative uploads remain valid.

These checks are not hardware throughput evidence. The Mac currently does not enumerate PearlPod's USB serial device, so flashing and the physical SD/RAM/combined sweep are pending. The existing approximately 0.668 MB/s workload result remains the latest measured full upload, not a maximum or an accepted final performance result.
