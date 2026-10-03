#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
fixture_dir=$(mktemp -d /tmp/pearl-codecs.XXXXXX)
trap 'rm -rf "$fixture_dir"' EXIT
cc -O1 -fsanitize=address,undefined -I vendor -I main tests/test_flac_compat.c main/metadata.c -lm -o /tmp/pearl-flac-compat
for rate in 44100 48000 96000 192000; do
 for bits in 16 24; do
  if [ "$bits" = 16 ]; then wavcodec=pcm_s16le; else wavcodec=pcm_s24le; fi
  ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=1' -ar "$rate" -ac 2 -c:a "$wavcodec" "$fixture_dir/source.wav"
  for level in 0 5 12; do
   ffmpeg -v error -i "$fixture_dir/source.wav" -metadata title="Matrix song" -metadata artist="Listener" -metadata album="Compatibility" -metadata track="7" -c:a flac -compression_level "$level" "$fixture_dir/native.flac"
   /tmp/pearl-flac-compat "$fixture_dir/native.flac" "$fixture_dir/source.wav" "$rate" "$bits" 0
   rm "$fixture_dir/native.flac"
  done
  ffmpeg -v error -i "$fixture_dir/source.wav" -metadata title="Matrix song" -metadata artist="Listener" -metadata album="Compatibility" -metadata track="7" -c:a flac -f ogg "$fixture_dir/ogg.flac"
  /tmp/pearl-flac-compat "$fixture_dir/ogg.flac" "$fixture_dir/source.wav" "$rate" "$bits" 0
  python3 - "$fixture_dir" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]); data=(p/'ogg.flac').read_bytes()
(p/'damaged.flac').write_bytes(data[:len(data)//2])
PY
  /tmp/pearl-flac-compat "$fixture_dir/damaged.flac" "$fixture_dir/source.wav" "$rate" "$bits" 1
  ffmpeg -v error -i "$fixture_dir/source.wav" -metadata title="Matrix song" -metadata artist="Listener" -metadata album="Compatibility" -metadata track="7" -c:a flac "$fixture_dir/native.flac"
  python3 - "$fixture_dir" <<'PY'
from pathlib import Path
import sys
p=Path(sys.argv[1]);data=(p/'native.flac').read_bytes()
damaged=bytearray(data)
damaged[len(damaged)//2] ^= 0x5a
(p/'corrupt.flac').write_bytes(damaged)
(p/'prefixed.flac').write_bytes(b'ID3\x04\x00\x00\x00\x00\x00\x00'+data)
PY
  /tmp/pearl-flac-compat "$fixture_dir/prefixed.flac" "$fixture_dir/source.wav" "$rate" "$bits" 0
  /tmp/pearl-flac-compat "$fixture_dir/corrupt.flac" "$fixture_dir/source.wav" "$rate" "$bits" 1
  rm "$fixture_dir"/*
 done
done
