# Solution Space

The concrete source adapters, wire requests, card layout, resume/activation algorithm and revised dissent are in [PLAYLIST-EXPORT-SYNC-DESIGN.md](PLAYLIST-EXPORT-SYNC-DESIGN.md). That expansion makes Roon export freshness, manifest-aware scanning and FAT32 fault testing explicit.

## Solution Space Analysis

**Problem:** Listener should independently curate music from the NAS and find those same playlists, in the same order, ready for offline playback on PearlPod.

**Key constraint:** Reliable, battery-conscious synchronization on an ESP32-S3/FAT32 device must preserve a playable library through interrupted transfers, unavailable WiFi and full cards.

**Working story:** An existing music app handles discovery and curation; a NAS-side bridge publishes a small, complete offline selection; the Pod pulls it. The device remains independent of any particular library server.

**Success signal:** Listener creates or edits a playlist, charges the Pod overnight or taps Sync now, and finds the changed playlist and artwork on the Pod without an adult exporting files. Removing WiFi afterward does not affect playback.

**Decision criteria:** Listener's ability to curate independently; existing household services; playlist/profile correctness; reliability on the MCU; NAS maintenance burden; capacity and transfer cost; reversible integration.

**Critical assumptions:** Listener has a suitable phone/tablet/computer for the authoring app; her Plex profile can create ordinary audio playlists and the bridge can retrieve them under that profile; selected music is accessible as local NAS files; reliable external-power detection can be established before charging-only scheduling is enabled. None of those household/hardware assumptions has been live-verified in this exploration.

## Candidates Considered

| Option | Level | Approach | Main trade-off |
|---|---|---|---|
| A: Plexamp + NAS bridge | Reframe | Listener curates in Plexamp; bridge reads her Plex playlists and publishes music, artwork and M3U8 for Pod pull-sync | Small service to maintain; Plex user/API behavior must be verified |
| B: Exported playlist files | Band-Aid / status quo | Roon exports files + M3U, or a UPnP controller writes M3U for MinimServer; export folder becomes sync source | Simple file boundary, but recurring adult/manual export unless authoring supports automatic persistence |
| C: Lyrion/LMS + bridge | Local optimum | Add LMS, curate through its web/controller UI, consume saved playlists | Additional server/library scan and another UI; no present evidence Listener prefers it |
| D: Listener-specific curator | Redesign | A small NAS web app offers search, album art, playlist editing and explicit “On my Pod” selection | Complete control over child UX, but we own search, identity, editing and accessibility |

A direct Plex client on the ESP32 is a competing implementation of A: it removes the bridge but puts Plex authentication, pagination, library quirks, format conversion and image processing into firmware. It is pruned initially because the bridge keeps credentials and heavy work off the MCU and leaves the sync boundary reusable for B/C/D. This is a reversible prototype choice, not a frozen public protocol.

Roon officially exports a playlist's files and an M3U together. That makes B a credible bootstrap; it does not establish a supported unattended export pipeline. [Roon export documentation](https://help.roonlabs.com/portal/en/kb/articles/export).

