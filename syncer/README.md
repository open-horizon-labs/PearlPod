# PearlPod syncer

Selected profile' audio playlists beginning with `PP:` are selected; the prefix is stripped for PearlPod. Source identity is the Plex playlist/track ID, not a display name. The host continuously prepares completed generations; PearlPod's Sync now discovers the host, starts its temporary FTP receiver and sends `POST /sync`. The host runs single-session `lftp mirror -R`, then uploads a catalog and ready marker last. No sync token, pairing or authentication is required. FTP uses anonymous protocol responses. The Plex token stays on the host.

## Local run

```sh
python3 -m venv .sync-venv
.sync-venv/bin/pip install -r syncer/requirements.txt
brew install lftp
.sync-venv/bin/python syncer/server.py --plex-token .sync-state/plex-token --music /Volumes/MUSIC --cache .sync-state/cache --advertise-ip 192.0.2.101
```

The IP is this Mac's observed LAN address for the spike; use the actual host address on another machine. The service advertises `_pearlpod-sync._tcp` on port 8787. PearlPod advertises `_pearlpod._tcp` during sync and accepts anonymous FTP on 2121, with passive data on 2024. mDNS locates the trigger service; file contents move through FTP. `GET /status` reports preparation and transfer state. The trigger destination is the caller's IPv4 address, not an arbitrary URL in the request. Only one transfer runs at a time. Background preparation runs every minute and retains the previous publication on error. A sync request is rejected while no completed publication exists or the latest publication failed.

Plex paths `/share/Media/Music/...` map to `/Volumes/MUSIC/...` locally. Supply `--source-root` if Plex uses a different prefix. The exporter resolves exactly one media part, confines resolved files to the allowed mount, and refuses ambiguous/unavailable selections. The NAS music mount is read-only to the exporter. In Plex, use the `selected-profile` profile; the verified test list is `PP: PearlPod Sync Test` (12345), containing two accessible P!nk tracks. Plex's playlist-creation API collapsed the attempted repeated entry; the exporter preserves the ordered entries actually returned by Plex.

## Preparation and incremental transfer

Lossless input converts to 256 kbps, 48 kHz stereo MP3. Existing MP3 audio is stream-copied. ID3v2.3 tags include title, artist, album artist, album, track/disc, genre and available year; album covers are resized to at most 480 × 480 and embedded. Covers also exist as separate delivery objects. Embedding means a cover/tag change legitimately changes that MP3's file bytes and delivery hash; lyric-only and playlist-only edits do not change audio. Stable immutable filenames plus lftp size comparisons avoid sending unchanged audio.

Same-stem LRC/SRT/VTT/TXT and language-qualified sidecars are carried over. Embedded untimed lyrics are extracted when no sidecar exists. Supported timed text normalizes to bounded UTF-8 LRC; originals remain separate assets. Lyrics are associated with the exact source track, not a global filename search. The initial device surface selects the first available language variant. Unsupported encodings or malformed/oversized text fail preparation and retain the old publication. Missing lyrics are fine.

The device keeps manual music separate from `.pearl/objects` and loads only the NVS-activated catalog. It verifies hashes, sizes and the full candidate library before committing active/previous hashes together. Same-size corrupt uploads are rejected and removed so a subsequent sync can resend them. Interrupted uploads are retransferred as files; upload resume and block deltas are not claimed. The initial implementation retains old objects rather than deleting them automatically; card-full transfer fails without activating the incomplete catalog. Free-space preflight and reference-safe collection remain follow-up work before large unattended libraries. FAT/microSD power-cut recovery remains a physical acceptance gate.

## Player controls

Open Your library and scroll to Sync now. Pause music first. Sync has a finite WiFi/FTP session; servers and radio stop afterward. A failed or interrupted update preserves the previously activated library. Long power hold requests sync shutdown before sleeping. USB console commands are `sync`, `sync status`, and `sync source http://HOST:8787` for a configured-address fallback when multicast is unavailable. No source contact happens at boot.

Now Playing shows a Lyrics button when associated lyrics exist. Timed text follows the decoder-derived millisecond clock with previous/current/next lines; arrows/swipes browse and Follow resumes tracking. Untimed text scrolls. Track changes and screen sleep release lyric memory. Current fonts do not cover all Japanese characters, and DMA/MP3 timing alignment still needs physical measurement; word highlighting and language selection are not implemented.

## Small container

Build from the repository root: `docker build -t pearlpod-syncer:local -f syncer/Dockerfile .`. The Docker context excludes credentials, firmware backups, caches and other workspace files. The locally built image is approximately 216 MiB including Python, ffmpeg and lftp.

On the Linux NAS, set `PEARLPOD_MUSIC`, `PEARLPOD_CACHE`, `PEARLPOD_PLEX_TOKEN`, `PEARLPOD_HOST_IP` and optionally `PEARLPOD_UID`/`PEARLPOD_GID`, then run `docker compose -f syncer/compose.yaml up -d`. The cache must be writable by that UID; music and the token are read-only mounts. Host networking permits mDNS and return FTP connections. No NAS container was deployed by the local spike. Nightly charging wake remains gated on a verified charging signal; manual sync is the current firmware path.

## Verification

`tools/check_sync.sh` exercises the prepared real NAS MP3s through PearlPod's metadata/art parsers (skipped when no local publication exists), native FTP code through host socket shims under AddressSanitizer/UBSan, no-op/truncated/interrupted/full-file transfers and lyric normalization/parser limits. Development tests additionally require `pip install -r syncer/requirements-dev.txt`. `tests/test_managed.py` uses the real prepared two-track cache and a host-compiled firmware loader with simulated NVS commits; the macOS CommonCrypto shim is test-only. `tools/check.sh` and `tools/render_ui.sh` cover library/WiFi/power and UI regressions. See `docs/SYNC-IMPLEMENTATION.md` for measured evidence and outstanding hardware gates.
