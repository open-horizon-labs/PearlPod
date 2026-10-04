# Fast startup from the saved library index

Normal boot loads `music/.pearl/library.idx` directly into PSRAM. It contains the complete scanned library: album/artist/title/genre, track/disc order, artwork and lyric paths, collection membership and exact playlist order (including duplicates). Boot does not enumerate music directories, read tags, parse playlists, validate every media file or decode album artwork. Manual card edits require Rescan card, as agreed; unchanged files are intentionally not checked individually at boot.

The snapshot has a magic number, explicit format version, root identity, active managed-catalog revision, bounded strings/counts, reference validation and a checksum of the index bytes. Music is never hashed. Missing, truncated, corrupt, incompatible or wrong-catalog snapshots trigger scanning and replacement. Array/string allocation failures leave the destination unchanged and release all partial allocations. PSRAM allocation preserves the same 2 MiB playback reserve as the scanner; a 16 MiB serialized-index bound rejects unreasonable files without introducing a new scanner track limit.

Saving writes `library.tmp`, flushes and fsyncs it, then removes the old index and renames the completed temporary. FAT cannot overwrite an existing destination. A reset between removal and rename leaves no usable index and boot rebuilds; boot never uses a partial temporary. This intentionally favors a safe rescan over a more complicated second snapshot journal. Music and playlists are untouched by index publication.

Explicit Rescan invalidates the snapshot and rebuilds it after a successful scan. Ordinary sync invalidates it when the first upload bytes arrive, so completed playlists remain discoverable after a crash. Changed or interrupted transfers rebuild through the existing rescan path. A completed unchanged sync restores the existing in-memory snapshot without rescanning. The active catalog revision also prevents reuse after activation changes but before the rescan finishes. Diagnostic transfers do not alter the library index.

## Verification

Sanitizer-backed production-code tests cover metadata/lyrics/artwork round trips, duplicate playlist order, version/catalog mismatch, truncation, malformed references with a correctly recomputed checksum, every allocation-failure point, interrupted temporary files and absent indexes. Removing the test media file still permits loading the index, proving that loading does not quietly rescan or stat individual songs. Existing general and production UI checks pass.

Physical device evidence (`backups/index-device.log`, ignored):

- Initial missing-index boot scanned once and reached library-ready at 22,843 ms.
- Next boot logged `Library index loaded`, with six albums and 91 songs, and reached library-ready at 1,505 ms. Test playlist playback advanced three seconds without a decoder error.
- Explicit Rescan completed successfully, preserving the same albums/tracks/playlists. Its following boot reached library-ready at 1,504 ms from the index.
- The next household sync reported zero missing music objects/bytes. Firmware logged `Unchanged library index saved=1`; the following boot loaded the index and reached library-ready at 1,507 ms.
- Final status was ready and paused, USB live, screen awake and WiFi off. No local sync/export service was started.

## Execution review

Aim: make normal startup fast without changing music browsing, playback or deliberate Rescan semantics. Review disposition: continue; implementation remains a versioned snapshot rather than a new database or automatic change detector.

| Risk | Disposition | Adversarial evidence |
|---|---|---|
| An index implementation still scans music | Retired | Load succeeds with the original audio absent; real startup improves from 22.8 s to 1.5 s. |
| Metadata or playlist ordering is lost | Retired | Production serializer tests include lyrics, art paths, genre, disc/track tags and duplicate playlist entries; physical groups and playlist playback match. |
| Damaged/interrupted index causes a crash | Retired | Corruption/truncation/version/reference tests and exhaustive allocation failures reject the snapshot without changing the destination; orphan temporaries are ignored. |
| Sync activation/crash reuses stale library data | Retired | Revision mismatch rejects old indexes; upload invalidation precedes later publication processing; normal changed/interrupted sync uses rescan. Real unchanged sync restores the index and retains fast reboot. |
| Manual edits are silently discovered | Accepted with rationale | No automatic filesystem change detection is requested. Listener uses Rescan card after manual edits. |

No new human verification is required for cache correctness or measured startup. Physical visual preferences for the existing theme artwork remain unchanged.
