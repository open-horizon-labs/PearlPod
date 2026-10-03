# PearlPod export media contract

Status: the local candidate implements managed export, genre fields, embedded covers and bounded lyric display. See [implementation evidence](SYNC-IMPLEMENTATION.md) for the current behavior and remaining gates. This document also retains proposed capabilities not yet implemented, including selectable lyric languages, unattended scheduling, free-space preflight and garbage collection. The selected managed layout is `music/.pearl/objects/` with catalogs in `music/.pearl/catalogs/`. Album-art embedding explicitly supersedes the earlier separate-art-only efficiency proposal: art/tag edits change the MP3 hash; lyric/playlist edits do not. This extends the playlist/sync design. The goal is that a selected song arrives with its identity, album, artwork, genres and available lyrics intact, regardless of conversion or transfer transport.

## Two layouts, explicit compatibility

A manually copied, scanner-compatible export uses an album directory per source album/edition and a playlist directory:

```text
music/
  Albums/<safe-artist>-<artist-id>/<safe-album>-<album-id>/
    01-003-<safe-title>-<track-id>.mp3
    01-003-<safe-title>-<track-id>.lrc
    cover.jpg
  Playlists/<safe-playlist>-<playlist-id>.m3u8
```

The album directory isolates covers; disc and track prefixes provide a useful fallback order. IDs prevent collisions between editions, same-name albums and same-name playlists. Filenames are bounded, deterministic ASCII slugs plus stable IDs, with no FAT-reserved characters, trailing dots/spaces, control characters, reserved device names or case-insensitive collisions. Unicode display names live in tags/catalog, not truncated filenames. Full device paths must fit `PEARL_PATH` (512 bytes including NUL); display fields must fit `PEARL_NAME` (160 bytes including NUL), with truncation only at UTF-8 boundaries. Preserve full display values in the NAS manifest and report device truncation.

Write UTF-8 M3U8 with LF endings and paths relative to its directory, for example `../Albums/.../song.mp3`. Entries must resolve to real audio files within `/sdcard/music`, retaining order and repetitions. Reject traversal, symlinks escaping the export root and remote URLs. The current scanner does not use EXTINF for track metadata or playlist covers. Do not promise either through M3U comments.

The incremental managed export continues to use `_pearl/objects/<hash>` and immutable generations from the sync design. Its catalog must explicitly map logical track IDs to audio objects, album IDs, metadata, artwork and lyrics. Opaque filenames cannot supply these values. It requires the active-generation loader and exclusion of staged/retired objects from scanning before use. Never present this layout as compatible with today's ordinary scanner.

## Metadata survives preparation

Capture metadata before stripping tags or converting audio. Resolve Plex metadata and source-file tags by an explicit field policy: selected Plex display values take precedence when present; source tags fill missing fields. Record provenance and absence rather than guessing from artist/title. Preserve title, artist list, album artist list, album title and stable album/edition ID, disc/track numbers and totals, date/year, duration, genre list, language and explicit-content flag when available. Unknown values remain unknown. Genres remain ordered strings; do not split arbitrary punctuation or collapse multiple genres into one numeric code.

For scanner-compatible MP3 exports, write bounded ID3v2.3 text frames using UTF-16: TIT2, TPE1, TPE2, TALB, TRCK and TPOS. Preserve genre as TCON and date/year in appropriate tags even though current PearlPod ignores those fields. FLAC exports preserve corresponding Vorbis comments, including repeated GENRE fields. The current `pearl_tags`/`pearl_track` models need genre support before genre browsing can be claimed. Same-title album editions currently group by album title plus album artist; distinct edition grouping needs a stable album identity in firmware rather than misleading title suffixes.

For managed exports, keep display metadata in the versioned catalog and keep prepared audio bytes stable across metadata-only edits. The generation loader supplies the same library fields as tagged files, extended with genres and album identity. Do not simultaneously promise stripped tags and compatibility with the current tag scanner. A standalone tagged export is a separate output profile; retagging it legitimately changes its file hash without requiring audio re-encoding.

## Artwork

Select an explicit album cover first, then usable embedded/source folder art; preserve provenance. Playlist art is a separate association, never substituted for every member's album cover. Keep artwork outside audio for managed delivery so an art-only edit transfers no audio. Use explicit catalog references for album, playlist and optional track-specific images.

