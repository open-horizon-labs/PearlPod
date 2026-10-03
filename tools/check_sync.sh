#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I vendor tests/test_export_compat.c main/metadata.c main/artwork.c -lm -o /tmp/pearl-export-compat
cc -DPEARL_FTP_HOST -fsanitize=address,undefined -I tests -I vendor/ftp tests/ftp_host.c vendor/ftp/ftp.c vendor/ftp/async_writer.c -pthread -o /tmp/pearl-ftp-host
cc -Wall -Wextra -Werror -fsanitize=address,undefined -include stdlib.h -I main tests/test_lyrics.c main/lyrics.c -o /tmp/pearl-lyrics-tests
cc -DPEARL_FTP_HOST -Wall -Wextra -Werror -fsanitize=address,undefined -I vendor/ftp tests/test_async_writer.c vendor/ftp/async_writer.c -pthread -o /tmp/pearl-async-writer-tests
/tmp/pearl-async-writer-tests
/tmp/pearl-lyrics-tests
.sync-venv/bin/python tests/test_syncer.py
