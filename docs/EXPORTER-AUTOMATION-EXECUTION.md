# Exporter automation execution — October 3, 2026

## Execute

### Task and aim

Automate the selected Selected profile Plex playlists into the existing format-2 readable publication, freshly check explicit sync requests, and run the service persistently on example NAS. This executes option B in [the solution-space handoff](EXPORTER-AUTOMATION-SOLUTION.md). Offline firmware startup, readable card files, anonymous FTP and host-only upstream credentials remain constraints. Firmware, charging wake, card ownership/deletion and transport replacement are outside this execution.

### Declared success criteria and execution checklist

- Polling prepares complete source changes without a manual export command; a sync request checks the source after that request, then pins its completed publication.
- Concurrent polling/sync cannot race publication scratch files or change the generation being delivered. The adversarial check must fail a second independent publisher in the HTTP handler.
- Source, sidecar, selection and same-URL artwork changes during preparation reject the candidate. Checks must fail “check playlist order twice but ignore file/art mutations.”
- Preparation waiting and child encoders are bounded; tests must fail killing only the Python parent while leaving ffmpeg running.
- Equal-length changed bytes are replaced; unchanged audio stays untouched. Tests must fail size-only mirror correctness.
- Restart and unreachable-source tests preserve the last publication and keep HTTP alive. Checks must fail constructing Plex before HTTP startup or treating source failure as an empty success.
- Host cache reclamation protects current/previous publications and delivery; tests must fail deleting everything absent from the newest candidate. Full cache/card rejection must retain the published head/manual files.
- Deploy with verified NAS architecture, mounts, UID, LAN discovery, runtime dependencies and return FTP, then test recreation. Mac container success alone is insufficient evidence.
- Stop/pivot when cold preparation exceeds the receiver window, source mapping/profile is wrong, stale bytes are skipped, or deployment loses offline independence. Physical playback and Listener's independent use require household verification.

### Delivered characteristics

- **Met:** A serialized publication/delivery lock, fresh source check per accepted sync, immediate HTTP 202 response, pinned generation and terminal failure marker. Polling warms the cache and backs off on failure.
- **Met:** HTTP liveness starts independently of Plex. Persisted source-check time, publication error and transfer state are distinct. Current/previous host publications survive failed candidates.
- **Met:** Persistent source and prepared-audio fingerprints survive preparation-worker restarts. Album artwork is fetched once per album in each of the two validation phases. Mutation fences cover source/sidecar files and same-URL Plex artwork.
- **Met:** Bounded process-group preparation stops child encoders. Background preparation allows 600 seconds; accepted sync preparation allows 120 seconds including publisher-lock waiting. Long transfers retain the existing two-hour bound.
- **Met:** Hidden host-written `.pearl/delivered.json` identity receipt detects equal-length replacements without device media hashing. Receipt/catalog/ready follow media delivery; ready remains last. Missing/damaged receipts cause a conservative replacement pass for equal-length existing files.
- **Met:** A 20 GiB default host budget, transcode/free-space preflight and conservative retention/abandoned-work cleanup. Cleanup runs under the same lock as preparation/delivery. Current/previous publications and recent candidates remain protected.
- **Met:** QNAP deployment through its existing Portainer API, persistent host storage, read-only music/token mounts, non-root service, restart policy, liveness health check and retained stopped rollback containers.
- **Adjusted:** First/cold preparation is a background phase. HTTP `/sync` returns 503 while the first completed publication is absent. Large subsequent edits may fail the bounded fresh-check phase, retain prepared progress and finish through polling; the Pod is not kept waiting indefinitely.
- **Requires human/device verification:** This QNAP deployment syncing to the physical PearlPod and Listener independently using the resulting selection. NAS-to-native-receiver evidence is not a physical Pod claim.

### Changes

