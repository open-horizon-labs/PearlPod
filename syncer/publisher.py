"""Publish immutable prepared generations only after all selected assets exist."""
import hashlib
import json
import os
from pathlib import Path
import requests
from plex_snapshot import snapshot
from media import prepare, canonical, store_bytes, digest


def publish(server, source_root, local_root, cache):
    cache.mkdir(parents=True, exist_ok=True)
    selected = snapshot(server, source_root, local_root)
    prepared = {}
    for key, track in selected['tracks'].items():
        album = server.fetchItem(int(track['album_id']))
        raw=b''
        if album.thumb:
            response=requests.get(server._baseurl+album.thumb,headers={'X-Plex-Token':server._token},timeout=15,stream=True)
            response.raise_for_status()
            for block in response.iter_content(65536):
                raw += block
                if len(raw)>10*1024*1024: raise ValueError('Plex artwork too large')
        prepared[key]=prepare(track,cache,raw)
    objects=cache/'objects'; objects.mkdir(exist_ok=True)
    records=list(prepared.values())
    for playlist in selected['playlists']:
        # Absolute /music paths are portable across host and Pod mount points.
        data=('#EXTM3U\n'+'\n'.join('/music/.pearl/objects/'+prepared[k]['track'] for k in playlist['entries'])+'\n').encode()
        name=store_bytes(objects,data,'.m3u8')
        display=playlist['title'].encode()[:159].decode('utf-8',errors='ignore')
        records.append({'playlist':name,'title':display,'full_title':playlist['title'],'id':playlist['id']})
    if canonical(snapshot(server,source_root,local_root)) != canonical(selected):
        raise ValueError('Plex selection changed during preparation')
    required=set()
    for record in records:
        if 'playlist' in record: required.add(record['playlist']); continue
        required.add(record['track'])
        if record['art']: required.add(record['art'])
        for lyric in record['lyrics']: required.update((lyric['file'],lyric['original']))
    files=[{'file':name,'bytes':(objects/name).stat().st_size} for name in sorted(required)]
    data=b''.join(canonical(row)+b'\n' for row in [{'format':1}]+files+records)
    if len(data)>512*1024 or any(len(line)>4095 for line in data.splitlines()): raise ValueError('Catalog limits exceeded')
    sha=hashlib.sha256(data).hexdigest()
    generations=cache/'catalogs'; generations.mkdir(exist_ok=True)
    target=generations/sha
    if not target.exists() or digest(target)!=sha:
        temp=generations/(sha+'.tmp')
        with temp.open('wb') as out:
            out.write(data);out.flush();os.fsync(out.fileno())
        temp.replace(target)
    head={'format':1,'catalog':sha,'bytes':len(data),'objects':len(files)}
    temporary=cache/'head.tmp'; temporary.write_bytes(canonical(head)); os.replace(temporary,cache/'head.json')
    return head
