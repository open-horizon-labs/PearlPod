"""Publish immutable prepared generations only after all selected assets exist."""
import hashlib
import json
import os
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import requests
from plex_snapshot import snapshot
from media import prepare, canonical, store_bytes, digest


def album_art(server, album_id, thumb=None):
    if thumb is None:
        thumb = server.fetchItem(int(album_id)).thumb
    raw = b''
    if thumb:
        with requests.get(server._baseurl+thumb, headers={'X-Plex-Token':server._token}, timeout=15, stream=True) as response:
            response.raise_for_status()
            for block in response.iter_content(65536):
                raw += block
                if len(raw)>10*1024*1024:
                    raise ValueError('Plex artwork too large')
    return raw


def prepare_cached(track,cache,cover,inputs,cover_hash,cache_limit):
    from media import PROFILE, fingerprint, write_json
    directory=cache/'tracks';directory.mkdir(exist_ok=True)
    memo=directory/(hashlib.sha256((track['source']+str(track.get('id',''))).encode()).hexdigest()+'.json')
    key=hashlib.sha256(canonical({'profile':PROFILE,'track':track,'inputs':inputs,'cover':cover_hash})).hexdigest()
    try:
        saved=json.loads(memo.read_text())
        if saved['key']==key and all(fingerprint(cache/'objects'/name)==stat for name,stat in saved['objects'].items()):
            memo.touch()
            return saved['result']
    except (OSError,ValueError,KeyError,TypeError):pass
    result=prepare(track,cache,cover,cache_limit=cache_limit)
    names={result.get('track'),result.get('art')}
    for lyric in result.get('lyrics',[]):names.update((lyric['file'],lyric['original']))
    states={name:fingerprint(cache/'objects'/name) for name in names if name}
    write_json(memo,{'key':key,'result':result,'objects':states})
    return result


def publish(server, source_root, local_root, cache, cache_limit=20*1024**3, playlist_prefix='PP:', open_share=None):
    cache.mkdir(parents=True, exist_ok=True)
    selected = snapshot(server, source_root, local_root, playlist_prefix)
    from media import fingerprint
    from cache import maintain
    prepared = {}
    covers = {}
    groups = {}
    for track in selected['tracks'].values():
        source = Path(track['source'])
        groups.setdefault(source.parent, set()).add(source.stem)
    def input_state():
        result = {}
        for directory, stems in groups.items():
            for path in directory.iterdir():
                stem = path.stem
                matches = stem in stems or any(stem[:i] in stems for i,c in enumerate(stem) if c=='.')
                if matches or path.name.lower() in ('cover.jpg','folder.jpg','cover.png','folder.png'):
                    if path.is_file():result[str(path)] = fingerprint(path)
        return result
    inputs = input_state()
    maintain(cache, cache_limit)
    album_thumbs={t['album_id']:t.get('album_thumb') for t in selected['tracks'].values()}
    def artwork(album_id):return album_id,album_art(server,album_id,album_thumbs[album_id])
    with ThreadPoolExecutor(max_workers=4) as pool:
        covers=dict(pool.map(artwork,album_thumbs))
    cover_hashes={key:hashlib.sha256(raw).hexdigest() for key,raw in covers.items()}
    def prepare_one(item):
        key,track=item
        source=Path(track['source'])
        associated={name:stat for name,stat in inputs.items() if Path(name).parent==source.parent and
                    (Path(name).stem==source.stem or Path(name).stem.startswith(source.stem+'.') or Path(name).name.lower() in ('cover.jpg','folder.jpg','cover.png','folder.png'))}
        result=prepare_cached(track,cache,covers[track['album_id']],associated,cover_hashes[track['album_id']],cache_limit)
        return key,result
    with ThreadPoolExecutor(max_workers=2) as pool:
        prepared=dict(pool.map(prepare_one,selected['tracks'].items()))
    objects=cache/'objects'; objects.mkdir(exist_ok=True)
    from layout import readable
    files,records,paths,unique,asset=readable(selected,prepared,objects)
    for playlist in selected['playlists']:
        from layout import component
        import posixpath
        name=unique('Playlists/'+component(playlist['title'],80)+'.m3u8',playlist['id'])
        data=('#EXTM3U\n'+'\n'.join(posixpath.relpath(paths[k],'Playlists') for k in playlist['entries'])+'\n').encode()
        source=store_bytes(objects,data,'.m3u8');asset(name,source)
        display=playlist['title'].encode()[:159].decode('utf-8',errors='ignore')
        records.append({'playlist':name,'title':display,'full_title':playlist['title'],'id':playlist['id']})
    if canonical(snapshot(server,source_root,local_root,playlist_prefix)) != canonical(selected):
        raise ValueError('Plex selection changed during preparation')
    with ThreadPoolExecutor(max_workers=4) as pool:
        for album_id, raw in pool.map(artwork,album_thumbs):
            if raw != covers[album_id]:raise ValueError('Plex artwork changed during preparation')
    if input_state() != inputs:
        raise ValueError('Source inputs changed during publication')
    data=b''.join(canonical(row)+b'\n' for row in [{'format':2}]+[files[n] for n in sorted(files)]+records)
    if len(data)>512*1024 or any(len(line)>4095 for line in data.splitlines()): raise ValueError('Catalog limits exceeded')
    sha=hashlib.sha256(data).hexdigest()
    generations=cache/'catalogs'; generations.mkdir(exist_ok=True)
    target=generations/sha
    if not target.exists() or digest(target)!=sha:
        temp=generations/(sha+'.tmp')
        with temp.open('wb') as out:
            out.write(data);out.flush();os.fsync(out.fileno())
        temp.replace(target)
    head={'format':2,'catalog':sha,'bytes':len(data),'objects':len(files),'source_checked_at':int(time.time())}
    try:
        old_head=json.loads((cache/'head.json').read_text())
        previous=old_head.get('previous_catalog') if old_head['catalog']==sha else old_head['catalog']
        if previous:head['previous_catalog']=previous
    except (OSError,ValueError,KeyError,TypeError):pass
    maintain(cache, cache_limit)
    temporary=cache/'head.tmp'; temporary.write_bytes(canonical(head)); os.replace(temporary,cache/'head.json')
    if open_share is not None:
        from open_share import materialize
        materialize(cache, head, open_share)
    return head


def main():
    import argparse
    from plexapi.server import PlexServer
    from cache import maintain
    p = argparse.ArgumentParser()
    p.add_argument('--plex-url', required=True)
    p.add_argument('--plex-token', type=Path, required=True)
    p.add_argument('--source-root', type=Path, required=True)
    p.add_argument('--music', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--cache-limit', type=int, required=True)
    p.add_argument('--playlist-prefix', default='PP:')
    p.add_argument('--open-share', type=Path)
    a = p.parse_args()
    maintain(a.cache, a.cache_limit)
    server = PlexServer(a.plex_url, token=a.plex_token.read_text().strip(), timeout=15)
    publish(server, a.source_root, a.music, a.cache, a.cache_limit,
            a.playlist_prefix, a.open_share)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        reasons = {
            'Plex selection changed during preparation':'selection_changed',
            'Plex artwork changed during preparation':'artwork_changed',
            'Source inputs changed during publication':'source_changed',
            'Source changed during preparation':'source_changed',
            'File changed while hashing':'source_changed',
            'Preparation cache budget exceeded':'cache_full',
            'Catalog limits exceeded':'catalog_limits',
            'Ambiguous or unavailable media part':'media_unavailable',
        }
        print(json.dumps({'error':reasons.get(str(error),type(error).__name__)}), flush=True)
        raise SystemExit(1) from None