`syncer/orchestrator.py` owns deadline-bound publication processes, scheduler/backoff, status and serialization. `publisher.py` adds source/art mutation fences, artwork deduplication, publication age, previous-publication identity and safe error codes. `cache.py` performs bounded host retention. `media.py` persists prepared-file verification fingerprints and preflights conversion capacity. `server.py` queues fresh preparation/delivery while serving independent health/status. `transfer.py` adds delivery identity receipts and forced equal-length replacement, preserving normal card paths. `compose.yaml` makes source configuration explicit and adds liveness health checks.

`tools/deploy_syncer.py` reads the Portainer credential through `op read` at runtime, loads a privately built image or performs a Python-only incremental image update, initializes only the dedicated deployment directory and keeps the prior owned container for rollback. Known start/health failures restore it. Network timeouts with unknown outcomes require inspection before retry. `tools/check_sync_service.py` now exercises format-2 paths. `tools/check_sync.sh` includes the new adversarial and deployment tests.

### Verification

The final local `tools/check_sync.sh` passes the existing ten sync/media/native FTP tests, thirteen automation tests and two deployment tests, plus its native sanitizer checks. An additional narrow HTTP check confirms first-export 503 after the final assertion was added. Tests cover ordered publication change rejection, valid empty selection, shared native FTP failure paths, equal-length changed lyric bytes followed by no-op reuse, publisher serialization, pinned delivery, bounded lock waiting, child-process cancellation, source/sidecar/art mutation, cached-audio identity across worker restarts, corrupted cached-audio regeneration, current/previous retention and deployment rollback/ownership protection. Real NAS-prepared MP3s are used for audio checks; no replacement music is synthesized.

QNAP observations: x86-64, QTS 6.0.2, Docker 27.1.2-qnap12. The dedicated service is `pearlpod-syncer`, runs as 1000:1000 and serves LAN port 8787. Music is `/share/Media/Music` read-only; Plex source mapping is `/share/Media/Music` to `/music`. Cache is `/share/Container/pearlpod-syncer/cache`; the Selected profile token is `/share/Container/pearlpod-syncer/secrets/plex-token`, mounted read-only. The private image contains Python, ffmpeg and lftp. The deployed Python sources were compared by SHA-256 with the workspace. No registry publication, hosted CI, release or firmware flash was performed.

LAN mDNS resolves `_pearlpod-sync._tcp` to `192.0.2.2:8787` with FTP transport. The live source became two playlists/91 distinct tracks during execution; the current publication contains 281 files/630,967,634 delivery bytes. The final background publication pass took 189.494 seconds while reusing previously prepared assets; this is not a clean-cache full-export benchmark. Observed warm checks vary with Plex/artwork/filesystem work and remain within the 120-second fresh-check bound.

A real QNAP HTTP → shared native FTP receiver run delivered all 91 tracks. Zero available space produced `card_full` before music upload. Full delivery took 117.265 seconds in that host harness; an unchanged follow-up found zero missing objects, left audio mtimes untouched and took 1.789 seconds for delivery after preparation. These are host receiver measurements, not physical PearlPod throughput. The earlier inventory timing counter excluded the listing; it was corrected before the final deployed verification.

A separate temporary NAS test container used an intentionally unreachable Plex URL, a read-only view of the persisted cache and no mDNS advertisement. Both startup and restart kept HTTP alive, retained the published catalog/check time and reported the source error. **Household Plex stayed available and was never stopped or altered.** The temporary container was removed. The actual service was recreated through the deployment tool and recovered its persisted publication/preparation work. A reboot of the entire NAS was not performed; its restart policy is configured, and container recreation/restart is the tested boundary.

### Risk retirement

