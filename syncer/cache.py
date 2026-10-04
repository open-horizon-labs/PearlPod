"""Host-only retention. Call exclusively while publication/delivery is locked."""
import json
import time


def maintain(cache, limit, reserve=0):
    cache.mkdir(parents=True, exist_ok=True)
    # Only this service's abandoned scratch files; no traversal outside cache.
    for path in cache.rglob('*'):
        if path.is_file() and (path.name.endswith('.tmp') or path.name.endswith('.preparing.mp3')):
            path.unlink()
    head = None
    previous = None
    try:
        saved_head = json.loads((cache/'head.json').read_text())
        head = saved_head['catalog']
        previous = saved_head.get('previous_catalog')
    except FileNotFoundError:
        pass
    except (ValueError, KeyError):
        # A damaged head is repairable, but never authorizes reclamation.
        used = sum(p.stat().st_size for p in cache.rglob("*") if p.is_file())
        if used + reserve > limit:raise OSError("Preparation cache budget exceeded")
        return used
    catalogs = sorted((cache/'catalogs').glob('*'), key=lambda p:p.stat().st_mtime, reverse=True)
    keep = {p.name for p in catalogs[:2]}
    if head:
        keep.add(head)
    if previous:
        keep.add(previous)
    # Retain recent generations so a failed/just-finished device job can retry.
    keep.update(p.name for p in catalogs if time.time()-p.stat().st_mtime < 86400)
    referenced = set()
    for path in catalogs:
        if path.name not in keep:
            path.unlink()
            continue
        for line in path.read_text().splitlines():
            row = json.loads(line)
            if 'file' in row:
                referenced.add(row.get('cache_source', row['file']))
    for path in (cache/'objects').glob('*'):
        if path.name not in referenced and time.time()-path.stat().st_mtime > 86400:
            path.unlink()
    for folder in ('prepared', 'sources', 'tracks'):
        for path in (cache/folder).glob('*.json'):
            if time.time()-path.stat().st_mtime > 86400:
                path.unlink()
    return enforce_budget(cache, limit, reserve)


def enforce_budget(cache, limit, reserve=0):
    import shutil
    used = sum(p.stat().st_size for p in cache.rglob('*') if p.is_file())
    if used + reserve > limit or shutil.disk_usage(cache).free < reserve + 4*1024*1024:
        raise OSError('Preparation cache budget exceeded')
    return used
