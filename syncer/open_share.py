"""Materialize one prepared catalog as ordinary files in a dedicated share directory."""
import json
import os
from pathlib import Path
import shutil
import tempfile

from layout import valid_path

MANIFEST = '.pearlpod-export.json'


def _safe_parent(root, relative):
    current = root
    for part in Path(relative).parts[:-1]:
        current = current / part
        if current.is_symlink():
            raise ValueError('Open-share path contains a symlink')
        if current.exists():
            if not current.is_dir():
                raise ValueError('Open-share path component is not a directory')
        else:
            current.mkdir()
    return current


def _write_atomic(source, target):
    fd, temporary = tempfile.mkstemp(dir=target.parent, prefix='.pearlpod-write-')
    try:
        with os.fdopen(fd, 'wb') as output, source.open('rb') as input_file:
            shutil.copyfileobj(input_file, output, 1024 * 1024)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, target)
    finally:
        Path(temporary).unlink(missing_ok=True)


def materialize(cache, head, output):
    """Copy the current catalog to the share, touching only exporter-owned files."""
    output = Path(output)
    if output.is_symlink():
        raise ValueError('Open-share directory cannot be a symlink')
    output.mkdir(parents=True, exist_ok=True)
    root = output.resolve(strict=True)
    manifest_path = root / MANIFEST
    previous = {'files': {}}
    if manifest_path.exists():
        if manifest_path.is_symlink() or not manifest_path.is_file():
            raise ValueError('Open-share manifest path is not a regular file')
        previous = json.loads(manifest_path.read_text())
        if not isinstance(previous, dict) or not isinstance(previous.get('files'), dict):
            raise ValueError('Invalid open-share manifest')
    old_files = previous['files']

    catalog = cache / 'catalogs' / head['catalog']
    desired = {}
    for line in catalog.read_text().splitlines()[1:]:
        row = json.loads(line)
        if 'file' not in row:
            continue
        relative = row['file']
        source_name = row.get('cache_source', relative)
        if not valid_path(relative) or Path(source_name).name != source_name:
            raise ValueError('Invalid open-share catalog path')
        desired[relative] = source_name

    unchanged = previous.get('catalog') == head['catalog'] and old_files == desired
    if unchanged:
        for relative in desired:
            target = root / relative
            if target.is_symlink() or not target.is_file():
                unchanged = False
                break
    if unchanged:
        return

    for relative, source_name in desired.items():
        parent = _safe_parent(root, relative)
        target = parent / Path(relative).name
        if target.is_symlink():
            raise ValueError('Open-share target cannot be a symlink')
        if target.exists() and relative not in old_files:
            raise ValueError('Open-share file already exists outside exporter ownership')
        source = cache / 'objects' / source_name
        if not source.is_file():
            raise ValueError('Prepared open-share object is missing')
        if old_files.get(relative) != source_name or not target.is_file():
            _write_atomic(source, target)

    for relative in old_files.keys() - desired.keys():
        if not valid_path(relative):
            raise ValueError('Invalid path in open-share manifest')
        parent = _safe_parent(root, relative)
        target = parent / Path(relative).name
        if target.is_symlink():
            raise ValueError('Open-share target cannot be a symlink')
        if target.is_file():
            target.unlink()
            parent = target.parent
            while parent != root:
                try:
                    parent.rmdir()
                except OSError:
                    break
                parent = parent.parent

    fd, temporary = tempfile.mkstemp(dir=root, prefix='.pearlpod-manifest-')
    try:
        with os.fdopen(fd, 'w', encoding='utf-8') as output_file:
            json.dump({'catalog': head['catalog'], 'files': desired}, output_file,
                      ensure_ascii=False, sort_keys=True)
            output_file.write('\n')
            output_file.flush()
            os.fsync(output_file.fileno())
        os.replace(temporary, manifest_path)
    finally:
        Path(temporary).unlink(missing_ok=True)
