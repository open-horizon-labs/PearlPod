# Exporter automation and QNAP deployment

## Solution Space

### Solution Space Analysis

**Problem:** Listener's Selected profile-profile Plex playlist edits should become prepared, ordinary music-player files automatically, and Sync now should deliver a freshly checked complete selection from the QNAP without an adult running an exporter.

**Key Constraint:** Preserve offline startup and existing HTTP-triggered anonymous FTP delivery, ordinary card paths, host-only Plex credentials and prior-library recovery.

**Working Story:** The existing Python exporter is already the correct source/preparation boundary. Its one-minute poll and container definition are useful foundations; scheduling, freshness semantics and deployment recovery need completion rather than another export system.

**Success Signal:** After a Plex edit, an unattended poll publishes it; an edit immediately followed by Sync now is included or produces a specific preparation failure. A QNAP reboot restores service without manual export. Unchanged sync transfers no audio. Plex outages and interrupted preparation never replace the published selection with an accidental empty library.

**Decision Criteria:** Few moving parts; fresh explicit sync; bounded device waiting; reliable complete publication; unchanged-media reuse; recovery from Plex/NAS/container failure; observable status without credentials.

**Critical Assumptions:** The QNAP can run the existing container dependencies, read the exact Plex music paths, reach the Pod's FTP ports and advertise mDNS on its LAN. The stored token identifies the intended Selected profile user. A fresh preparation normally fits the Pod's three-minute no-data deadline; large first preparations may not.

### Evidence from the current repository

`syncer/server.py` already polls every 60 seconds, advertises `_pearlpod-sync._tcp` on 8787 and accepts `POST /sync`. The caller's address determines the FTP destination. It currently constructs the Plex client before starting HTTP, rejects sync after a publisher error, and reads the current head in the transfer thread without a sync-time source check. Polling and delivery have separate execution paths; only transfers have a lock.

`publisher.py` checks the ordered selection again after preparation and atomically replaces `head.json` only after preparing the catalog. Format 2 uses readable relative paths; host objects stay content-addressed. Preparation fingerprints and profile version support caching. Artwork is currently requested per track on every publication pass, even for a shared album.

`compose.yaml` provides host networking, restart policy, read-only music, persistent cache and a mounted Plex token. Plex URL/source root currently rely on Python defaults. Actual QNAP deployment and its architecture, runtime, access route, paths, permissions and firewall remain unverified. Other ongoing firmware/tracer edits are outside this analysis.

### Candidates Considered

| Option | Level | Approach | Main trade-off |
|---|---|---|---|
| A | Band-Aid | Deploy the current one-minute poll unchanged; Sync now sends the last publication | Smallest effort, but recent edits can be missed, poll failures block sync and startup requires Plex |
| B | Local Optimum | One service and serialized preparation worker; polling warms the cache, Sync now requests a fresh check then pins and delivers that publication | Some scheduler/status work and a bounded wait; preserves the tested transport and export contract |
| C | Reframe | Prepare only when a Pod requests sync; retain the last complete cache | Less background activity, but cold transcodes happen while the Pod waits and keep it on WiFi longer |
| D | Redesign | Separate exporter/job queue and static publication service, with Pod HTTP pull | Separates compute and delivery, but adds deployment components and replaces the proven firmware transport |

All four can deliver prepared media. A assumes freshness delay is acceptable; B tests whether NAS preparation can make explicit sync fresh within the current device budget; C assumes preparation is cheap enough to move entirely onto the user path; D assumes the principal problem is transport/service separation. A and C remain fallback options if measurements favor them. D is deferred until actual concurrency or transport requirements justify it.

### Interpretive Variety Check

- B most preserves the current exporter frame. C tests whether continuous export is necessary at all. D changes the delivery architecture.
- If preparation is routinely slower than the receiver's idle budget, the failure teaches that export readiness must precede opening FTP; it does not justify extending every device wait indefinitely. Revisit the preparation/receiver handshake before changing transport.
- If the QNAP cannot support LAN return FTP or mDNS, test an explicit host address first; only a confirmed transport limitation reopens D.

### Risk Retirement Plan

“Retired by evidence” below is the planned disposition; pending checks are not claims that the risk has already been retired.

