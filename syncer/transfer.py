"""Single-session lftp delivery; completion marker is uploaded last."""
import ftplib
import io
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def quote(value):
    # lftp's command language is not a shell; quote its own metacharacters.
    value=str(value)
    if any(c in value for c in '\r\n\x00'): raise ValueError('Invalid path')
    return '"'+value.replace('\\','\\\\').replace('"','\\"').replace('$','\\$').replace('`','\\`')+'"'


def inventory(address, port, normal=False):
    """One bounded FTP listing, avoiding one request per object."""
    found = {}
    with ftplib.FTP() as ftp:
        ftp.connect(str(address), port, timeout=15)
        ftp.login()
        pending=[''] if normal else ['objects']
        while pending:
            directory=pending.pop(); entries=[]
            try:ftp.retrlines('LIST /'+directory,entries.append)
            except ftplib.error_perm as e:
                if str(e).startswith('550'):continue
                raise
            for line in entries:
                fields=line.split(None,8)
                if len(fields)!=9:continue
                name=fields[8]
                if name.startswith('.'):continue
                path=(directory+'/'+name).lstrip('/')
                if fields[0].startswith('d') and normal:pending.append(path)
                elif fields[0].startswith('-'):
                    if not normal and not re.fullmatch(r'[a-f0-9]{64}\.(mp3|jpg|lrc|srt|vtt|txt|m3u8)',name):continue
                    found[path if normal else name]=int(fields[4])
                if len(found)+len(pending)>16384:raise ValueError('Inventory too large')
        ftp.quit()
    return found


class CapacityError(OSError):
    pass


def report_failure(address, port, reason, normal=False):
    """A small terminal marker lets the Pod stop WiFi promptly on host failure."""
    if reason not in ('card_full','transfer_failed'):raise ValueError('Invalid failure')
    with ftplib.FTP() as ftp:
        ftp.connect(str(address),port,timeout=5);ftp.login()
        ftp.storbinary('STOR '+('/.pearl/error.txt' if normal else '/error.txt'),io.BytesIO((reason+'\n').encode()))
        ftp.quit()


def capacity(required, existing, free_bytes, catalog_bytes):
    # Full mismatched files are budgeted conservatively; active assets are never deleted.
    missing=[(name,size) for name,size in required.items() if existing.get(name)!=size]
    needed=sum(size+32768 for _,size in missing)+catalog_bytes+4*1024*1024
    if type(free_bytes) is not int or free_bytes<0:raise ValueError('Invalid free space')
    if needed>free_bytes:raise CapacityError('Insufficient card space')
    return needed


def deliver(cache, head, address, port=21, timeout=7200, free_bytes=None, trace=None):
    ip=ipaddress.ip_address(address)
    if ip.version!=4 or not ip.is_private or ip.is_loopback or ip.is_multicast or ip.is_unspecified:
        raise ValueError('Expected a local Pod IPv4 address')
    if not 1<=port<=65535: raise ValueError('Invalid FTP port')
    started=time.monotonic()
    metrics={}
    def mark(name,before):
        metrics[name]=round((time.monotonic()-before)*1000,2)
        if trace:trace(dict(metrics))
    sha=head['catalog']
    if not re.fullmatch('[a-f0-9]{64}',sha): raise ValueError('Invalid catalog hash')
    normal=head.get('format')==2
    metadata='/.pearl' if normal else ''
    catalog=cache/'catalogs'/sha
    if free_bytes is not None:
        required={}
        for line in catalog.read_text().splitlines():
            record=json.loads(line)
            if 'file' in record:required[record['file']]=record['bytes']
        before=time.monotonic()
        existing=inventory(ip,port,normal)
        metrics.update(object_count=len(required),object_bytes=sum(required.values()),missing_objects=sum(existing.get(n)!=b for n,b in required.items()),missing_bytes=sum(b for n,b in required.items() if existing.get(n)!=b))
        mark("inventory_ms",before)
        capacity(required,existing,free_bytes,catalog.stat().st_size)
    before=time.monotonic()
    with tempfile.TemporaryDirectory(dir=cache,prefix='delivery-') as directory:
        staged=Path(directory)/'objects';staged.mkdir()
        for line in catalog.read_text().splitlines():
            record=json.loads(line)
            if 'file' not in record: continue
            name=record['file']
            from layout import valid_path
            if normal:
                if not valid_path(name):raise ValueError('Unsafe music path')
                source=record['cache_source']
            else:source=name
            if not re.fullmatch(r'[a-f0-9]{64}\.(mp3|jpg|lrc|srt|vtt|txt|m3u8)',source):raise ValueError('Unsafe cache source')
            target=staged/name;target.parent.mkdir(parents=True,exist_ok=True)
            os.link(cache/'objects'/source,target)
        marker=Path(directory)/'ready';marker.write_text(sha+'\n')
        script='\n'.join([
            'set cmd:fail-exit yes', 'set cmd:move-background no',
            'set xfer:use-temp-file '+('yes' if normal else 'no'),
            'set net:timeout 15', 'set net:max-retries 2', 'set net:reconnect-interval-base 2',
            'set ftp:ssl-allow no', 'set ftp:passive-mode yes', 'set ftp:use-mlsd no',
            'set ftp:use-feat no', 'set ftp:use-site-utime no', 'set ftp:use-site-chmod no',
            f'open -u anonymous,pearlpod ftp://{ip}:{port}',
            'set cmd:fail-exit no', 'mkdir -p '+('.pearl/catalogs' if normal else 'objects catalogs'), 'set cmd:fail-exit yes',
            'mirror --reverse --ignore-time --no-perms --parallel=1 '+quote(staged)+(' /' if normal else ' /objects'),
            'put '+quote(catalog)+' -o '+metadata+'/catalogs/'+sha,
            'put '+quote(marker)+' -o '+metadata+'/ready.tmp',
            'mv '+metadata+'/ready.tmp '+metadata+'/ready', 'bye',
        ])+'\n'
        path=Path(directory)/'transfer.lftp';path.write_text(script)
        # No shell and no credentials; capture output instead of logging household data.
        mark('staging_ms',before)
        before=time.monotonic()
        subprocess.run(['lftp','-f',str(path)],check=True,timeout=timeout,capture_output=True)
        mark("lftp_ms",before)
        mark("total_ms",started)
    return sha
