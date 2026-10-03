#!/usr/bin/env python3
"""Prepare artwork and labels without changing music files. FAT32 card: music/artist/album/tracks."""
from pathlib import Path
import argparse,io,struct
from PIL import Image,ImageOps
from mutagen import File
SIZE=240

def rgb565(image):
    im=ImageOps.fit(image.convert('RGB'),(SIZE,SIZE),method=Image.Resampling.LANCZOS)
    return b''.join(struct.pack('>H',((r>>3)<<11)|((g>>2)<<5)|(b>>3)) for r,g,b in im.getdata())

def cover_from_tags(tags):
    if not tags:return None
    if getattr(tags,'pictures',None):return tags.pictures[0].data
    if tags.tags:
        for key,value in tags.tags.items():
            if str(key).startswith('APIC'):return value.data
        covr=tags.tags.get('covr')
        if covr:return bytes(covr[0])
    return None

def first(tags,key):
    if not tags:return None
    value=tags.get(key)
    return str(value[0]) if value else None

def prepare(root):
    music=root/'music'
    if not music.is_dir():raise ValueError('Card must contain a music directory; put album folders inside it first.')
    albums={}
    for path in sorted(music.rglob('*')):
        if path.is_file() and path.suffix.lower() in ('.mp3','.flac','.wav'):
            albums.setdefault(path.parent,[]).append(path)
    for folder,tracks in albums.items():
        cover=None;album_title=None
        for name in ('cover.jpg','folder.jpg','cover.png','folder.png','Cover.jpg','Folder.jpg'):
            p=folder/name
            if p.exists():
                try:cover=Image.open(p).copy();break
                except Exception as exc:print(f'Skip artwork {p}: {exc}')
        for path in tracks:
            try:
                easy=File(path,easy=True)
                title=first(easy,'title');album_title=album_title or first(easy,'album')
                if title:path.with_name(path.name+'.pearl-title').write_text(title.replace('\n',' ')[:159],encoding='utf-8')
                if cover is None:
                    data=cover_from_tags(File(path))
                    if data:cover=Image.open(io.BytesIO(data)).copy()
            except Exception as exc:print(f'Skip metadata {path}: {exc}')
        if album_title:(folder/'.pearl-title').write_text(album_title.replace('\n',' ')[:159],encoding='utf-8')
        if cover is not None:(folder/'pearl-cover.rgb').write_bytes(rgb565(cover))
        print(f'{folder.relative_to(music)}: {len(tracks)} tracks, '+('art ready' if cover else 'personal fallback art'))
    return len(albums)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('card',type=Path);args=parser.parse_args();print(f'{prepare(args.card)} albums prepared.')
