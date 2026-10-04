"""Read PP: audio playlists and validate exact media paths; never logs tokens."""
import argparse
import json
import os
from pathlib import Path
import tempfile
from plexapi.server import PlexServer


def snapshot(server, source_root, local_root, playlist_prefix='PP:'):
    local_root = local_root.resolve(strict=True)
    lists = []
    tracks = {}
    for playlist in server.playlists():
        if playlist.playlistType != 'audio' or not playlist.title.startswith(playlist_prefix):
            continue
        entries = []
        for item in playlist.items():
            parts = [part for media in item.media for part in media.parts]
            if len(parts) != 1:
                raise ValueError('Ambiguous or unavailable media part')
            relative = Path(parts[0].file).relative_to(source_root)
            path = (local_root / relative).resolve(strict=True)
            path.relative_to(local_root)
            if not path.is_file():
                raise ValueError('Source is not a file')
            key = str(item.ratingKey)
            entries.append(key)
            tracks[key] = {
                'id': key, 'source': str(path), 'title': item.title,
                'artist': item.grandparentTitle, 'album': item.parentTitle,
                'album_id': str(item.parentRatingKey), 'album_thumb': getattr(item,'parentThumb',None), 'track': item.index,
                'disc': item.parentIndex, 'duration_ms': item.duration,
                'genres': [g.tag for g in getattr(item, 'genres', [])],
                'bytes': path.stat().st_size,
            }
        lists.append({'id': str(playlist.ratingKey), 'title': playlist.title[len(playlist_prefix):].strip(), 'entries': entries})
    return {'format': 1, 'server_id': server.machineIdentifier, 'playlists': lists, 'tracks': tracks}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server', default='http://192.0.2.2:32400')
    p.add_argument('--token-file', type=Path, required=True)
    p.add_argument('--source-root', type=Path, default=Path('/share/Media/Music'))
    p.add_argument('--local-root', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    server = PlexServer(a.server, token=a.token_file.read_text().strip(), timeout=15)
    result = snapshot(server, a.source_root, a.local_root)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(dir=a.output.parent, prefix='.snapshot-')
    try:
        with os.fdopen(fd, 'w') as f:
            json.dump(result, f, ensure_ascii=False, sort_keys=True)
            f.write('\n'); f.flush(); os.fsync(f.fileno())
        os.replace(temp, a.output)
    finally:
        if os.path.exists(temp): os.unlink(temp)
    print('Validated snapshot:', len(result['playlists']), 'playlists,', len(result['tracks']), 'distinct tracks')


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        raise SystemExit('Snapshot failed (' + type(e).__name__ + '); previous output retained. Credentials were not logged.') from None
