#!/usr/bin/env python3
"""Exercise the running real NAS publisher against the shared FTP host harness.

This does not exercise the physical Pod. Start syncer/server.py first and build
/tmp/pearl-ftp-host with tools/check_sync.sh. No audio is synthesized.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host',required=True)
p.add_argument('--port',type=int,default=8787)
a=p.parse_args()
base=f'http://{a.host}:{a.port}'

def status():
    with urllib.request.urlopen(base+'/status',timeout=5) as response:
        return json.load(response)

def wait_for(predicate,seconds=180):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        result=predicate()
        if result:return result
        time.sleep(.2)
    raise TimeoutError('Acceptance check timed out')

def trigger(body):
    request=urllib.request.Request(base+'/sync',data=json.dumps(body).encode(),headers={'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(request,timeout=5) as response:return response.status
    except urllib.error.HTTPError as error:return error.code

wait_for(lambda:status().get('publisher')=='ready',seconds=660)
assert trigger([])==400
assert trigger({'ftp_port':True})==400
assert trigger({'ftp_port':2121,'free_bytes':True})==400
with tempfile.TemporaryDirectory(prefix='pearl-real-sync-') as directory:
    root=Path(directory)
    (root/'.pearl').mkdir()
    (root/'manual-note').write_text('Keep existing files')
    receiver=subprocess.Popen(['/tmp/pearl-ftp-host',str(root)],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        time.sleep(.2)
        assert trigger({'ftp_port':2121,'free_bytes':0})==202
        wait_for(lambda:(root/'.pearl/error.txt').is_file())
        assert (root/'.pearl/error.txt').read_text()=='card_full\n'
        assert not (root/'.pearl/ready').exists()
        assert not list(root.rglob('*.mp3'))
        time.sleep(.2)
        (root/'.pearl/error.txt').unlink()
        assert trigger({'ftp_port':2121,'free_bytes':1024*1024*1024})==202
        wait_for(lambda:status().get('status')=='uploaded')
        sha=(root/'.pearl/ready').read_text().strip()
        catalog=root/'.pearl/catalogs'/sha
        records=[json.loads(line) for line in catalog.read_text().splitlines()]
        tracks=[record['track'] for record in records if 'track' in record]
        assert tracks, 'Select a real PP: playlist first'
        before={name:(root/name).stat().st_mtime_ns for name in tracks}
        assert trigger({'ftp_port':2121,'free_bytes':1024*1024*1024})==202
        wait_for(lambda:status().get('status')=='uploaded')
        assert before=={name:(root/name).stat().st_mtime_ns for name in tracks}
        assert (root/'manual-note').read_text()=='Keep existing files'
        print(f'Real NAS HTTP -> native FTP: {len(tracks)} tracks; card-full fails before upload; no-op leaves audio untouched')
    finally:
        receiver.terminate()
        _,errors=receiver.communicate(timeout=5)
        assert b'AddressSanitizer' not in errors and b'runtime error:' not in errors,errors.decode()
