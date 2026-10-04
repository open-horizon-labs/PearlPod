# Dedicated sync tracer

The tracer is part of the normal locally built firmware and starts only on an explicit console command. It hooks the mounted card's `do_transaction` callback, forwards the original command unchanged, and records command duration, opcode, requested payload size, R1 response and error. It does not patch ESP-IDF. Normal startup allocates no event history or diagnostic task; the disabled hook forwards directly. Starting tracing allocates a bounded 2,048-event history in PSRAM, approximately 64 KiB. Sixteen longest command events are retained separately. Cumulative duration/byte counters use 64 bits, including runs longer than the approximately 71-minute range of 32-bit microseconds. Aggregate counters/histograms continue when the event ring wraps; `events_overwritten` reports loss of old detailed events. Stop disables recording and retains the history for inspection.

## Fact check of the performance claim

| Claim | Status | Primary evidence | Correction |
|---|---|---|---|
| The old I²C-driver warning causes slow SD transfer | Unsupported | Pinned ESP-IDF v5.5.5 `components/driver/i2c/i2c.c`, startup constructor `check_i2c_driver_conflict`; PearlPod mounts the card with SDMMC | Treat it as a boot-time migration warning; no measured transfer-time attribution exists |
| 8 KiB caps every card transaction | Verified with edit | Pinned `components/sdmmc/sdmmc_cmd.c`, `sdmmc_write_sectors`, and `include/sd_protocol_types.h` | It caps the current staged PSRAM/unaligned path at 16 × 512-byte sectors. Aligned internal DMA-capable buffers can bypass staging and use a larger multi-block command |
| Increasing that staging size will solve the throughput gap | Unsupported | Existing workload counters establish stalls and backpressure, but do not isolate their cause | Measure transaction sizes, command time, status polling and network-only reception before attributing the gap |
| 40 MHz × four data wires corresponds to 20 MB/s | Verified with edit | Mounted card reports 40,000 kHz/four-bit SDR configuration; arithmetic gives 160 Mbit/s | This is raw bus capacity, before commands, card programming, filesystem work, CPU scheduling and network limitations; it is not measured file throughput |

Source gaps: the dedicated SD sweep has produced physical measurements below; RAM-only reception and combined WiFi-to-card measurements remain pending USB availability. Describe 8 KiB as the staged-path limit, and keep overall transfer attribution unproven until those measurements complete.

## Commands

- `trace start`: reset counters and enable command recording; refuses while sync or an SD sweep is active.
- `trace status`: print a versioned JSON snapshot with card configuration, internal/PSRAM heap, SD counters, command timing and histograms.
- `trace events 128`: export the last requested number of command events (maximum 2,048), then the sixteen longest retained events.
- `trace stop`: stop recording and print the final snapshot.
- `trace sd`: run the dedicated filesystem write sweep using bytes from an existing track. WiFi must be off, playback paused, and library scanning idle. The playback worker and I²S channel are stopped and released during the sweep, then recreated. No mutex ownership is handed between tasks.
- `trace net http://HOST:8877`: start a bounded five-minute diagnostic FTP session using a transient trigger URL. It never changes the saved sync URL and never activates a catalog. The ordinary FTP/PSRAM writer and WiFi configuration are used. Only this explicit diagnostic session recognizes `/.pearl/.bench-ram` as a discard sink.
- `trace cancel`: cancel a sweep or network session. A long power hold also requests cancellation and waits for safe closure before sleeping.

`sd bench` remains an alias for the dedicated SD sweep. `sd trace` and `sync trace` retain the existing human-readable configuration/pipeline snapshots.

## Measurements

The SD sweep writes six temporary 16 MiB files sequentially, deleting each one after closure. Data are repeated from the first MiB of an existing song, loaded before measurement. The files are diagnostic binaries under `music/.pearl`, never songs or playlist members. The sweep compares normal 32 KiB buffered PSRAM writes, direct POSIX 32 KiB PSRAM writes, and aligned internal 4/8/16/32 KiB buffers with direct POSIX writes. Only the baseline mode uses buffered stdio, so libc cannot split the direct-path batches. Internal-memory allocation failure skips a mode. The mounted driver's shared DMA buffer and chunk settings are never replaced during the sweep. The SD hook reveals whether the internal path actually yields larger card commands; the caller's buffer size alone does not establish that.

Each sweep result reports completed bytes, full write-loop elapsed time, close time, copy time, explicit task-yield time, worst write operation, command count, command-write/status time, errors and per-mode command size/latency histograms. Successful timing includes final closure. A yield occurs at most once per 20 ms of active writing, rather than after every chunk, and its elapsed time is recorded. An existing diagnostic file is not overwritten; remove a stale `.trace-sd.tmp` deliberately before retrying. No raw sectors are written and the card is never reformatted.

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

## Measured SD results and memory changes