| Risk / assumption / stop trigger | Status | Tempting patch this check fails | Evidence / route |
|---|---|---|---|
| Fresh request after an in-flight poll | Retired by evidence | Deliver the poll's already-started snapshot as fresh | Scheduler test requires a second check after request; real service executes bounded fresh preparation before upload |
| Publication/delivery races | Retired by evidence | Direct independent `publish()` in the HTTP handler | Concurrent refresh and pinned-consumer tests; delivery holds the publication lock |
| Receiver preparation deadline | Triggered, adjusted and checked | Extend every radio wait indefinitely | Cold background pass exceeded 120 seconds; first publication now returns 503, bounded fresh requests fail explicitly, cached work survives to background retry; deadline/child tests pass |
| Plex unavailable at startup/restart | Retired by evidence | Restart-loop the entire server or clear the selection | Local HTTP/failure/recovery tests and isolated unreachable-source NAS startup/restart check with persisted publication |
| Equal-length changed media | Retired by evidence | Size-only lftp reuse | Native receiver test replaces equal-length lyric bytes, then proves no-op mtime preservation; real NAS follow-up reports zero missing objects |
| Source/sidecar/art mutation | Retired by evidence | Check only playlist membership twice | Source fingerprint checks, sidecar-addition and same-URL artwork mutation tests reject candidate and retain prior head |
| QNAP runtime/mapping/LAN assumptions | Retired by evidence at host boundary | Infer NAS support from a Mac image | Observed NAS architecture/runtime, dedicated mounts/UID, actual preparation, mDNS, return FTP and container recreation |
| Cache exhaustion and unsafe reclamation | Retired by evidence at host boundary | Delete unreferenced-by-latest files while a job runs | Budget and retained-current/previous/failed-candidate tests; shared lock; actual card-full preflight rejects before media upload |
| Polling may be unnecessary | Accepted with rationale | N/A | Cold preparation is materially longer than a fresh-check budget; background preparation is useful. Additional event infrastructure remains outside scope |
| Physical storage/playback/charging | Accepted with rationale | N/A | No device was flashed or physical sync attempted in this execution. Existing FAT/control/power limitations remain in hardware evidence; scheduled charging wake is excluded |

### Review

**Aim:** Automatic fresh Plex-to-PearlPod publication on QNAP using the established contract.

**Verdict:** Host implementation and deployment align with the selected approach. Review caught loss of process-local prepared checksums after moving preparation to a bounded process; persistent prepared-file fingerprints and an adversarial worker-restart/corruption test address it. Review also corrected misleading inventory timing and explicitly protected the previous host publication from newer failed candidates. No transport, device authentication or startup change was introduced.

**Remaining limits:** A device sync from this NAS is still required. Existing ordinary-path format-2 updates do not promise byte-for-byte rollback of every already-replaced media file across a later failed multi-file update; host head retention is not a claim of transactional FAT media replacement. Normal sync retains structural/length/write checks without media hashing. Card ownership/deletion, charger sensing, audible playback and Listener's adoption remain outside host-test evidence.

### Operational handoff

Use [the syncer README](../syncer/README.md) for full-image and incremental Portainer deployment commands, status and rollback. Keep upstream credentials in runtime files. Stopped rollback containers remain intentionally available; do not start one alongside the current mDNS advertiser. Use `GET /status` for current freshness and errors, `GET /health` for process liveness and `tools/check_sync_service.py --host 192.0.2.2` for a host receiver acceptance run. The household player can discover the NAS through the existing Sync now flow; no source contact is added to firmware boot.

## Exporter speed checkpoint

The deployed performance update reduced a measured warm 91-track publication from 15,497 ms to 2,630 ms, approximately 5.9× faster, while preserving catalog `a1b52eecb422c4cd3cddb3723252a148722cbc70100ea7accb9f82badaf81f2d`. It scans each source directory once per phase, reuses complete prepared-track results when source/sidecar/art/metadata and output fingerprints match, obtains album thumb URLs from the playlist response, fetches artwork with four workers and prepares changed tracks with two workers. Concurrent cache writes use unique temporary files and atomic replacement. Cold conversion speedup has not been independently benchmarked. The existing thirteen automation checks pass on this update; a narrow added track-cache check confirms unchanged tracks bypass preparation and changed source inputs invalidate reuse.
