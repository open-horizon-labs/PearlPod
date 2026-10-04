"""Playlist-ordered deltas and bounded, content-oriented FTP progress messages."""
import json
import posixpath
import re
from pathlib import PurePosixPath


def short(value, budget=64):
    value=re.sub(r'[\x00-\x1f]', ' ', str(value or ''))
    if len(value.encode())<=budget:return value
    if budget<3:return ''
    return value.encode()[:budget-3].decode('utf-8','ignore')+'...'


def plan(rows, staged, existing, changed):
    files={row['file']:row['bytes'] for row in rows if 'file' in row}
    missing={name for name,size in files.items() if existing.get(name)!=size or name in changed}
    tracks={row['track']:row for row in rows if 'track' in row}
    playlists=[row for row in rows if 'playlist' in row]
    ordered=[];seen=set();metadata={}
    def add(name, playlist='', index=0, track=None):
        if name not in missing or name in seen:return
        seen.add(name)
        path=PurePosixPath(name)
        suffix=path.suffix.lower()
        kind='song' if suffix=='.mp3' else 'artwork' if suffix=='.jpg' else 'playlist' if suffix=='.m3u8' else 'lyrics'
        song=artist=album=''
        if track:
            music=PurePosixPath(track['track'])
            song=re.sub(r'^\d+(?:-\d+)? - ', '', music.stem)
            album=music.parent.name;artist=music.parent.parent.name
            # ID3 is already prepared by the exporter. Read tags only: no audio
            # hashing, decoding, conversion, or additional Plex request.
            if track['track'] not in metadata:
                from mutagen.id3 import ID3, ID3NoHeaderError
                try:
                    tags=ID3(staged/track['track'])
                    song=str(tags.get('TIT2',song));album=str(tags.get('TALB',album));artist=str(tags.get('TPE1',artist))
                except ID3NoHeaderError:pass
                metadata[track['track']]=(song,album,artist)
            song,album,artist=metadata[track['track']]
        ordered.append((name,dict(p=short(playlist),i=index,c=len(playlists),
                                  t=short(song),a=short(album),r=short(artist),k=kind)))
    for index,playlist in enumerate(playlists,1):
        parent=posixpath.dirname(playlist['playlist'])
        for line in (staged/playlist['playlist']).read_text().splitlines():
            if not line or line.startswith('#'):continue
            name=posixpath.normpath(posixpath.join(parent,line))
            track=tracks.get(name)
            if not track:continue
            add(name,playlist['title'],index,track)
            add(track.get('art',''),playlist['title'],index,track)
            for lyric in track.get('lyrics',[]):
                add(lyric['file'],playlist['title'],index,track)
                add(lyric['original'],playlist['title'],index,track)
        add(playlist['playlist'],playlist['title'],index)
        # The ordinary scanner discovers this playlist after a reboot. Publish
        # its file only after all referenced media, art and lyrics have closed.
        ordered.append((None,dict(p=short(playlist['title']),i=index,c=len(playlists),t='',a='',r='',k='saved',q=index)))
    for name in sorted(missing):add(name,track=tracks.get(name))
    total=sum(files[name] for name,_ in ordered if name)
    songs=sum(info['k']=='song' for _,info in ordered)
    completed=done=ready=0
    result=[]
    for name,info in ordered:
        info.update(b=total,d=completed,f=files[name] if name else 0,n=songs,s=done)
        info.setdefault('q',ready)
        result.append((name,info))
        completed+=files[name] if name else 0;done+=info['k']=='song';ready+=name is None
    final=dict(p='',i=0,c=len(playlists),t='',a='',r='',k='finishing',b=total,d=total,f=0,n=songs,s=songs,q=len(playlists))
    return result,final


def message(info):
    """Fits one FTP control line even with JSON escaping and multibyte titles."""
    info=dict(info)
    for budget in (64,48,32,16,0):
        for key in ('p','t','a','r'):info[key]=short(info[key],budget)
        data=json.dumps(info,ensure_ascii=False,separators=(',',':'))
        if len(data.encode())<=480:return 'SITE PEARL '+data
    raise ValueError('Progress message too large')
