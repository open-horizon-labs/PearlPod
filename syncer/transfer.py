"""Single-session lftp delivery; completion marker is uploaded last."""
import ipaddress
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


def quote(value):
    # lftp's command language is not a shell; quote its own metacharacters.
    value=str(value)
    if any(c in value for c in '\r\n\x00'): raise ValueError('Invalid path')
    return '"'+value.replace('\\','\\\\').replace('"','\\"').replace('$','\\$').replace('`','\\`')+'"'


def deliver(cache, head, address, port=21, timeout=840):
    ip=ipaddress.ip_address(address)
    if ip.version!=4 or not ip.is_private or ip.is_loopback or ip.is_multicast or ip.is_unspecified:
        raise ValueError('Expected a local Pod IPv4 address')
    if not 1<=port<=65535: raise ValueError('Invalid FTP port')
    sha=head['catalog']
    if not re.fullmatch('[a-f0-9]{64}',sha): raise ValueError('Invalid catalog hash')
    catalog=cache/'catalogs'/sha
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
        subprocess.run(['lftp','-f',str(path)],check=True,timeout=timeout,capture_output=True)
    return sha
