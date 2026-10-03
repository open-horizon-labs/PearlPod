"""Real prepared catalog through firmware loader, with simulated NVS failure."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
root=Path('/tmp/pearl-managed-tests');cache=Path('.sync-state/cache')
try:
    dest=root/'music'/'.pearl';(dest/'objects').mkdir(parents=True,exist_ok=True);(dest/'catalogs').mkdir(exist_ok=True)
    head=json.loads((cache/'head.json').read_text());data=(cache/'catalogs'/head['catalog']).read_bytes()
    for line in data.splitlines():
        record=json.loads(line)
        if 'file' in record:os.link(cache/'objects'/record['file'],dest/'objects'/record['file'])
    first=hashlib.sha256(data).hexdigest();(dest/'catalogs'/first).write_bytes(data)
    rows=[json.loads(line) for line in data.splitlines()]
    for row in rows:
        if 'playlist' in row:row['title']='Renamed test playlist'
    data=b''.join(json.dumps(row,sort_keys=True).encode()+b'\n' for row in rows)
    second=hashlib.sha256(data).hexdigest();(dest/'catalogs'/second).write_bytes(data)
    data=b'{'*30+b'}'*30+b'\n';bad=hashlib.sha256(data).hexdigest();(dest/'catalogs'/bad).write_bytes(data)
    subprocess.run(['/tmp/pearl-managed-check',first,second,bad],check=True)
finally:
    if root.exists():shutil.rmtree(root)
