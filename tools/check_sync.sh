#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I vendor tests/test_export_compat.c main/metadata.c main/artwork.c -lm -o /tmp/pearl-export-compat
: "${IDF_PATH:?Source ESP-IDF 5.5.5 export.sh first}"
cc -Wno-deprecated-declarations -fsanitize=address,undefined -I "$IDF_PATH/components/json/cJSON" -c "$IDF_PATH/components/json/cJSON/cJSON.c" -o /tmp/pearl-cjson.o
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I "$IDF_PATH/components/json/cJSON" tests/test_sync_progress.c main/sync_progress.c /tmp/pearl-cjson.o -lm -o /tmp/pearl-sync-progress-tests
/tmp/pearl-sync-progress-tests
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_resume_library.c main/library.c main/metadata.c main/playlist.c -o /tmp/pearl-resume-library
cc -DPEARL_FTP_HOST -fsanitize=address,undefined -I tests -I main -I vendor/ftp -I "$IDF_PATH/components/json/cJSON" tests/ftp_host.c main/sync_progress.c /tmp/pearl-cjson.o vendor/ftp/ftp.c vendor/ftp/async_writer.c -pthread -lm -o /tmp/pearl-ftp-host
cc -Wall -Wextra -Werror -fsanitize=address,undefined -include stdlib.h -I main tests/test_lyrics.c main/lyrics.c -o /tmp/pearl-lyrics-tests
cc -DPEARL_FTP_HOST -Wall -Wextra -Werror -fsanitize=address,undefined -I vendor/ftp tests/test_async_writer.c vendor/ftp/async_writer.c -pthread -o /tmp/pearl-async-writer-tests
/tmp/pearl-async-writer-tests
/tmp/pearl-lyrics-tests
.sync-venv/bin/python tests/test_syncer.py
.sync-venv/bin/python tests/test_sync_progress.py
.sync-venv/bin/python tests/test_exporter_automation.py
.sync-venv/bin/python tests/test_syncer_deploy.py
