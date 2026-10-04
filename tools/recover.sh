#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
[ -f dist/known-working-e1e3b76.bin ] || ./tools/fetch-firmware.sh
if [ ! -x .flash-venv/bin/python ]; then
 python3 -m venv .flash-venv
 .flash-venv/bin/python -m pip install 'esptool==4.12.0'
fi
exec .flash-venv/bin/python tools/recover.py "$@"
