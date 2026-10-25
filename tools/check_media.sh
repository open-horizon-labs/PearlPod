#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
shared=shared/embedded-services/components
. tools/host_media.sh
python3 tests/test_media_commands.py
cc -D_POSIX_C_SOURCE=200809L -DDOWNLOAD_WRITER_HOST -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -I tests/pull_host -I main -I "$shared/device_transfer/include" tests/test_media_pull.c main/media_pull.c "$shared/media_library/media_transfer.c" "$shared/device_transfer/download_writer.c" "$shared/device_transfer/download_checkpoint.c" "$shared/device_transfer/http_range.c" "$shared/device_transfer/file_publication.c" "$media_objects/model.o" "$media_objects/json.o" -pthread -lm -lcrypto -o /tmp/pearl-media-pull
/tmp/pearl-media-pull
fixture=$(mktemp -d /tmp/pearl-seek.XXXXXX)
trap 'rm -rf "$fixture" "$media_objects"' EXIT HUP INT TERM
cc -O1 -fsanitize=address,undefined -I vendor tests/test_mp3_seek.c -lm -o /tmp/pearl-mp3-seek
for channels in 1 2; do
 for mode in cbr vbr; do
  if [ "$mode" = cbr ]; then encoding='-b:a 128k'; else encoding='-q:a 4'; fi
  ffmpeg -v error -f lavfi -i 'sine=frequency=440:duration=4' -ar 44100 -ac "$channels" -c:a libmp3lame $encoding "$fixture/$channels-$mode.mp3"
  /tmp/pearl-mp3-seek "$fixture/$channels-$mode.mp3"
 done
done
