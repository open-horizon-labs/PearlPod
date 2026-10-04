# Runner discovery, progress and interruption repair

Aim: a manual sync finds the current household runner, shows playlist/song progress and a measured ETA, and leaves each completed playlist usable after a reset. Preserve offline startup, open household networking, ordinary music paths, the runner's Selected profile profile, and separate Listener/Jonah preparation.

## Causes established on the player

A saved `pearl_sync/source` bypassed mDNS entirely. An unreachable old URL led directly to generic failure. Discovery now considers all returned IPv4 service addresses, probes `/health`, and prefers a reachable advertised service; a saved URL is a checked fallback. `sync source auto` clears that fallback. A physical run with the closed NAS endpoint `192.0.2.2:8787` saved selected the runner at `192.0.2.254:8787`.

The runner's deployed server/transfer modules predated `progress_version: 1`. Restored negotiation and playlist-ordered transfers while preserving the deployed Selected profile, `J:` open-share exports and FLAC tag iteration fix. `SITE PEARL` drives the existing content UI without recurring SD status writes. Small/already-present playlists publish first; song order stays intact. Short rate stalls receive roughly twenty-sample smoothing; ETA blends the measured whole-session rate with the recent rate (75%/25%), including actual control and SD pauses. A fifteen-second stall still explicitly says it is waiting.

A real transfer crashed at 5.3 MB with a NULL device in `resume_dev_in_isr`, through `bg_exit_core`. ESP-IDF upstream fix [a16c003](https://github.com/espressif/esp-idf/commit/a16c00376b85ffcf48b1ba53e96d83dd47f76fd5) snapshots the acquiring/selected device rather than rereading a concurrently changed lock pointer. The checked-in patch is applied by `tools/build.sh` only to the exact pinned v5.5.5 source hash, and verifies the patched hash. `tests/test_spi_race.py` compiles the actual patched ISR against injected owner changes that break the original code.

After removing the panic, transfer tracing established the next failure: FAT's rename returns `EEXIST` when lftp publishes over an existing file. `vendor/ftp/replace.c` preserves the previous file under a hidden backup and writes a flushed replacement journal before moving names. Startup recovers an interrupted rename before scanning the library. Tests emulate FAT overwrite rejection, a failed second rename, and resets before/after final publication. A journal failure never silently reports success. Completed media on older cards without receipts is reused by length; a known changed identity still forces replacement. Subsequent completed receipts carry current identities.

A full-card retry exposed an inherited FTP send bug: nonblocking `send()` may accept only part of a 32 KiB receipt buffer. The old code retried the entire buffer or reset the connection instead of advancing the sent prefix. The same assumption existed in listings and control replies. All three now share a bounded send helper that advances offsets, waits through `EAGAIN`, handles interruption and fails cleanly on disconnection/timeout. `tests/test_ftp_send.py` compiles the production helper against deterministic partial sends and backpressure.

The runner retains a bounded lftp error tail in `/status`. This contains anonymous FTP paths and file details, never the Plex token. The player logs each completed playlist and the chosen runner. Console status includes saved-playlist count, progress, completion and full content text.

## Evidence collected

- General host checks passed; sync checks passed using two real prepared NAS songs, including metadata, embedded art, sidecar lyrics, native FTP, same-sized changes, checkpoint reset/retry and no-op transfers. No generated music was used for these integration checks.
- Created `PP: Sync Check A` and `PP: Sync Check B` in the logged-in Selected profile browser profile; both reference the existing Katy Perry “Woman's World” song.
- Firmware UI remained ready at about 858 ms. WiFi starts only on demand.
- A cancelled hardware run received more than 75 MB without repeating the SPI panic. The retry reused approximately 235 MB already present.
- A subsequent real transfer published P!nk plus both new test playlists. Forced USB chip reset during the next playlist's transfer, then enumerated the card after reboot: both test playlists contained one track, P!nk contained thirty, partial hidden media stayed unlisted. Playing each test playlist advanced playback to three seconds with no decoder error.
- The complete hardware transfer succeeded: 345,004,709 missing bytes across 141 files; inventory 1.127 s, staging 1.135 s, lftp 862.090 s (about 0.400 MB/s including file/control overhead). Both endpoints reported completion, four playlists saved, activation succeeded, then the card rescanned to six albums and ninety-one unique songs. This is a functional result, not a claim of maximum throughput.
- Playlist-edit and no-op hardware checks remain pending while the newly discovered receipt-send issue is repaired.