MinimServer consumes local playlists and documents playlist cover-image conventions; a suitable controller/editor still has to author those files. [MinimServer library guide](https://minimserver.com/ug-library.html). Lyrion documents playlist save/load operations and M3U support. [Lyrion CLI reference](https://preview.lyrion.org/reference/cli/playlist/).

The Python PlexAPI project's own documentation exposes playlist item enumeration, home-user switching and media-part disk paths. It is a third-party client project, not an official Plex API stability promise. [Playlists](https://python-plexapi.readthedocs.io/en/latest/modules/playlist.html), [home users](https://python-plexapi.readthedocs.io/en/latest/modules/myplex.html), [media files](https://python-plexapi.readthedocs.io/en/latest/modules/media.html). Plex describes Plexamp as its app for personal server music and offline playlists; those in-app downloads should not be assumed to be an export contract for another device. [Plexamp](https://www.plex.tv/plexamp/).

## Interpretive Variety Check

A assumes Listener is happy curating in an existing music app and tests that cheaply. B assumes file export is acceptable, exposing the cost of adult involvement. C tests a different existing ecosystem. D challenges the app-first assumption and optimizes the task of choosing music for this specific device. If Listener struggles with Plexamp, improving the exporter will not solve the problem; test D's small curation prototype before adding another server.

## Risk Retirement Plan

“Retired by evidence” below is the required execution disposition, not a claim that a live check already passed.

| Risk / assumption / alternate frame | Planned disposition | Tempting patch this must fail | Required evidence or rationale | Stop/pivot if |
|---|---|---|---|---|
| Wrong Plex user or inaccessible child playlists | Retired by evidence | Read administrator playlists and filter titles | Create/edit under Listener's actual profile; verify retrieval excludes an identically named adult playlist | Profile cannot author or bridge cannot retrieve reliably; use explicit shared-family convention or D |
| Curation app is too cumbersome | Accepted with rationale for technical prototype; needs human observation | Treat functioning API as successful independence | Listener creates, reorders and renames a short playlist herself | She needs repeated adult intervention; test D |
| NAS paths differ from Plex container paths | Retired by evidence | Match songs by artist/title/basename | Resolve actual media-part paths through an explicit mount map; test duplicate titles, versions, Unicode and renamed files | Mapping is ambiguous or files inaccessible |
| Playlist order/duplicates and shared tracks | Retired by evidence | Sort/deduplicate playlist entries | Round-trip a reordered list with a repeated song and a song shared by two lists; preserve entries, store audio once | Actual exported order differs from app |
| Interrupted update / full SD / FAT recovery | Retired by evidence | Download over live files and replace M3U immediately | Cut power during file, playlist and catalog activation; corrupt bytes; fill card; boot into a complete old/new snapshot with prior music intact | Recovery yields missing tracks or catalog corruption; improve storage transaction scheme before autosync |
| Charging detection | Retired by evidence | Treat USB host presence as wall charging, or wake nightly on battery | Identify a safe VBUS/charger-status signal; distinguish wall charger, USB host and battery, including unplug during transfer | No reliable signal: explicit dock mode or manual sync first |
| Capacity, unsupported audio and MCU memory | Retired by evidence | Skip missing songs silently or stream whole downloads into RAM | Mixed codecs/high-resolution/artwork and over-capacity selection; bounded chunked downloads; explicit unavailable-track report | Required selection exceeds capacity or transfer crashes; negotiate smaller selection/profile |
| Plex credentials/API evolution | Accepted with rationale, isolated in NAS adapter | Store household Plex token on Pod | NAS-only secret; Pod-specific read-only delivery credential; adapter smoke check on updates | Adapter becomes expensive/unreliable; retain M3U publisher boundary and switch source |
| Streaming-only tracks and smart lists | Accepted with rationale: start with ordinary local-file playlists | Promise automatic offline export of every app queue | Clear local-files-only scope; evaluate smart lists into finite snapshots only in a later phase | Listener mainly chooses content with no local file; reframe source availability |
| NAS/WiFi outage | Retired by evidence | Retry indefinitely or erase lists when server returns empty/error | Unavailable server/auth failure/incomplete manifest leave old library intact; retries/time bounded, radio off afterward | Offline boot/playback waits for networking |

## Recommendation

**Selected:** A — Plexamp authoring with a NAS publisher and a generic Pod pull client.

**Level:** Reframe: synchronize Listener's chosen music rather than replicate the NAS library.

Start with her ordinary local-music playlists. Prefer automatically including playlists in her dedicated profile; this avoids a naming chore. If using a shared family profile, opt in with a prefix such as `POD: Hero Training`, stripped from the displayed Pod name. Identify lists by source IDs, not their mutable titles. A setting may narrow the selection later if capacity demands it.

Proposed flow:

1. A small NAS service snapshots the selected playlists every proposed one-to-five minutes, preserving order and repeated entries. Manual Pod sync reads the newest completed publication and shows source freshness; immediate source refresh is deferred to keep the first version free of a custom job API.
2. The publisher resolves original NAS files, copies or converts only selected music into a delivery cache, prepares suitable artwork, and generates relative-path M3U8 plus a checksummed, versioned manifest. Shared tracks use one stored asset. Supported MP3/FLAC/WAV can normally pass through; unsupported formats are converted on the NAS. Exact output profiles remain a prototype decision.
3. PearlPod's **Sync now** downloads missing/changed assets in bounded chunks, verifies them, and activates a complete library generation. Existing playlists/playback remain available until activation; any brief playback pause needed for catalog swap is explicit. It never writes over an active audio file. A successful rescan exposes the playlists in the existing Playlists view.
4. Each list has its own name and, after a small UI extension, cover image; current firmware already preserves playlist order and duplicate entries, but dedicated playlist-cover lookup needs implementation.
5. The same sync engine runs nightly while externally powered, once charger detection is proven. Use a deep-sleep timer to wake near the configured local window, check power, sync with a finite budget, then turn WiFi off and sleep. Recheck power during transfer. Keep the screen dark and show last-sync/result on the next normal wake. If reliable sensing is unavailable, use an explicit “charging tonight” dock mode as an opt-in interim policy, clearly labeled as intent rather than detection.

No WebDAV requirement returns here: an existing authenticated static HTTP server is sufficient, using ESP-IDF's native HTTP client on the Pod. NAS WebDAV can also host the same files via GET. NAS rclone → Pod WebDAV is a credible alternate for reusing file-copy orchestration, but still requires restricted staging, authentication, verification and complete-generation activation on the device; see the transport reuse decision in the expanded design. NAS preparation can run regardless of whether the Pod is online; device-side scheduling and NAS publication are separate. Retain manually copied music; only garbage-collect managed assets after successful activation and only when no active playlist references them. Reserve staging/recovery space and fail clearly if it is insufficient.

**Accepted trade-offs:** One small NAS service and its Plex adapter; separate storage for the prepared delivery selection; initial local-file/ordinary-playlist scope; charging-only scheduling gated on hardware evidence. Plex identity details stay in the adapter, with an interchangeable M3U publication boundary.

## S&T Selection

No existing Problem Weave or S&T lineage was found for this scope. No step IDs are invented. The sufficient prototype is one real child-profile playlist through the NAS exporter to the existing Pod M3U8 reader. Scheduling and graphical playlist covers follow only after that round trip and interrupted-transfer checks pass. This exploration records the recommendation and execution handoff; it does not deploy firmware or a server.

## Execution Handoff

- Preserve: fast offline boot, music without networking, list order/repeats, current album/folder/manual-card behavior, private source/local builds, no Plex secrets on the Pod and no changes to the source NAS library.
- Verify via: Listener-created list and subsequent rename/reorder/add/remove reflected on a disconnected Pod; power-cut/full-card recovery; bounded retries and successful radio-off memory recovery.
- Decision criteria: independent curation, correctness, low MCU complexity, safe updates, battery behavior and reversible server integration.
- Critical assumptions: actual profile API access, original NAS file mapping, a usable authoring device, finite local-file selection and verified charging signal.
- Accepted trade-offs: small NAS bridge; extra delivery/staging space; first version excludes streaming-only tracks and smart-list semantics.
- Risk retirement checks: every row above retains its disposition and adversarial test; household account tests must precede deep integration, and storage fault tests must precede nightly operation.
- Invalidated if: Listener cannot independently curate, her playlists cannot be accessed, or a complete offline set cannot fit reliably.
- Stop/pivot triggers: wrong-profile data, ambiguous file mapping, damaged library after interruption, unbounded network attempts, or no truthful charger signal. Keep manual sync and existing music usable while resolving them.
- Needs human verification: Listener's app experience, her actual household profile/library access, charger wiring and real charging behavior. No NAS service, schedule, account or firmware has been changed by this exploration.
