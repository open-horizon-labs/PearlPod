#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I vendor tests/test_export_compat.c main/metadata.c main/artwork.c -lm -o /tmp/pearl-export-compat
cc -DPEARL_FTP_HOST -fsanitize=address,undefined -I tests -I vendor/ftp tests/ftp_host.c vendor/ftp/ftp.c -o /tmp/pearl-ftp-host
cc -Wall -Wextra -Werror -fsanitize=address,undefined -include stdlib.h -I main tests/test_lyrics.c main/lyrics.c -o /tmp/pearl-lyrics-tests
/tmp/pearl-lyrics-tests
.sync-venv/bin/python tests/test_syncer.py
