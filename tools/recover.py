#!/usr/bin/env python3
"""Catch a briefly enumerating PearlPod and restore a known-working application."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import time
from datetime import datetime

ROOT = Path(__file__).resolve().parent.parent
IMAGE = ROOT / 'dist/known-working-e1e3b76.bin'
EXPECTED_SHA = 'bed096a46f7fc45762b2fbc013e70e9abe526f70db7dd6565d2847871be872c1'


def enumerate_ports():
    from serial.tools import list_ports
    return list_ports.comports()

def candidates(ports, port=None):
    # USB identity establishes an Espressif S3 interface, not an exact board.
    # Never choose arbitrarily when multiple matching devices are connected.
    matches = [p.device for p in ports if p.vid == 0x303a and getattr(p, 'pid', None) == 0x1001 and (port is None or p.device == port)]
    return matches if len(matches) == 1 else []

def run_attempts(attempts, port, image, log, *, enumerate_ports=enumerate_ports, run=subprocess.run, now=time.monotonic, sleep=time.sleep):
    for attempt in range(1, attempts + 1):
        deadline = now() + 3
        matched = []
        while now() < deadline:
            matched = candidates(enumerate_ports(), port)
            if matched:
                break
            sleep(.05)
        if not matched:
            print(f'Attempt {attempt}/{attempts}: player not detected.', flush=True)
            continue
        command = [sys.executable, '-m', 'esptool', '--chip', 'esp32s3', '--port', matched[0], '--before', 'usb_reset', '--connect-attempts', '1', '--baud', '460800', 'write_flash', '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '16MB', '0x10000', str(image)]
        print(f'Attempt {attempt}/{attempts}: flashing {matched[0]}.', flush=True)
        log.write(f'\nAttempt {attempt}: {matched[0]}\n'); log.flush()
        try:
            result = run(command, stdout=log, stderr=subprocess.STDOUT, timeout=60, cwd=ROOT)
        except (subprocess.TimeoutExpired, OSError) as error:
            log.write(f'Attempt failed: {type(error).__name__}\n'); log.flush()
            continue
        if result.returncode == 0:
            print('Known-working firmware restored; flash verified by esptool.', flush=True)
            return True
    print('All recovery attempts exhausted. See the log for connection errors.', flush=True)
    return False

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--attempts', type=int, default=20)
    parser.add_argument('--port', help='Optional port constraint; USB vendor/product identity is still checked')
    parser.add_argument('--check', action='store_true', help='Verify recovery image and report detection without flashing')
    args = parser.parse_args()
    if not 1 <= args.attempts <= 100:
        parser.error('--attempts must be between 1 and 100')
    if hashlib.sha256(IMAGE.read_bytes()).hexdigest() != EXPECTED_SHA:
        raise SystemExit('Recovery image checksum mismatch; no flash attempted.')
    print('Recovery image verified: device-tested application e1e3b76; app only, preferences preserved.', flush=True)
    if args.check:
        print('Matching player ports:', ', '.join(candidates(enumerate_ports(), args.port)) or 'none')
        return 0
    folder = ROOT / 'backups'; folder.mkdir(exist_ok=True)
    path = folder / f'recovery-{datetime.now():%Y%m%d-%H%M%S}.log'
    print(f'Log: {path}', flush=True)
    with path.open('w') as log:
        return 0 if run_attempts(args.attempts, args.port, IMAGE, log) else 1

if __name__ == '__main__':
    raise SystemExit(main())
