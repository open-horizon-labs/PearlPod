# PearlPod syncer local spike

Implementation is in progress. The initial authentication helper uses Plex's device-link flow; it never asks for the household password or prints the resulting token. The intended container reads a mounted token secret and read-only music files, with a separate writable preparation cache. Plex credentials never go on PearlPod.

```sh
python3 -m venv .sync-venv
.sync-venv/bin/pip install -r syncer/requirements.txt
.sync-venv/bin/python syncer/plex_auth.py --token-file .sync-state/plex-token
```

For a managed Home profile, pass `--profile NAME`; its PIN, if set, is entered locally through a hidden prompt. Account/profile playlist visibility must be verified against the real server before export. Authentication files are created with mode 0600. The ignored `.sync-state/` directory is local only; mount its token file as a secret when containerizing.

Execution scope: Plex `PP:` playlist snapshots, exact local-file resolution, MP3 preparation, metadata/art/lyrics preservation, incremental generic delivery and bounded manual Pod sync. Preserve existing manual music, fast offline boot and physical controls. The implementation must satisfy the media contract and sync design acceptance gates. Nightly charging automation remains dependent on a verified charger signal.

First real-data gates are ordered membership for `P: P!nk` (observed playlist 41741), the owning profile, exact NAS file mapping, repeated/shared tracks and multiple media parts. Failed source access must retain the last published generation. Local synthetic checks can precede credentials; real export and device delivery cannot be called verified until those gates pass.

Selection convention: only audio playlists whose titles start with `PP:` are selected. Strip that prefix and surrounding whitespace for the Pod display name; retain Plex playlist ID as identity. Live Selected profile-profile verification on 2026-10-03 succeeded against NAS2 with `/share/Media/Music` mapped to `/Volumes/MUSIC`. The test list contains two readable P!nk FLAC tracks. Plex createPlaylist collapsed the attempted repeated track; repetition support through this authoring path remains unverified. `plex_snapshot.py` validates exact single-part files and path containment before atomically replacing its output. This is a source snapshot, not a completed media exporter or device sync.
