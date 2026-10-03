#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
port=${1:?Usage: tools/flash.sh /dev/cu.usbmodem...}
: "${IDF_PATH:?Source ESP-IDF 5.5.5 export.sh first}"
[ -f dist/pearl-player-merged.bin ] || { echo "Run tools/build.sh first"; exit 1; }
python -m esptool --chip esp32s3 --port "$port" --baud 921600 write_flash 0 dist/pearl-player-merged.bin
