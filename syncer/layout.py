"""Readable, FAT-compatible music paths; internal cache IDs never name card media."""
import re
from pathlib import PurePosixPath

def component(value, limit=60):
    value=re.sub(r'[<>:"/\\|?*\x00-\x1f]', '_', str(value or 'Unknown')).strip().rstrip('. ')
    value=value.encode('utf-8')[:limit].decode('utf-8','ignore').rstrip('. ') or 'Unknown'
    if value.split('.')[0].upper() in {'CON','PRN','AUX','NUL',*[f'COM{i}' for i in range(1,10)],*[f'LPT{i}' for i in range(1,10)]}: value='_'+value
    if value.startswith('.'):value='_'+value.lstrip('.')
    return value

def valid_path(value):
    p=PurePosixPath(value)
    return bool(value) and not p.is_absolute() and len(value.encode())<220 and all(x not in ('','.','..') and not x.startswith('.') for x in value.split('/')) and not re.search(r'[\\\x00-\x1f]',value)

def readable(selected, prepared, objects):
    files={};records=[];paths={};used={};albums={}
    def unique(value, identity):
        key=value.casefold()
        if key in used and used[key]!=identity:
            p=PurePosixPath(value);value=str(p.with_name(p.stem+' ('+identity+')'+p.suffix));key=value.casefold()
        if len(value.encode())>=220:raise ValueError('Music path too long')
        used[key]=identity;return value
    def asset(path,source):
        if not valid_path(path):raise ValueError('Unsafe music path')
        if path in files and files[path]['cache_source']!=source:raise ValueError('Music path collision')
        files[path]={'file':path,'bytes':(objects/source).stat().st_size,'cache_source':source}
        return path
    for key in sorted(prepared):
        t=selected['tracks'][key];p=prepared[key]
        album_key=(component(t['artist'],45),component(t['album']),t['album_id'])
        directory=albums.get(album_key)
        if directory is None:
            directory=unique(album_key[0]+'/'+album_key[1],t['album_id']);albums[album_key]=directory
        disc=int(t.get('disc') or 1);prefix=(f'{disc:02d}-{int(t.get("track") or 0):02d}' if disc>1 else f'{int(t.get("track") or 0):02d}')
        path=unique(directory+'/'+prefix+' - '+component(t['title'])+'.mp3',key)
        paths[key]=asset(path,p['track']);stem=path[:-4]
        lyrics=[]
        for i,lyric in enumerate(p['lyrics']):
            lang=component(lyric['language'],12);suffix=PurePosixPath(lyric['file']).suffix
            target=stem+(('.'+lang) if i else '')+suffix
            delivery=asset(target,lyric['file'])
            original=delivery if lyric['original']==lyric['file'] else asset(stem+'.'+lang+'.source'+PurePosixPath(lyric['original']).suffix,lyric['original'])
            lyrics.append(dict(lyric,file=delivery,original=original))
        art=asset(directory+'/cover.jpg',p['art']) if p['art'] else ''
        records.append(dict(p,track=path,art=art,lyrics=lyrics))
    return files,records,paths,unique,asset
