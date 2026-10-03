# Playlist export and efficient sync — concrete design

This records the earlier exploration. The executed local candidate selects **HTTP trigger → anonymous native FTP → lftp upload**, with no sync authentication, rather than the HTTP-pull proposal below. Selection is the Selected profile profile’s `PP:` playlists. MP3 album-art embedding was subsequently requested, so cover/tag edits change audio file hashes. Interrupted files are retransferred, not range-resumed. The actual managed root is `music/.pearl/`. See [current implementation and acceptance evidence](SYNC-IMPLEMENTATION.md); the historical proposal below is not a claim of implemented behavior. It expands [the solution-space analysis](PLAYLIST-SYNC-SOLUTION.md). The outcome is Listener choosing songs once and finding those ordered playlists on her offline Pod. The binding constraints are reliable playlist-to-file mapping, a bounded MCU memory budget, and preserving playback through incomplete updates.

## What “export” means for Plex

The NAS adapter talks to Plex under Listener's actual user context. An administrator's token plus a title filter is insufficient: two users can have identically named playlists. Setup selects a server, music library and user; include all ordinary audio playlists for that user, or an explicit playlist-ID allowlist. A `POD:` prefix is an optional convention for a shared account, not a required typing task for Listener.

The read path is concrete:

1. Use Python PlexAPI's `PlexServer.playlists(playlistType="audio")` under the selected account. Record server identity, playlist `ratingKey`, title and source update information. IDs determine identity; titles only determine display.
2. Call `playlist.items()` and preserve returned sequence, including repeated tracks. Do not sort by track number or remove duplicate playlist entries. Expand all pages through the client and verify the actual order against the app before choosing this adapter.
3. For each track, reload media details as needed, choose the explicitly selected/local media part and read `MediaPart.file`. Translate the Plex container root through a configured mount map, for example `/music/...` → `/nas-library/...`. Resolve real paths, require containment in the read-only music mount, and fail on unavailable or ambiguous parts. Never resolve solely by artist/title or basename.
4. Fetch metadata and artwork into the NAS cache. Playlist artwork can use Plex's playlist image/composite; album art is prepared separately. Validate that the selected account is entitled to every member. No original NAS path or household Plex credential goes into the Pod catalog.
5. Read the playlist membership/update information again after preparation; if it changed, abandon this candidate and retry within a bound. This catches ordinary edits during collection, rather than presenting a mixture of two list orders. It is not a claim that Plex provides a transactional library snapshot.
6. Publish only after every selected entry has a valid prepared asset. A missing file produces a named export error and retains the prior published selection. An empty or unavailable API response is not an instruction to delete Listener's music; an intentional clear/delete must be confirmed by a successful source snapshot.

The service polls at a proposed one-to-five-minute interval. Manual Pod sync reads the latest completed publication and displays its source age; it does not force a new Plex snapshot in the first version. It can update metadata and selection while caching previously prepared audio. Smart playlists and streaming-only entries are excluded from the first version; they need explicit snapshot/availability semantics later.

