"""Deterministic, bounded NAS-side media preparation for PearlPod."""
import hashlib
import io
import json
import re
import subprocess
import os
import tempfile
from pathlib import Path
from PIL import Image, ImageOps
from mutagen import File
from mutagen.id3 import ID3, APIC, TIT2, TPE1, TPE2, TALB, TRCK, TPOS, TCON, TYER, USLT

PROFILE = 'mp3-256k-48k-id3v23-cover480-v2'
LIMIT = 256 * 1024
_verified = {}

def fingerprint(path):
    st=path.stat()
    return [st.st_dev,st.st_ino,st.st_size,st.st_mtime_ns,st.st_ctime_ns]

def unchanged_digest(path):
    key=str(path.resolve());state=fingerprint(path)
    saved=_verified.get(key)
    if saved and saved[0]==state:return saved[1]
    value=digest(path)
    if fingerprint(path)!=state:raise ValueError('File changed while hashing')
    if len(_verified)>=16384:_verified.clear()
    _verified[key]=(state,value)
    return value

def source_digest(source,cache):
    # Persistent stat-keyed source identity avoids rereading the entire NAS every minute.
    directory=cache/'sources';directory.mkdir(exist_ok=True)
    key=hashlib.sha256(str(source.resolve()).encode()).hexdigest()
    memo=directory/(key+'.json');state=fingerprint(source)
    if memo.exists():
        stored=json.loads(memo.read_text())
        if stored.get('stat')==state and re.fullmatch('[a-f0-9]{64}',stored.get('sha','')):
            memo.touch()
            return stored['sha']
    value=unchanged_digest(source)
    write_json(memo,{'stat':state,'sha':value})
    return value


def write_json(path,value):
    fd,name=tempfile.mkstemp(dir=path.parent,suffix='.tmp')
    try:
        with os.fdopen(fd,'w') as out:json.dump(value,out)
        os.replace(name,path)
    finally:
        Path(name).unlink(missing_ok=True)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(65536), b''): h.update(block)
    return h.hexdigest()


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')).encode()


def cover_bytes(raw):
    if not raw: return b''
    if len(raw) > 10 * 1024 * 1024: raise ValueError('Artwork too large')
    with Image.open(io.BytesIO(raw)) as image:
        image = ImageOps.exif_transpose(image).convert('RGB')
        image.thumbnail((480, 480))
        out = io.BytesIO(); image.save(out, 'JPEG', quality=88)
        result = out.getvalue()
    if len(result) > 400 * 1024: raise ValueError('Prepared artwork too large')
    return result


def source_cover(path, media):
    if media:
        pictures = getattr(media, 'pictures', [])
        if pictures: return pictures[0].data
        if media.tags:
            for key in media.tags:
                value = media.tags[key]
                if key.startswith('APIC'): return value.data
    for name in ('cover.jpg', 'folder.jpg', 'cover.png', 'folder.png', 'Cover.jpg', 'Folder.jpg'):
        candidate = path.parent / name
        if candidate.is_file():
            candidate.resolve().relative_to(path.parent.resolve())
            if candidate.stat().st_size > 10 * 1024 * 1024: raise ValueError('Artwork too large')
            return candidate.read_bytes()
    return b''