| Risk / assumption / alternate frame | Planned disposition | Tempting patch this must fail | Required evidence or rationale | Stop/pivot if |
|---|---|---|---|---|
| Fresh edits at sync time | Retired by evidence | Send the cached head and call it fresh | Edit/reorder immediately after a poll, trigger sync and assert the delivered pinned catalog contains that edit; edit again during preparation and assert no mixed generation | A successful sync silently sends a selection checked before its request |
| Poll and sync races | Retired by evidence | Add another direct `publish()` call in the HTTP handler | Concurrent poll/sync/repeated trigger tests prove one publisher, one delivery, consistent status and no shared temporary-file race | Duplicate preparation, mismatched head/catalog or two transfers |
| Preparation exceeds receiver idle budget | Retired by evidence | Unbounded HTTP request or indefinitely renewed WiFi lease | Slow/cold preparation and blocked Plex tests; bound the refresh phase below the device idle deadline with margin, terminate subprocesses and send a terminal marker on failure | Routine first preparation cannot finish within budget; introduce a prepare-before-receiver flow |
| Plex unavailable at container startup or later | Retired by evidence | Restart-loop the whole service or pretend old cache is fresh | Start/restart with Plex down and a valid persisted publication; HTTP/status stays available, retries back off, last success remains distinguishable from current error, recovery succeeds | Service cannot recover without manual intervention |
| Same-length changed media | Retired by evidence | Trust only `lftp` size comparison | Replace an MP3/lyric fixture with different bytes of equal length; host-side prior catalog identity detects change and delivery forces replacement; unchanged files retain mtimes | Changed bytes are skipped; do not deploy on size-only correctness |
| Source mutation during preparation | Retired by evidence | Check only playlist order twice | Modify source/sidecar/art during a deliberately slow preparation; reject inconsistent candidate and keep prior head; validate identity/fingerprints at the preparation boundaries | A published candidate combines unvalidated source revisions |
| QNAP execution/network/path assumptions | Retired by evidence | Assume the Mac image and mount layout prove NAS support | Verify CPU/runtime, actual music mapping, UID writes, token profile, ffmpeg/lftp imports, mDNS from another LAN machine and return FTP from NAS; recreate/reboot and inspect recovery | Unsupported runtime, wrong profile/path or unreachable receiver |
| Cache grows without bound | Retired by evidence | Delete every object absent from latest head | Set a storage budget; retention tests preserve pinned transfers, current/previous publications and interrupted work while removing only unreferenced cache material | No safe reclaimable space; reject preparation, preserve old publication |
| Polling may be unnecessary | Accepted with rationale | N/A — accepted | One-minute polling is already implemented and warms expensive preparation. Measure unchanged-pass work before adding an event system | Poll work is costly enough to affect NAS use; lengthen interval or reassess C |
| Playback, FAT faults and charging automation | Accepted with rationale | N/A — accepted | Keep documented hardware limitations and manual sync scope. Export polling does not imply scheduled Pod wake or safe unattended card activation | Automatic charging-only sync is proposed without sensing/physical acceptance |

### Recommendation

**Selected:** B — one QNAP service with polling and fresh sync-time preparation.

**Level:** Local Optimum.

Keep the current `POST /sync` request body and immediate 202 response. Queue a bounded refresh in the worker, then deliver its completed generation. Do not hold the HTTP response open for transcodes. A request must receive a source check that starts after the request arrives; a poll already in progress can finish, but cannot by itself satisfy that freshness promise. Coalesce compatible requests while enforcing the existing one-transfer rule. Pin the returned head/catalog for the whole transfer; polling can subsequently publish a newer head without changing that job.

On fresh-check failure, preserve the last successful publication, expose its age and failure separately, and report a terminal preparation error to the Pod. Do not silently fall back to stale delivery. An explicit stale-copy action can be added if household use later calls for it. Support an absent first publication as a preparing/unavailable state rather than crashing the HTTP service. Connect/reconnect to Plex inside the retrying worker. Start with configurable 60-second polling and capped failure backoff; use a monotonic scheduler and bound whole preparation, not merely individual HTTP calls.

The requested format and location already exist: `/cache/head.json`, `/cache/catalogs/<catalog-sha>` and `/cache/objects/` are private host publication storage. The Pod discovers the service and requests delivery; it does not need a mounted NAS directory or a new static-download endpoint. On the card, delivery remains `music/Artist/Album/NN - Title.mp3`, adjacent covers/lyrics, relative `music/Playlists/Name.m3u8`, and hidden `music/.pearl` bookkeeping.

Preparation should check the complete selected membership, metadata, source fingerprints and available sidecars each pass; reuse audio only when its preparation inputs/profile are unchanged. Fetch an album's artwork once per pass and deduplicate preparation. Add bounded artifact retention and startup cleanup for abandoned temporary work. Do not use a playlist update timestamp alone to skip source/lyric/art changes. Do not introduce Plex webhooks, a broker or NAS cron alongside the service's scheduler initially.

**Accepted trade-offs:**

- Explicit sync incurs a fresh-check delay; polling pays most preparation cost in advance.
- Tag/cover changes may replace MP3s because embedding is part of the media contract.
- No media hashing on the Pod. Host-side publication identities and successful transfer outcomes must drive changed-file replacement; device activation keeps length/structure checks.
- Removed selections still leave ordinary card media, as documented; automatic card ownership/deletion is separate work.
- One household Pod/transfer at a time, with interrupted whole-file retry.