The October 3 dedicated sweep wrote and closed six 16 MiB temporary files successfully, with no reported SD errors. Data came from an existing music file. Decimal MB/s includes write-loop and close time; copying and task yields are already inside the write-loop time.

| Source and write path | Batch | Write + close | MB/s | SD commands |
|---|---:|---:|---:|---:|
| PSRAM, buffered stdio | 32 KiB | 8.900 s | 1.885 | 121,708 |
| PSRAM, direct POSIX | 32 KiB | 8.028 s | 2.090 | 110,205 |
| Internal aligned, direct POSIX | 4 KiB | 10.553 s | 1.590 | 134,329 |
| Internal aligned, direct POSIX | 8 KiB | 8.513 s | 1.971 | 115,221 |
| Internal aligned, direct POSIX | 16 KiB | 6.110 s | 2.746 | 81,435 |
| Internal aligned, direct POSIX | 32 KiB | 4.807 s | 3.490 | 56,625 |

The 32 KiB aligned mode produced 512 card commands with 32,768-byte payloads; the PSRAM paths produced 2,048 commands with 8,192-byte payloads. This verifies that aligned internal batches bypass the unchanged 8 KiB shared staging buffer. The 32 KiB result is 1.85 times the buffered baseline on this sweep, not a measured end-to-end sync speed. CMD13 polling is included in the command totals. Its time measures commands, not pure card-programming time.

After all six measurements, the initial tracer cleanup reset the device: the console task had acquired an audio preference mutex that the SD task attempted to release. That cross-task handoff has been removed. These SD measurements completed before the cleanup assertion; they do not establish successful playback restoration. The runner now detects assertions/resets instead of waiting for a missing completion marker. Local capture: `/tmp/pearl-direct-dma-trace.jsonl` (excluded from Git).

Before playback teardown was added, relocating CPU audio arrays to external BSS raised idle internal free memory from 94,983 to 145,875 bytes, and the largest internal block from 31,744 to 65,536 bytes. The 50,892-byte change includes eligible lwIP globals relocated by the SDK external-BSS option, beyond the audio arrays' 37,376 bytes. The pinned I²S implementation copies caller samples into its own DMA buffers (`components/esp_driver_i2s/i2s_common.c`), so CPU input/PCM/output arrays can live in PSRAM.

The new sync lifecycle also deletes the I²S channel, exits the 32 KiB decoder task and 3 KiB preference task, closes the decoder file and NVS handle, and powers down the DAC. The I²S configuration allocates approximately 32 KiB of sample DMA memory plus descriptors/control structures. Actual reclaimed heap is logged before/after teardown; do not infer that measured amount from stack declarations alone. The control queue and locks remain reusable. At completion, cancellation or transfer failure, FTP closes and WiFi shuts down before playback is recreated. Successful normal sync rescans after the worker is ready.

The production writer retains its bounded 2 MiB PSRAM queue and reserves a separate 64-byte-aligned internal 32 KiB DMA buffer. It assembles full batches as packets arrive, releasing ring space after copying; waiting for a whole batch to accumulate in a one-batch ring can deadlock on irregular packet sizes. Only a completed file's final tail is short. Direct POSIX writes avoid libc splitting these batches. Copy time is reported separately. Host sanitizer checks cover irregular segmentation, exact byte order, wraparound, final-tail drain, cancellation, restart, write failure and batch coalescing.

With audio memory released for transfer, WiFi uses the SDK iperf receive profile of 16 static RX buffers, 64 dynamic RX buffers and a 32-entry receive aggregation window. Sustained throughput improvement from that profile is not yet measured. Increasing the PSRAM queue alone cannot raise throughput once the SD writer is the sustained bottleneck; the queue handles temporary stalls. Live UI resources remain available for progress and cancellation.

The local ESP-IDF v5.5.5 build, ten sync tests, two tracer tests and final sanitizer-backed writer check pass with these changes. The diagnostic runner waits for radio-off, completed diagnostic teardown and error-free paused playback readiness before advancing to another phase; radio-off alone is insufficient evidence of restoration. The Mac currently does not enumerate PearlPod's USB serial device, so flashing the full playback teardown and checking SD-sweep completion, RAM/card reception, audio restart and repeated-cycle heap recovery remain pending. The approximately 0.668 MB/s prior album upload remains the latest measured complete music sync, not a maximum or an accepted final performance result.

Review status is **Adjust until physical acceptance**. The mechanism addresses internal-memory pressure and measured write fragmentation without enlarging the stall queue. Irregular-packet and batch-count checks reject a tempting implementation that either deadlocks while waiting for a full batch or silently returns to packet-sized writes. Byte-order, final-tail, cancellation/restart and write-failure checks cover shared writer integrity. Hardware audio restart, WiFi peak memory with the expanded RX profile, repeated-cycle heap recovery and actual combined throughput remain unretired checks; host tests cannot establish them. No music library retransfer, filesystem reformat or release publishing was performed for this checkpoint.