def normalize_lyrics(raw, suffix):
    if len(raw) > LIMIT: raise ValueError('Lyrics too large')
    text = raw.decode('utf-8-sig') if not raw.startswith((b'\xff\xfe', b'\xfe\xff')) else raw.decode('utf-16')
    text = text.replace('\r\n', '\n').replace('\r', '\n')
    if '\x00' in text:raise ValueError('NUL in lyrics')
    cues = []
    if suffix == '.lrc':
        offset = 0
        m = re.search(r'\[offset:([+-]?\d+)\]', text, re.I)
        if m: offset = int(m.group(1))
        for line in text.splitlines():
            times = re.findall(r'\[(\d+):(\d{2})(?:[.:](\d{1,3}))?\]', line)
            lyric = re.sub(r'^(?:\[\d+:\d{2}(?:[.:]\d{1,3})?\])+', '', line)
            lyric = re.sub(r'<\d+:\d+(?:\.\d+)?>', '', lyric).strip()
            for minute, second, fraction in times:
                if int(second) >= 60: raise ValueError('Invalid LRC timestamp')
                stamp = max(0, int(minute)*60000+int(second)*1000+int((fraction or '').ljust(3, '0') or 0)+offset)
                cues.append((stamp, lyric))
    elif suffix in ('.srt', '.vtt'):
        pattern = r'(?:(\d+):)?(\d{2}):(\d{2})[.,](\d{3})'
        for block in re.split(r'\n\s*\n', text):
            lines = block.splitlines()
            for i, line in enumerate(lines):
                if '-->' not in line: continue
                start = re.search(pattern, line)
                if not start: raise ValueError('Invalid subtitle timestamp')
                hours, minute, second, millis = start.groups()
                stamp = int(hours or 0)*3600000+int(minute)*60000+int(second)*1000+int(millis)
                lyric = ' / '.join(re.sub(r'<[^>]*>', '', v).strip() for v in lines[i+1:])
                cues.append((stamp, lyric))
                # Preserve end as a blank cue, so text does not persist through gaps.
                end = re.search(pattern, line.split('-->', 1)[1])
                if end:
                    h,m,s,ms=end.groups(); cues.append((int(h or 0)*3600000+int(m)*60000+int(s)*1000+int(ms), ''))
                break
    if cues:
        if len(cues) > 4096: raise ValueError('Too many lyric cues')
        cues.sort(key=lambda v: v[0])
        lines = []
        for stamp, lyric in cues:
            if len(lyric.encode()) > 1024: raise ValueError('Lyric line too long')
            lines.append(f'[{stamp//60000:02d}:{stamp//1000%60:02d}.{stamp%1000:03d}]{lyric}')
        data = ('\n'.join(lines)+'\n').encode()
        if len(data) > LIMIT: raise ValueError('Normalized lyrics too large')
        return '.lrc', data
    if suffix != '.txt' and suffix != '.lrc': raise ValueError('No valid timed cues')
    return '.txt', text.encode()


def lyric_sources(path, media):
    found = []
    for candidate in sorted(path.parent.iterdir()):
        if candidate.suffix.lower() not in ('.lrc', '.srt', '.vtt', '.txt'): continue
        stem = candidate.stem
        if stem != path.stem and not stem.startswith(path.stem + '.'): continue
        candidate.resolve().relative_to(path.parent.resolve())
        if candidate.stat().st_size > LIMIT: raise ValueError('Lyrics too large')
        raw = candidate.read_bytes()
        language = stem[len(path.stem):].lstrip('.') or 'und'
        found.append((language, candidate.suffix.lower(), raw))
    if not found and media and media.tags:
        for key, value in media.tags.items():
            if key.startswith('USLT'): found.append((value.lang or 'und', '.txt', value.text.encode()))
            elif key.lower() in ('lyrics', 'unsyncedlyrics'):
                text = '\n'.join(value) if isinstance(value, list) else str(value)
                found.append(('und', '.lrc' if re.search(r'\[\d+:\d+', text) else '.txt', text.encode()))
    return found


def store_bytes(objects, data, extension):
    name = hashlib.sha256(data).hexdigest() + extension
    target = objects / name
    if not target.exists() or unchanged_digest(target) != name[:64]:
        fd,temporary = tempfile.mkstemp(dir=objects,suffix='.tmp')
        try:
            with os.fdopen(fd,'wb') as out:
                out.write(data); out.flush(); os.fsync(out.fileno())
            os.replace(temporary,target)
        finally:Path(temporary).unlink(missing_ok=True)
    return name


