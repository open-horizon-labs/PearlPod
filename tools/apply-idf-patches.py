#!/usr/bin/env python3
"""Apply the upstream SPI ISR race fix, without touching other SDK revisions."""
import hashlib
import os
from pathlib import Path
import subprocess

sdk = Path(os.environ['IDF_PATH'])
source = sdk / 'components/esp_hw_support/spi_bus_lock.c'
before = 'e3a431ce00e5afcb5e43797a9d0857fcedd87dbba6f6e63dcf56c2aadaae20ab'
after = '255730f40ae2e4cb88419cb7fb7462d3afa07d4a5f0c842c0d3aab860105b12f'
digest = hashlib.sha256(source.read_bytes()).hexdigest()
if digest == before:
    patch = Path(__file__).parent / 'idf-patches/spi-bg-exit-race.patch'
    subprocess.run(['git', '-C', str(sdk), 'apply', str(patch.resolve())], check=True)
elif digest != after:
    raise SystemExit('Unexpected SPI bus lock source; review SDK changes before building.')
assert hashlib.sha256(source.read_bytes()).hexdigest() == after
print('ESP-IDF SPI ISR race fix a16c003 applied and verified')
