#!/bin/sh
# USB update without an ESP-IDF installation; never erases the NVS region.
set -eu
cd "$(dirname "$0")/.."
[ -f dist/SHA256SUMS ] || ./tools/fetch-firmware.sh
shasum -a 256 -c dist/SHA256SUMS
if [ "${1:-}" = --check ]; then exit 0; fi
port=${1:?Usage: tools/update.sh /dev/cu.usbmodem... (or --check)}
if [ ! -x .flash-venv/bin/python ]; then
 python3 -m venv .flash-venv
 .flash-venv/bin/python -m pip install 'esptool==4.12.0'
fi
.flash-venv/bin/python -m esptool --chip esp32s3 --port "$port" --baud 921600 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 dist/bootloader.bin 0x8000 dist/partition-table.bin 0x10000 dist/pearl-player.bin