def prepare(track, cache, plex_cover=b'', cache_limit=20*1024**3):
    source = Path(track['source'])
    before = source.stat()
    initial_fingerprint = fingerprint(source)
    media = File(source)
    metadata = dict(track)
    easy = File(source, easy=True)
    tags = easy.tags if easy and easy.tags else {}
    metadata['album_artist'] = (tags.get('albumartist') or [track['artist']])[0]
    metadata['genres'] = track.get('genres') or list(tags.get('genre', []))
    metadata['year'] = (tags.get('date') or [''])[0]
    art = cover_bytes(plex_cover or source_cover(source, media))
    objects = cache / 'objects'; objects.mkdir(parents=True, exist_ok=True)
    lyrics = []
    for language, suffix, raw in lyric_sources(source, media):
        original = store_bytes(objects, raw, suffix)
        extension, normalized = normalize_lyrics(raw, suffix)
        delivery = store_bytes(objects, normalized, extension)
        lyrics.append({'language': language, 'file': delivery, 'original': original})
    source_hash = source_digest(source,cache)
    signature = hashlib.sha256(canonical({'source': source_hash, 'profile': PROFILE,
        'metadata': {k: metadata.get(k) for k in ('title','artist','album','album_artist','album_id','track','disc','genres','year')},
        'cover': hashlib.sha256(art).hexdigest()})).hexdigest()
    mappings = cache / 'prepared'; mappings.mkdir(exist_ok=True)
    memo = mappings / (signature + '.json')
    if memo.exists():
        saved = json.loads(memo.read_text())
        name = saved['audio']
        if not (objects/name).is_file():
            memo.unlink()
        elif saved.get('audio_stat') != fingerprint(objects/name):
            if unchanged_digest(objects/name) != name[:64]:
                memo.unlink()
            else:
                write_json(memo,{'audio':name,'audio_stat':fingerprint(objects/name)})
    if not memo.exists():
        from cache import enforce_budget
        reserve=max(int(track.get('duration_ms') or 0)*40, before.st_size)+2*1024*1024
        enforce_budget(cache, cache_limit, reserve*2)
        fd,temporary=tempfile.mkstemp(dir=objects,suffix='.preparing.mp3')
        os.close(fd)
        temp=Path(temporary)
        codec = ['-c:a','copy'] if source.suffix.lower()=='.mp3' else ['-c:a','libmp3lame','-b:a','256k','-ar','48000','-ac','2']
        command = ['ffmpeg','-nostdin','-v','error','-y','-i',str(source),'-map','0:a:0',*codec,'-map_metadata','-1','-map_chapters','-1',str(temp)]
        try:
            subprocess.run(command, check=True, capture_output=True, timeout=600)
            id3 = ID3()
            for cls, value in ((TIT2,metadata['title']),(TPE1,metadata['artist']),(TPE2,metadata['album_artist']),
                               (TALB,metadata['album']),(TRCK,str(metadata.get('track') or 0)),(TPOS,str(metadata.get('disc') or 1))):
                id3.add(cls(encoding=1,text=[value]))
            if metadata['year']: id3.add(TYER(encoding=1,text=[metadata['year'][:4]]))
            if metadata['genres']: id3.add(TCON(encoding=1,text=metadata['genres']))
            if art: id3.add(APIC(encoding=0,mime='image/jpeg',type=3,desc='Cover',data=art))
            id3.save(temp, v2_version=3, padding=lambda _: 0)
            name = digest(temp)+'.mp3'; temp.replace(objects/name)
            write_json(memo,{'audio':name,'audio_stat':fingerprint(objects/name)})
        finally:
            temp.unlink(missing_ok=True)
    else:
        name=json.loads(memo.read_text())['audio']
        memo.touch()
    if fingerprint(source) != initial_fingerprint: raise ValueError('Source changed during preparation')
    return {'track':name, 'album_id':metadata['album_id'], 'genres':metadata['genres'], 'lyrics':lyrics,
            'art':store_bytes(objects,art,'.jpg') if art else '', 'id':track['id']}
