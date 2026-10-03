#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${IDF_PATH:?Source ESP-IDF 5.5.5 export.sh first}"
version=$(git -C "$IDF_PATH" describe --tags --exact-match)
[ "$version" = v5.5.5 ] || { echo "Expected ESP-IDF v5.5.5, got $version"; exit 1; }
idf.py build
idf.py merge-bin -o pearl-player-merged.bin
mkdir -p dist
cp build/pearl-player-merged.bin dist/
cp build/pearl-player.bin build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/flasher_args.json dist/
shasum -a 256 dist/*.bin > dist/SHA256SUMS
