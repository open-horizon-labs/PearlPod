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
    extra=b'previous-only asset';extra_name=hashlib.sha256(extra).hexdigest()+'.txt'
    protected=dest/'objects'/extra_name;protected.write_bytes(extra)
    orphan=dest/'objects'/(hashlib.sha256(b'orphan').hexdigest()+'.txt');orphan.write_bytes(b'orphan')
    (dest/'objects'/'unmanaged.txt').write_text('keep')
    (root/'music'/'manual.txt').write_text('keep')
    first_data=(json.dumps({'file':extra_name,'bytes':len(extra)})+'\n').encode()+data
    first=hashlib.sha256(first_data).hexdigest();(dest/'catalogs'/first).write_bytes(first_data)
    rows=[json.loads(line) for line in data.splitlines()]
    for row in rows:
        if 'playlist' in row:row['title']='Renamed test playlist'
    data=b''.join(json.dumps(row,sort_keys=True).encode()+b'\n' for row in rows)
    second=hashlib.sha256(data).hexdigest();(dest/'catalogs'/second).write_bytes(data)
    for row in rows:
        if 'playlist' in row:row['title']='Third selection'
    third_data=b''.join(json.dumps(row,sort_keys=True).encode()+b'\n' for row in rows)
    third=hashlib.sha256(third_data).hexdigest();(dest/'catalogs'/third).write_bytes(third_data)
    data=b'{'*30+b'}'*30+b'\n';bad=hashlib.sha256(data).hexdigest();(dest/'catalogs'/bad).write_bytes(data)
    empty_data=b'{"format":1}\n';empty=hashlib.sha256(empty_data).hexdigest();(dest/'catalogs'/empty).write_bytes(empty_data)
    subprocess.run(['/tmp/pearl-managed-check',first,second,bad,third,str(protected),str(orphan),empty],check=True)
finally:
    if root.exists():shutil.rmtree(root)
