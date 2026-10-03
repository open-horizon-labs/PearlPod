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


def inventory(address, port):
    """One bounded FTP listing, avoiding one request per object."""
    found = {}
    with ftplib.FTP() as ftp:
        ftp.connect(str(address), port, timeout=15)
        ftp.login()
        def entry(line):
            fields=line.split(None,8)
            if len(fields)!=9 or not fields[0].startswith('-'):return
            name=fields[8]
            if not re.fullmatch(r'[a-f0-9]{64}\.(mp3|jpg|lrc|srt|vtt|txt|m3u8)',name):return
            size=int(fields[4])
            if size<0:raise ValueError('Invalid inventory size')
            found[name]=size
            if len(found)>16384:raise ValueError('Inventory too large')
        try:ftp.retrlines('LIST /objects',entry)
        except ftplib.error_perm as e:
            if not str(e).startswith('550'):raise
        ftp.quit()
    return found


class CapacityError(OSError):
    pass


def report_failure(address, port, reason):
    """A small terminal marker lets the Pod stop WiFi promptly on host failure."""
    if reason not in ('card_full','transfer_failed'):raise ValueError('Invalid failure')
    with ftplib.FTP() as ftp:
        ftp.connect(str(address),port,timeout=5);ftp.login()
        ftp.storbinary('STOR /error.txt',io.BytesIO((reason+'\n').encode()))
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
    catalog=cache/'catalogs'/sha
    if free_bytes is not None:
        required={}
        for line in catalog.read_text().splitlines():
            record=json.loads(line)
            if 'file' in record:required[record['file']]=record['bytes']
        before=time.monotonic()
        existing=inventory(ip,port)
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
            if not re.fullmatch(r'[a-f0-9]{64}\.(mp3|jpg|lrc|srt|vtt|txt|m3u8)',name): raise ValueError('Unsafe object name')
            os.link(cache/'objects'/name,staged/name)
        marker=Path(directory)/'ready';marker.write_text(sha+'\n')
        script='\n'.join([
            'set cmd:fail-exit yes', 'set cmd:move-background no',
            'set net:timeout 15', 'set net:max-retries 2', 'set net:reconnect-interval-base 2',
            'set ftp:ssl-allow no', 'set ftp:passive-mode yes', 'set ftp:use-mlsd no',
            'set ftp:use-feat no', 'set ftp:use-site-utime no', 'set ftp:use-site-chmod no',
            f'open -u anonymous,pearlpod ftp://{ip}:{port}',
            'set cmd:fail-exit no', 'mkdir -p objects catalogs', 'set cmd:fail-exit yes',
            'mirror --reverse --ignore-time --no-perms --parallel=1 '+quote(staged)+' /objects',
            'put '+quote(catalog)+' -o /catalogs/'+sha,
            'put '+quote(marker)+' -o /ready.tmp',
            'mv /ready.tmp /ready', 'bye',
        ])+'\n'
        path=Path(directory)/'transfer.lftp';path.write_text(script)
        # No shell and no credentials; capture output instead of logging household data.
        mark('staging_ms',before)
        before=time.monotonic()
        subprocess.run(['lftp','-f',str(path)],check=True,timeout=timeout,capture_output=True)
        mark("lftp_ms",before)
        mark("total_ms",started)
    return sha
