#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
shared=shared/embedded-services/components
export C_INCLUDE_PATH="${C_INCLUDE_PATH:+$C_INCLUDE_PATH:}$PWD/$shared/device_input/include:$PWD/$shared/device_image/include:$PWD/$shared/device_power/include:$PWD/$shared/wifi_station/include:$PWD/$shared/wifi_profiles/include:$PWD/$shared/wifi_provisioning/include:$PWD/$shared/tomlc17/include"
. tools/host_media.sh
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I vendor tests/test_export_compat.c main/metadata.c main/artwork.c "$shared/device_image/device_image.c" -lm -o /tmp/pearl-export-compat
: "${IDF_PATH:?Source ESP-IDF 5.5.5 export.sh first}"
python tests/test_spi_race.py
python tests/test_sync_discovery.py
python tests/test_ftp_send.py
cc -Wall -Wextra -Werror -fsanitize=address,undefined -Drename=pearl_test_rename -I vendor/ftp tests/test_replace.c vendor/ftp/replace.c -o /tmp/pearl-replace-tests
/tmp/pearl-replace-tests
cc -Wno-deprecated-declarations -fsanitize=address,undefined -I "$IDF_PATH/components/json/cJSON" -c "$IDF_PATH/components/json/cJSON/cJSON.c" -o /tmp/pearl-cjson.o
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I "$IDF_PATH/components/json/cJSON" tests/test_sync_progress.c main/sync_progress.c /tmp/pearl-cjson.o -lm -o /tmp/pearl-sync-progress-tests
/tmp/pearl-sync-progress-tests
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_resume_library.c main/library.c main/metadata.c main/playlist.c "$shared/device_input/device_input.c" $media_link -lm -o /tmp/pearl-resume-library
cc -DPEARL_FTP_HOST -DDEVICE_FILE_WRITER_HOST -I "$shared/async_file_writer/include" -fsanitize=address,undefined -I tests -I main -I vendor/ftp -I "$IDF_PATH/components/json/cJSON" tests/ftp_host.c main/sync_progress.c /tmp/pearl-cjson.o vendor/ftp/ftp.c "$shared/async_file_writer/device_file_writer.c" vendor/ftp/replace.c -pthread -lm -o /tmp/pearl-ftp-host
cc -Wall -Wextra -Werror -fsanitize=address,undefined -include stdlib.h -I main tests/test_lyrics.c main/lyrics.c -o /tmp/pearl-lyrics-tests
cc -DPEARL_FTP_HOST -DDEVICE_FILE_WRITER_HOST -I "$shared/async_file_writer/include" -Wall -Wextra -Werror -fsanitize=address,undefined -I vendor/ftp tests/test_async_writer.c "$shared/async_file_writer/device_file_writer.c" -pthread -o /tmp/pearl-async-writer-tests
/tmp/pearl-async-writer-tests
/tmp/pearl-lyrics-tests
printf "%s\n" "Host service checks moved to media-prep-service/tools/check.sh; run them from that repository."