### S&T Selection

No Problem Weave or S&T lineage exists for this scope. No synthetic step IDs are introduced. The sufficient selected work is scheduler/freshness orchestration, changed-file correctness, service recovery/status, bounded cache maintenance and verified QNAP deployment. HTTP-pull redesign, event ingestion and scheduled Pod wake remain deferred.

### Execution Handoff

- Preserve: offline boot/playback, ordinary file layout, intended Selected profile `PP:` selection/order/repeats, no device authentication, immutable completed host generations and credentials outside source/logs.
- Verify via: meaningful scheduler/failure/concurrency tests, current host receiver/loader checks and a real QNAP-to-Pod sync after a recent Plex edit, followed by unchanged sync and container recreation.
- Decision criteria: fresh explicit sync, reuse, bounded waiting, low operational complexity and recovery without adult export steps.
- Critical assumptions: NAS runtime/mount/profile/LAN reachability and cold preparation budget; measure rather than infer them from Mac results.
- Accepted trade-offs: fresh-check wait, embedded tag/art replacement, single transfer and no automatic card deletion or charging wake.
- Risk retirement checks: every pending evidence row above is an execution gate; none may disappear in implementation handoff.
- Invalidated if: frequent cold preparations exceed the finite receiver window or NAS networking cannot support the transport.
- Stop/pivot triggers: wrong profile/path, skipped equal-length replacement, mixed publication, cache exhaustion without safe reclamation, receiver-budget failure or loss of offline behavior.
- Needs human verification: Listener sees the expected fresh playlist and can independently sync/use it; physical playback and fault limits remain recorded separately.

## Dissent

**Decision under review:** Extend the existing service and deploy it on QNAP. **Stakes:** Freshness and ordinary-file correctness while keeping the firmware simple. **Confidence before dissent:** Medium.

**Steel-man:** Polling prepares music before Listener requests it; one fresh check catches last-minute edits; existing HTTP/FTP avoids another firmware protocol and places conversion next to the music.

**Contrary evidence:** The current receiver stops after three minutes without incoming bytes, making cold export waiting a real limit. The existing transfer uses size comparison, which cannot prove unchanged bytes at readable paths. Plex construction precedes HTTP startup, so restart policy alone does not yield a usable degraded service. QNAP execution remains unproven.

**Pre-mortem scenarios:**

1. Functional failure: equal-length metadata/media replacement is skipped, leaving yesterday's bytes under today's catalog.
2. Adoption failure: Listener hits Sync now after adding a large album and repeatedly times out during preparation.
3. Opportunity cost: an event queue/static-download redesign consumes work while the existing local transport already meets household needs.

**Hidden assumptions:** A warm cache keeps fresh checks short; validate a cold large selection as well. Size-only reuse is sufficient; reject that assumption with an equal-length replacement test. The QNAP matches local container/network behavior; verify directly before claiming deployment.

**Reconstructed story:** The source boundary and transport remain useful. The weakest assumption is preparation latency within the receiver lifetime. Confidence in size-only replacement decreases to insufficient; host-side change identity is required. The next action is orchestration and adversarial host checks, followed by NAS runtime/network inspection and staged deployment.

**Decision: ADJUST.** Proceed with B after adding bounded refresh failure, changed-file replacement and restart recovery gates. If cold preparation fails the budget, add a preparation phase before opening the receiver. Confidence after dissent is medium until these measurements pass.

## QNAP deployment sequence

1. Use the existing authorized QNAP access route. Inspect CPU architecture, Container Station/Docker/Compose availability, LAN IP, music/Plex path mapping, storage and runtime UID/GID. Do not alter unrelated containers.
2. Make Plex URL, Plex source root, poll interval and advertised address explicit configuration. Keep the Selected profile token in a host-owned restricted runtime file mounted read-only. Persist cache independently of container recreation; music remains read-only. Keep the current host-network deployment only after validating it on this NAS.
3. Build/test the corrected image for the observed NAS architecture, deploy the single service with restart policy and a local HTTP liveness check. Status separately reports source readiness/freshness, last successful publication, current preparation error and transfer state. A Plex outage is degraded readiness, not a reason for endless health-check restarts.
4. Verify the known real selection, correct file paths/art/lyrics, discovery and return FTP. Trigger a recent-edit sync from PearlPod, then unchanged sync. Record publication/transfer duration and bytes. Keep the local service available for rollback but avoid simultaneous conflicting discovery advertisements.
5. Recreate/restart the container and test Plex-down recovery with persistent cache. Document start/update/rollback/status commands and the observed NAS paths without secrets. Record deployment evidence honestly; a running container alone is not end-to-end acceptance.

This document is an exploration and execution handoff. It does not claim that orchestration changes or a QNAP deployment have occurred.