For ordinary folders, emit `cover.jpg` or `cover.png`, bounded to 400 KiB and modest dimensions (proposed 480 × 480 maximum), with orientation applied on the NAS. Today's renderer center-crops to 240 × 240. An optional `pearl-cover.rgb` must be exactly 115,200 bytes: 240 × 240 RGB565, high byte first, matching `main/artwork.c`. Do not write a differently sized raw image under that name. Missing art uses the player's fallback and is reported; malformed art is rejected during export validation.

## Lyrics and other text sidecars

Discover same-stem `.lrc`, `.srt`, `.vtt` and `.txt` beside the exact resolved source audio, including language-qualified variants such as `song.ja.lrc`. Also extract embedded timed/untimed lyrics when present and accessible. Do not assume Plex exposes sidecars through its playlist API: inspect the resolved, allowed NAS source directory. Carry available originals as distinct hashed assets with language, format, source identity and timing basis. Never fetch or invent missing lyrics automatically. File association is by source track identity, not a global basename search.

Normalize supported timed sources on the NAS to a versioned cue asset with start/end milliseconds, text and language; retain original sidecars for fidelity. LRC repeated timestamps, offsets, multiline text and enhanced word timing must either be parsed deliberately or preserved with an explicit unsupported-feature report. SRT/VTT cues retain their time intervals; strip executable/HTML styling from display text. Untimed lyrics remain a scrollable text asset. Encoding must be decoded explicitly and normalized to UTF-8; unrecognized encoding is a named warning, not silent replacement. Bound original and normalized assets (initial proposal: 256 KiB per asset, 4,096 cues, 1,024 UTF-8 bytes per cue); oversize content is preserved in the NAS cache but not published as device-displayable.

MP3 conversion must preserve the timing relationship. Record any effective start offset and apply it consistently to cues; test encoder delay/padding with the actual decoder. Audio trimming or speed changes require deliberate cue remapping. Do not claim sample-accurate lyrics from nominal container duration alone.

## Proposed player surface

Add a Lyrics action on Now Playing only when the selected track has an available text asset. Show a large current line, subdued previous/next lines and an easy return to album art. Swiping scrolls lyrics; a Follow control resumes automatic highlighting after manual scrolling. Untimed text uses ordinary scrolling and a clear untimed state. Language variants are selectable. Track changes clear old cues; pause freezes following; resume and future seek recompute the active cue. Volume and long-hold controls retain their physical behavior.

Load only the selected track's cues with bounded allocation, release them on track change, and stop lyric work while the screen sleeps. Current playback state exposes whole seconds; timed highlighting needs a decoder-derived millisecond playback clock that accounts for output buffering and pause, rather than wall time since opening a file. Word highlighting is deferred until that clock is verified. Lyrics must not make boot, audio playback or an album-art-only screen depend on parsing text.

## Required acceptance evidence

Before publishing an exporter profile, run generated fixtures through the actual PearlPod metadata, artwork, playlist and eventual managed-catalog/lyrics parsers. A manifest/schema validator alone cannot establish player compatibility.

- Mixed MP3/FLAC input, Japanese/emoji tags, missing fields, multi-disc albums, repeated/shared tracks and different editions with identical names retain intended identity and order after conversion.
- Same-stem source collisions, FAT case collisions, long paths and unsafe sidecar references never misassociate content or escape the allowed source/export root.
- Cover precedence, malformed/oversize images and absent art yield the expected pixels or fallback; artwork edits transfer zero audio in the managed profile.
- Multiple genres and language variants round-trip without loss; unavailable firmware features are reported as unsupported rather than marked passed.
- LRC offsets/repeated timestamps, SRT/VTT multiline cues, untimed lyrics, invalid encodings and hostile/oversize text are handled within limits. Real playback validates pause/resume, track switching and conversion timing.
- Lyrics/genre/art-only edits transfer only changed metadata/assets. Every required sidecar participates in generation hashing, free-space preflight, checksums, activation, rollback and reference-safe garbage collection alongside audio.

Code inspection basis: `main/player.h` defines the path/name/art limits and lacks genre/lyrics fields; `main/metadata.c` reads current text/number tags; `main/library.c` determines grouping and folder cover precedence; `main/playlist.c` resolves contained paths and preserves entries; `main/artwork.c` emits high-byte-first RGB565. No new exporter or lyric renderer was deployed by this document.
