# PearlPod export media contract

The microSD uses ordinary music-player files. The user's naming requirement supersedes the former hash-named object store. Content-addressed preparation files stay on the host; card media use readable artist/album folders, numbered song filenames, adjacent lyric/cover files and relative UTF-8 playlists. Sync bookkeeping stays separately in `music/.pearl/`.

```text
music/
  Artist/Album/
    01 - Song Title.mp3
    01 - Song Title.lrc
    01 - Song Title.ja.lrc
    cover.jpg
  Playlists/Playlist Name.m3u8
  .pearl/catalogs/...
```

Names preserve Unicode, spaces and ordinary punctuation. FAT-reserved characters and controls are replaced; components are bounded at UTF-8 boundaries. A stable numeric suffix appears only on an actual case-insensitive collision. Multi-disc prefixes include the disc number. Playlist entries are relative, for example `../Artist/Album/01 - Song Title.mp3`, and preserve order/repetitions. Unsafe or escaping paths are rejected.

MP3s retain ID3v2.3 title, artist, album artist, album, disc/track number, genre, available year and embedded JPEG cover. Existing MP3 audio is copied; other supported input is converted to 256 kbps stereo MP3 at 48 kHz. Available Plex artwork is preferred, with source/embedded art fallback. `cover.jpg` is also emitted beside album audio for ordinary players. The player groups by album tags and reads ordinary folders and playlists independently of the sync catalog.

Lyrics are discovered beside the exact NAS source file, including language-qualified LRC/SRT/VTT/TXT, or supported embedded lyrics. Available originals are carried alongside normalized UTF-8 display lyrics with a language/source suffix when needed. The first supported variant shares the song stem for ordinary-player association; additional variants retain a language suffix. No missing music, artwork or lyrics is generated or fetched automatically. Sidecars/cues remain bounded. PearlPod's current lyric UI follows decoder timing and selects the first variant; word timing and selectable languages remain future work.

Catalog format 2 describes readable relative media paths and expected lengths. Activation checks paths, lengths, metadata/playlist references and transactional NVS commit. It does not hash media contents or reread songs. Transfer errors and failed closes prevent activation. Ordinary manually copied media are preserved. Removing a playlist selection currently leaves ordinary music files on the card; ownership/deletion reconciliation remains future work.

During migration, existing hidden hash-named media remain until all readable files pass activation. Once format 2 replaces a format 1 catalog, the old managed media are retired. Hidden bookkeeping is retained; music is never renamed to hashes again. A native receiver test transfers the actual NAS export, resolves every relative playlist entry and confirms unchanged delivery does not rewrite media. Firmware loader tests exercise readable paths, migration, retained/manual files, catalog damage and NVS commit failure.
