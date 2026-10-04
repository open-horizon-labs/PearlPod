# Playlist sync status and recovery

## Direction and success criteria

The aim is that Listener can tell which playlist is arriving, what is happening now, how much remains, and whether anything finished before an interruption. The user's selected hierarchy is playlist first. This is an Operate surface within PearlPod's established navy, cream, yellow and teal world; it does not replace the surrounding player design. The 460×460 touchscreen leads with a 28 px playlist name, then a 20 px song/action, 16 px artist/album context, an explicitly overall song count, a visible linear progress bar, a whole-sync time estimate and the existing 64 px action target. Long playlist and song names scroll. Unknown totals hide the bar; a stall replaces the ETA with a waiting message. Success opens Playlists directly.

The implementation must preserve fast offline startup, playback/volume controls, open household networking, readable microSD paths, incremental copying, and the separate diagnostics needed for performance measurements. It must not rehash music, introduce recurring SD status-file writes, or display a precise estimate before a transfer rate exists. Playlist checkpoints must not require rescanning or decoding the entire library between uploads.

## Transfer and ETA contract

The firmware requests `progress_version: 1` in the existing HTTP trigger. An updated service inventories the card once, compares file sizes and prepared source identities, and orders only the delta by playlist membership. Shared tracks and their artwork/lyrics transfer once. Already-prepared ID3 tags supply song/artist/album names without an extra Plex request or music decode. Small, bounded JSON messages use `SITE PEARL` over the same lftp control connection. They describe the current playlist/song/action, confirmed completed songs/bytes, total changed bytes, current file size and saved playlist count. The device samples its actual file receive counter; diagnostic byte/latency counters remain separate.

Progress is based on changed bytes, including artwork, lyrics and playlist files. Completed songs advance after FTP has closed their files and acknowledged them. The ETA uses a smoothed measured rate, waits at least five seconds for a sample, rounds to minutes or hours, and disappears after fifteen seconds without new data. Updating/activation displays “Almost ready to listen” because transfer-only timing does not predict library scanning. An older service still works and displays “Time estimate unavailable.”

## Playlist checkpoints

lftp writes hidden `.in.*` temporary files, closes them, then renames them to ordinary music filenames. The playlist `.m3u8` is published after its referenced songs, artwork and lyrics. A receipt of confirmed source identities is then atomically renamed into `music/.pearl/delivered.json` before the “playlist saved” event. The next attempt uses those identities plus inventory sizes to skip completed work, including same-size changed files.

The ordinary microSD scanner already discovers readable music and `.m3u8` files without a full-sync catalog marker. A completed playlist can therefore load after reboot while later playlists are unfinished. Cancellation/connection failure also rescans completed files once playback has been restored. A failed current file remains hidden and is retried from its beginning; byte-offset resume is not implemented. A crash before a playlist receipt commits can cause that playlist's new files to be sent again. This is application-level interruption recovery, not a guarantee against FAT corruption from abrupt power loss.

## Verification and risk retirement

| Risk | Tempting patch rejected | Check |
|---|---|---|
| ETA based on the whole library instead of the delta | Divide all exported bytes by lifetime throughput | Native delivery retry/no-op tests verify only missing songs count; C tests cover measured warmup, stall suppression, retry and totals over 4 GiB |
| Shared songs counted/copied repeatedly | Walk every playlist without deduplicating paths | Two test playlists share a repeated song; native progress and media mtimes must show one copy |
| A finished playlist disappears or repeats after a crash | Keep receipt/playlist publication until the full-sync marker | Stop the receiver immediately after the first saved event, scan the card with production library code, then retry and compare completed media mtimes |
| Partial song appears as playable music | Upload directly to final `.mp3` paths | Hidden temporary-file setting and interrupted-transfer checks; normal scanner ignores hidden files |
| Status breaks on TCP packet boundaries or hostile text | Assume one recv is one complete command | Fragmented/pipelined control-line tests and bounded escaped JSON tests |
| A stale UI claims success or ignores cancellation | Use cosmetic progress unrelated to receiver/service state | Host UI captures/assertions for idle, transfer, stall, preparation, error, long names and success routing |
| UI changes slow transfer or startup | Poll/write SD progress every second or decode artwork synchronously | Progress uses one small control message per file/checkpoint, no periodic SD status writes; local ESP-IDF build |

Host checks and local build are required before the commit. Physical transfer timing, visual legibility on the AMOLED, radio peak memory and an actual device reboot during sync require hardware observation; host crash simulation does not establish those properties. Review and runtime results are recorded below when complete.

## October 4 verification

The local ESP-IDF 5.5.5 build passed, along with the sanitizer-backed progress model, the existing ten sync checks, fourteen exporter automation checks, two deployment checks, three new content/interruption checks, the general host suite and the LVGL interaction/render harness. The new checks use real cached NAS exports for delivery. They stop the receiver immediately after the first playlist's saved event, verify its repeated entries with the production library scanner without a full-sync marker, confirm hidden interrupted audio does not enter the library, and restart delivery. The resumed job transfers only the second playlist's missing song; the first song's modification time remains unchanged. No-op delivery preserves every media/playlist timestamp. Raw fragmented/pipelined commands and lftp's own quoting of hostile/multibyte title text are tested separately. The progress parser limits both message size and JSON depth before cJSON parsing.

The independent Impeccable finish review requested two corrections: a visible remaining bar track and explicit whole-sync scope for counts/ETA. The verdict pass scored both resolved and returned **ship within host/source scope**. Seven captured states cover idle, receiving, preparation, stall, error, success and long names. Scrolling names that exceed the wire budget now retain a visible `...` suffix. The documenter confirmed inheritance of the incumbent design system; this extension changes no global design tokens. Pre-existing PRODUCT.md platform/no-WiFi description drift was recorded without repair.

Hardware installation and live service acceptance are pending: the Mac currently detects no PearlPod USB serial port, and the NAS service at `192.0.2.2:8787` was unreachable. No new firmware was flashed, NAS container replaced, physical sync started or release published in this checkpoint. Both firmware and syncer must be updated to get the new content messages; old-service compatibility deliberately falls back to an unavailable ETA. The host tests establish application interruption/retry behavior, not physical power-cut filesystem durability or a new hardware throughput result.