These are documented capabilities of the **third-party Python PlexAPI project**, not an official Plex API stability promise: [server playlists](https://python-plexapi.readthedocs.io/en/latest/modules/server.html#plexapi.server.PlexServer.playlists), [playlist items](https://python-plexapi.readthedocs.io/en/latest/modules/playlist.html#plexapi.playlist.Playlist.items), [media file paths](https://python-plexapi.readthedocs.io/en/latest/modules/media.html#plexapi.media.MediaPart), [home-user switching](https://python-plexapi.readthedocs.io/en/latest/modules/myplex.html#plexapi.myplex.MyPlexAccount.switchHomeUser). Household account access, ordering and the mount map still require a live test.

## What “export” means for Roon

Roon has a supported folder-export route: on a computer, open a local-file playlist, choose its context menu's Export action and export to a folder. Roon copies local audio into artist/album subfolders, writes tags as displayed in Roon, and creates an M3U under `Playlists/` referring to the copies. Its documentation says export is available on computers/laptops, not phones/tablets. [Roon's current export documentation](https://help.roonlabs.com/portal/en/kb/articles/export).

A proposed registered feed `roon-export:hero-training` looks like this. The paths are examples, not discovered NAS paths:

```text
/pearlpod/staging/hero-training-job-0042/
  Artist/Album/01 Song.flac
  Artist/Album/02 Song.flac
  Playlists/Hero Training.m3u
```

After Roon reports completion, move that **fresh, completed folder** into `/pearlpod/inbox/` on the same NAS filesystem, or mark it ready using a small publisher interface. Do not re-export into a live published directory: renamed playlists, stale files and partially rewritten tracks become indistinguishable there. The importer freezes a private candidate, reads the M3U in order, resolves its entries within that export root, validates every listed file, and passes the result through the same preparation/publishing pipeline as Plex. Its registered feed ID stays stable if the displayed playlist name changes. The intake job may later be removed; it is not the delivery cache.

The copy made by Roon stays on the NAS side. The Pod receives only missing delivery objects, even when a later Roon export copies the whole playlist again. Only listed tracks are imported; leftover files never mean extra selected songs. Exported/embedded artwork is usable when present. Custom art that exists only in Roon is not assumed to be included: use an explicit image beside the feed or a generated collage until an image-API mapping is verified.

**Freshness matters:** unattended Pod sync can fetch the newest completed Roon export; it does not automatically export edits made inside Roon. I found no documented native automated folder-export/file-path API in the published extension interfaces inspected. Roon's official browse service returns display-oriented items and browse keys, with paginated list loading, rather than a documented original-file-path contract. That is evidence for a gap in this approach, not proof that no private/internal mechanism exists. [Official browse source and item definitions](https://github.com/RoonLabs/node-roon-api-browse/blob/master/lib.js).

There are three distinct Roon choices:

| Route | Recurring action | Consequence |
|---|---|---|
| Roon is the author; completed folder export is the feed | Export again after editing, from a computer | Supported path, exact copied-file mapping; the Pod can automate delivery but cannot promise fresh unexported edits |
| External M3U is the author; Roon imports it | Edit through another curator, publisher reads the M3U automatically | Can automate NAS-to-Pod publication; editing the Roon copy is not assumed to write back to that M3U |
| Explore an automatic Roon extension/private export integration | First prove ordered membership **and exact original file identity** | Research branch only; title matching is not an acceptable substitute for edition/version mapping |

Roon documents [M3U import](https://help.roonlabs.com/portal/en/kb/articles/importing-playlists). The official API's [discovery, pairing and authorization patterns](https://github.com/RoonLabs/node-roon-api) can be reused conceptually from the roon-knob family for a future adapter, but do not fill the file-identity gap. No copyrighted roon-knob source is needed for this design.

## Common export product

Both inputs yield a profile-specific published selection: distinct logical tracks, album identity/metadata, ordered playlist entries and separate artwork objects. Two levels of identity are deliberate:

- **Source identity:** a namespaced playlist/track identity, such as Plex server + playlist `ratingKey`, or the registered Roon-export feed. It supports renaming and source reconciliation.
- **Delivery identity:** SHA-256 of the actual prepared object bytes. It determines the immutable URL, cache entry and whether the Pod already has those exact bytes. Multiple playlists reference one audio object; repeated playlist entries still play repeatedly.

The NAS owns all file mapping, format work and image processing. A proposed compatible output profile is: preserve supported MP3/FLAC compressed audio where appropriate; convert WAV, unsupported audio and unnecessarily large high-resolution material on the NAS to 16-bit/48 kHz FLAC. The current player outputs a 16-bit-derived, 48 kHz signal, so native high-resolution delivery is not a current benefit. A compact MP3 profile can be selected if card capacity or measured transfer time demands it; that quality trade-off is explicit.

Metadata and artwork are separate from audio in the managed catalog. Playlist title/order/cover edits never force audio to change. Roon retags exported copies, which can change a whole-file hash even if the sound is unchanged. Avoid claiming those bytes are a new song: normalize non-audio tags in a deterministic stream-copy preparation step and carry the new metadata in the catalog. Cache the resulting object. Verify repeated exports/retag-only edits against the output profile; if normalization is not deterministic, byte-level deduplication remains correct but loses efficiency. A profile/toolchain change may legitimately produce new assets. Already-lossy MP3 should not be re-encoded simply to change tags. FFmpeg documents stream-copy and metadata mapping separately; this supports the proposed preparation mechanism without implying that every container/profile is deterministic. [FFmpeg stream-copy and metadata documentation](https://ffmpeg.org/ffmpeg.html#Streamcopy).

Source files are never modified. The NAS cache index uses source identity, file stat information and preparation-profile version to avoid unnecessary work, but a changed file is hashed/validated before cache reuse. Timestamps alone are not content identity; mutation during preparation invalidates that candidate. Exact duplicate delivered bytes across sources may share one object; no promise is made to merge two different encodings of the same performance.

Prepare 240×240 RGB565 album/playlist artwork using the existing `tools/prepare_card.py` byte order (115,200 bytes) and, when the managed UI supports it, 64×64 thumbnails (8,192 bytes). Reuse art by hash. Names and ordered membership reside in catalog records, not mutable audio paths.

## Transport reuse decision

**Selection reopened by fact checking:** [SYNC-REUSE-FACT-CHECK.md](SYNC-REUSE-FACT-CHECK.md) documents an omitted directly relevant precedent: FrameFi uses NAS/host-side lftp mirroring to an ESP32-S3 FTP server. Prioritize evaluating existing native ESP-IDF FTP reception plus single-session lftp before building a custom transfer client. Keep all Plex account/library/export logic on the NAS. HTTP pull remains a comparison candidate; its detailed algorithm below is a proposal, not a settled transport choice.

For HTTP pull, ESP-IDF v5.5.5 already supplies streaming reads, header control and HTTP authentication; it supplies the transport, not a complete directory-sync engine. An existing NAS static server or `rclone serve http` supplies file serving. We still implement a bounded manifest reader, verified local inventory, download resume and complete-generation activation. The catalog is application data; the network transfer uses standard HTTP. [ESP-IDF HTTP client](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/protocols/esp_http_client.html), [rclone HTTP serving](https://rclone.org/commands/rclone_serve_http/).

| Candidate | Reuse | Remaining work / disposition |
|---|---|---|
| NAS lftp → native ESP-IDF FTP receiver | Existing directory-mirror tool and FATFS/SDMMC ESP32-S3 server example; FrameFi demonstrates this pattern | First reuse prototype to evaluate: restricted staging, runtime credentials, integrity/activation, bounded wake coordination; interrupted upload and memory behavior remain unverified |
| Static NAS files → native HTTP pull | Existing NAS server and ESP-IDF client | Comparison proposal: manifest/inventory, bounded resume, integrity and safe activation; Pod controls wake/session duration |
| NAS WebDAV → Pod pull | Existing NAS WebDAV service, ordinary GET for known paths | Compatible hosting option; PROPFIND/XML directory traversal adds work without replacing the sync/activation layer, so use the same manifest |
| NAS rclone → Pod WebDAV server | rclone's file-level comparison/copy and an existing ESP-IDF WebDAV server component | Viable alternate prototype if off-the-shelf transfer orchestration matters most; add authentication, restricted staging, verification and activation; coordinating an intermittently awake receiver is also required |
| ESP-Sync on Pod | Existing small ESP32 serial/SPIFFS file-sync library and Python companion | Real embedded sync code, but WiFi/SDMMC adaptation and lifting its approximately 16 MiB payload cap are required |
| openrsync/librsync on Pod | Existing C wire-protocol implementation / separate streaming delta library | Port candidates: openrsync has real daemon interoperability but UNIX filesystem assumptions; librsync has neither wire protocol nor directory/network handling; no ready ESP-IDF build established |
| Syncthing/zsync on Pod | Existing protocols and desktop implementations | Deferred: no ready ESP-IDF complete engine established in this bounded search; integration/porting is not free, and block deltas buy little for immutable audio objects |

The WebDAV-push alternative is concrete, not dismissed as impossible: [ErikMeinders/webdav](https://github.com/ErikMeinders/webdav) supports VFS-backed streamed PUT/GET and WebDAV directory operations. Its documented limitations include no authentication/TLS, no enforced locking and unpaginated listings; it has not been validated on this player. Standard PUT does not establish resumable uploads. A prototype would use `rclone copy` into restricted managed staging, serialize transfers, and submit a completed manifest only after uploads succeed. The Pod must verify it before activation. `copy` skips identical files without deleting destination files; generic WebDAV comparison capabilities vary and do not provide a portable checksum contract, so comparison alone cannot prove a generation safe. A same-size corrupt object must not be silently accepted or endlessly skipped. Do not point `rclone sync` at the live card. [rclone copy](https://rclone.org/commands/rclone_copy/), [WebDAV backend limitations](https://rclone.org/webdav/).

File-level incremental transfer is the requirement: unchanged audio stays on the card, reorder/name edits change small metadata files, and newly selected songs download once. Within-file block deltas are a separate optimization. [Syncthing BEP](https://docs.syncthing.net/specs/bep-v1.html) includes device authentication, indexes and block requests; [zsync](https://zsync.moria.org.uk/) supplies per-file HTTP block-delta retrieval, not a complete playlist/directory activation layer.

Risk-retirement gate for HTTP pull: test the exact NAS server with authentication, conditional requests, immutable validators and Range responses (including ignored Range), then measure bounded MCU memory and interrupted download recovery. For FTP reception, test single-session lftp compatibility, interrupted/corrupt upload repair, restricted paths, full-card handling, runtime memory and radio-off reclamation; remove the example's credential logging and bound its path handling. Rclone's documented connection requirements need a separate compatibility strategy for a single-session server. For the WebDAV alternative, additionally demonstrate rclone compatibility, same-size corruption repair, interrupted PUT behavior, staging confinement and bounded listing/stack use. Compare measured integration cost and transfer reliability before selecting; do not treat an available component as device validation.

## HTTP boundary and wire behavior

Static publication replaces the earlier proposed refresh/job REST API in the first version. Paths are examples, not installed URLs:

| Request | Purpose |
|---|---|
| `GET /pearl/head.json` with `If-None-Match` | Small current-generation descriptor; `304` means no changed descriptor |
| `GET /pearl/generations/<catalog-sha>/catalog.ndjson` | Immutable catalog, byte length and SHA-256 given by head |
| `GET /pearl/objects/<full-sha>.<ext>` | Immutable audio/art/playlist object; strong ETag and byte Range support |

`head` contains format/profile version, catalog hash, byte size, immutable catalog URL and last successful source-check time. Updating that freshness timestamp may change the descriptor's ETag; the Pod also compares the catalog hash and skips catalog/audio transfer when that hash is unchanged. Generation identity changes only for semantic selection/metadata/asset changes. The NAS writes a candidate catalog then publishes its head only after the object set is complete. An explicit empty selection is valid; a failed source read is an export error, not an empty success. Retain prior publications long enough for a sleeping/interrupted Pod to finish its pinned generation.

A catalog is streamed NDJSON with bounded records: header, album, track, object and playlist records. Playlist **membership** is a separate ordered M3U8 or entry stream, so a long playlist is not one giant JSON line. Records refer to objects by full hash and expected size. One track can appear in many lists, and a membership stream can reference it repeatedly. The format has explicit version/count/size limits; unknown major versions, escaping paths, duplicate conflicting IDs and unresolved object references reject the entire candidate. Playback paths are derived from validated hash identifiers and a fixed managed root, not arbitrary server-provided absolute paths.

The first transport is scoped to the home LAN and a per-device delivery credential; the household Plex token stays on the NAS. The delivery account can read only its prepared selection. Plain HTTP does not provide confidentiality; if TLS is required, validate the memory/stack budget before accepting it. The device stores the configured NAS address instead of probing every service on boot. Keep-alive avoids an unnecessary connection per file. No archive of the entire library, rsync or Syncthing daemon runs on the MCU.

HTTP conditional requests and resumable ranges use standard semantics, not a custom delta algorithm. [RFC 9110: conditional requests and Range](https://www.rfc-editor.org/rfc/rfc9110.html).

## Metadata, artwork and lyrics contract

The exporter must satisfy [EXPORT-MEDIA-CONTRACT.md](EXPORT-MEDIA-CONTRACT.md), including filesystem compatibility, genre preservation, explicit artwork/lyric associations and parser round-trip checks. Transport success alone is insufficient. Genre and lyric display require firmware changes; the current scanner does not implement them.

## Device algorithm and card layout

The existing firmware scans `/sdcard/music` recursively. A managed subtree must be excluded from that ordinary scan, then loaded from **only the active generation**. Otherwise staged files, previous playlists and orphaned tracks appear as current music. This is a required firmware change, not an existing feature.

```text
/sdcard/music/                    existing manually copied music
/sdcard/music/_pearl/objects/     immutable prepared audio/art objects
/sdcard/music/_pearl/generations/<catalog-sha>/
  catalog.ndjson
  playlists/<playlist-id>.m3u8
/sdcard/.pearl-sync/partial/      resumable .part files and checkpoints
ESP NVS: activation record       active + previous catalog hashes, format version
```

M3U8 paths stay within `/sdcard/music`, matching the current playlist parser's containment rule. Managed catalog metadata supplies display titles and album identities; it does not derive them from opaque hash filenames. The generation loader builds the current `pearl_library` model, so audio playback can use existing file paths. Manually copied albums/playlists remain present and unmanaged.

1. Start networking only for Sync now or an authorized scheduled sync. Bound connection failures. A missing NAS leaves existing music playable.
2. Conditional GET of head. On `304` or an unchanged catalog hash, stop networking: no audio scan or transfer. Record source freshness from a changed descriptor. On a new catalog, download/verify it in bounded buffers and validate all references before altering active state.
3. Compare required object hashes/sizes with the local **verified-object inventory**, not just the last generation: an older retained object can satisfy a newly re-added song. Deduplicate physical objects, not playlist entries. Check presence and expected size; suspicious/unverified files require a local hash check. Do not send one HEAD request per track.
4. Preflight free space for **only missing objects**, new catalog/membership files, existing partials and a reserve. Keep the current and previous generations' referenced assets intact. If the delta will not fit, fail before downloading; do not delete the current selection to make room. Historical unreferenced managed objects may be reclaimed without touching manual music or either retained generation.
5. Download sequentially with a proposed 16 KiB heap/PSRAM buffer, small stack frames and incremental SHA-256. Write a `.part`, checkpoint verified length periodically, and resume with `Range: bytes=<offset>-` plus `If-Range` for that immutable object's strong ETag. Validate status, Content-Range and expected total. If the server returns `200` to a resume request, restart that **partial** from zero; never append a full response. Treat `416` by verifying an apparently complete partial or restarting, rather than assuming success.
6. Rehash the stored prefix on resumed transfers rather than trusting a volatile hash context. Require final size and full SHA-256; flush/fsync and close before registering an object as verified. Promote the partial only after verification. Cancellation preserves a useful checkpoint; a corrupt download retries within a bound.
7. Build and validate the complete next catalog before activation. Downloads may run while music plays, subject to measured SD contention. **Activate when playback is paused/stopped** initially; manual sync can report “Downloaded — pause to update.” The current detach/attach path closes the decoder and resets position; sample-accurate continuation is not silently assumed.
8. Quiesce artwork/library references, acquire the existing library lock, persist an activation record containing active and previous hashes, swap the in-memory catalog, then render the new lists. NVS write failure retains the old activation. Do not implement activation as several independent active-pointer keys. [ESP-IDF NVS documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/storage/nvs_flash.html).
9. Retain the previous valid generation and its assets as rollback protection. On boot, verify activation/catalog structure/hash and required file sizes; choose the previous generation if the new one is incomplete. Network contact is absent from boot. Full multi-gigabyte hashing on every boot would violate the startup aim; periodic/local integrity audits are a separate policy.
10. Garbage-collect only objects absent from both retained generations and any pending transfer, after successful activation. Removing a song from one list does not delete it while another list references it. Turn WiFi off after completion/failure/cancellation; wake the UI with last-sync status on ordinary use.

NVS plus immutable generations is a logical commit/rollback design, **not a claim that FAT32 or a microSD controller becomes power-loss transactional**. Real cuts during data writes, directory updates, fsync, activation and GC must establish recovery behavior. Card-wide corruption remains possible and may require reformatting/restoring the cache. No automatic sync should ship on the strength of rename semantics alone.

## Transfer examples

Illustrative sizes, not measured network throughput: 100 distinct tracks averaging 8 MiB occupy 800 MiB. Suppose Listener adds five playlist entries, two referring to songs already on the card:

| Edit | Audio transferred | Other changes |
|---|---|---|
| No published change | 0 | Conditional head response |
| Reorder or rename a list | 0 | New catalog/membership bytes |
| Add an already present song to another list | 0 | New membership |
| Add five entries, two already present | 3 × 8 MiB = **24 MiB** | Catalog/membership + missing artwork |
| Remove entries | 0 | New membership; later reference-safe GC |
| New 240px cover | 0 | 112.5 KiB RGB565 object, optionally an 8 KiB thumbnail |
| Interrupted after 5 MiB of an 8 MiB object | About 3 MiB, plus checkpoint rollback if needed | Local prefix rehash; final full checksum |

An addition-only update needs roughly 24 MiB plus metadata/reserve in free space, not another copy of the existing 800 MiB. Replacing every track can still require space comparable to a full selection because the old generation is retained. That is the capacity cost of rollback; it must be visible in preflight. Resume and no-op behavior are independent of whether the source was Plex or a Roon export.

## Manual and nightly behavior

**Sync now** reads the newest completed NAS publication, displays source age and progress, then downloads the delta. Proposed result states are Up to date, Downloading N of M, Ready to update, Synced, and a specific recoverable error. The one-to-five-minute publisher interval is an explicit freshness trade-off; show when the source was last checked. Source age must also be shown for Roon, so “synced” does not imply that unexported edits were seen. An immediate-refresh API can be added later if the observed delay interferes with Listener's use.

The NAS publication schedule and the Pod wake schedule are separate. The publisher can prepare changes continuously; the Pod wakes once in the configured overnight window, verifies external power, connects, reads a prepared snapshot, and sleeps again after a finite session. Proposed limits are 20 seconds per WiFi attempt, bounded retries, a 15-minute device transfer budget and an idle-transfer deadline; useful partials survive to the next session. If a measured first library load needs longer, the manual UI offers an explicit longer session rather than making every overnight wake unbounded. Sync uses its own bounded station-session lease, without starting the setup AP. An explicit longer manual session must also extend that lease; progress does not silently renew it forever. Changing source material mid-transfer does not mix revisions: finish or abandon the pinned catalog, then check latest on a later bounded pass.

USB host presence is already available; a truthful wall-charging signal is not established. Timer wake plus an explicit dock mode is a possible interim intent signal, not charger detection. Charging-only automation remains gated on verified hardware sensing; manual sync can be built independently. No automation or nightly job is created by this design document.

## Revised selection and dissent

**Selected source boundary:** Plex adapter on the NAS with generic prepared files; completed Roon export remains a supported manual-input proposal. **Transport reopened:** evaluate native ESP-IDF FTP reception plus lftp first, compare against the detailed HTTP-pull proposal, and retain rclone/WebDAV as another standard-reuse route. **Deferred:** custom refresh/job APIs, block-delta engines and automatic Roon extraction until exact file identity/export can be proven; choosing an outside-Roon M3U author remains a real alternative if automatic freshness is essential. **Rejected for the first version:** copying the live directory, a whole-selection ZIP, title-only Roon-to-NAS matching, a Syncthing daemon on the ESP32, and treating an HTTP error as an empty library.

Dissent changes the earlier recommendation in three ways: the NAS bridge does not solve Roon freshness; a manifest/catalog loader is required rather than blindly rescanning a managed directory; and logical activation is insufficient evidence for FAT power-loss safety. Functional failure could come from partial publication or ambiguous media matching. Adoption failure could come from Listener having to wait for an adult to export Roon. Opportunity cost could come from spending weeks reverse-engineering Roon instead of validating her Plexamp workflow or a tiny curator. The weakest assumption remains household authoring/file access, followed by actual storage fault tolerance.

**Decision: ADJUST.** Prototype the export boundary first and measure transfer/recovery behavior before freezing a protocol or adding scheduled wake. This couples neither source identity nor Plex credentials to the MCU. The following checks are explicit execution gates:

| Assumption/risk | Required disposition and adversarial evidence | Tempting patch this must fail | Pivot condition |
|---|---|---|---|
| Plex author/profile/order | Retire by a real Listener-profile list with repeats, two same-title user playlists and multiple media versions | Administrator title filter | Wrong user or unavailable/ambiguous part |
| Roon intake | Retire by a fresh real export: validate exact paths/order/repeats, rename under stable feed ID, reject partial bundle and out-of-root paths | Copying an entire reused export directory | Export cannot preserve selection or requires excessive adult intervention |
| Automatic Roon | Accepted as deferred until API/private integration proves exact file identity and freshness | Artist/title matching | No credible exact-mapping evidence; use manual export or external M3U author |
| Deterministic preparation | Retire via unchanged export, tag-only edit, art-only edit, encoder/profile update and source mutation during preparation | Raw mtime as identity | Same audio needlessly regenerates or a different edition maps to the same track |
| Efficient transfer | Retire with packet/byte accounting for no-op, reorder, duplicate/shared song, partial resume, server ignoring Range and mismatched ETag | Redownload every file, append a `200` response to a partial | Incorrect bytes or pathological requests; revise protocol |
| Storage activation | Retire with real cuts at each write/fsync/commit/GC boundary, full card and corrupt catalog; old/previous selection must remain recoverable within card limitations | “Rename is atomic” or delete old to make room | Missing active tracks, boot loop or accidental manual-file deletion |
| Bounded MCU behavior | Retire with hostile/huge catalog, OOM, repeated sync and SD contention during music; small frame warnings and runtime stack/heap telemetry | Whole-manifest stack allocation | Audio starvation, memory growth, crash or network-dependent boot |
| Nightly power | Retire through actual charger/host/battery/unplug observations | USB connected means charging | No truthful signal: manual sync/explicit dock mode |

No Problem Weave/S&T lineage exists for this scope, so no artificial step IDs are created. The selected set is sufficient for a source-to-card prototype; safe activation and power sensing are required before automatic scheduled operation. The original analysis's risk plan remains applicable, strengthened by the checks above.

## Concrete next execution

1. On the NAS, test one real Plex child-profile playlist and one completed Roon export against original files. Report each ordered entry's exact resolved source, output profile, bytes, delivery hash and errors without modifying either source.
2. Produce a prepared generation and dry-run inventory diff. Demonstrate zero audio transfer for reorder/rename, one stored object for shared/repeated songs, and only new objects for additions.
3. Implement the bounded download/inventory and active-generation loader together. Validate the transfer bytes, interrupted resume, disk-full behavior and existing manual library before enabling activation on the device.
4. Fault-inject storage and measure startup, peak heap/stack and playback contention. Manual sync is the first delivered flow; nightly wake follows the charger evidence gate.

This exploration updates the proposal and evidence requirements. No publisher, account, scheduled job or firmware was deployed or flashed in this turn.

## Bounded host evidence from this exploration

A local synthetic two-second 44.1 kHz stereo fixture was created separately in FLAC and MP3. For each format, two copies received different title tags through stream-copy. Both were normalized with the command below. The pair converged to identical output bytes for each format, and decoded signed-16-bit PCM matched the original fixture exactly. Resulting objects were 33,183 bytes (FLAC) and 33,061 bytes (MP3). This verifies the proposed tag-normalization mechanism on those fixtures; it does not verify real Roon exports, every encoder/tag type, household access or network/storage recovery.

```sh
ffmpeg -i tagged.flac -map 0:a:0 -c:a copy -map_metadata -1 -map_chapters -1 delivery.flac
# The same options were exercised with .mp3 input/output.
```

Runtime: `ffmpeg version 7.0 Copyright (c) 2000-2024 the FFmpeg developers`. Fixture files were generated in a temporary directory; no NAS or device files were touched. Transfer arithmetic was also checked independently: repeated/shared entries retained in a 106-entry fixture refer to 103 distinct tracks; compared with 100 existing objects, only three new 8 MiB objects (24 MiB) are needed. These are bounded host checks, not a throughput benchmark or a functioning end-to-end sync implementation.
