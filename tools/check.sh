#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_library.c main/library.c main/metadata.c main/playlist.c -o /tmp/pearl-library-tests
/tmp/pearl-library-tests
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I vendor tests/test_artwork.c main/artwork.c main/metadata.c -lm -o /tmp/pearl-art-tests
/tmp/pearl-art-tests
.venv/bin/python tests/test_prepare.py

cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_catalog.c main/library.c main/metadata.c main/playlist.c -o /tmp/pearl-catalog-tests
/tmp/pearl-catalog-tests

cc -Wall -Wextra -Werror -fsanitize=address,undefined -Dmalloc=pearl_test_malloc -Drealloc=pearl_test_realloc -I main tests/test_catalog_oom.c main/library.c main/metadata.c main/playlist.c -o /tmp/pearl-catalog-oom
/tmp/pearl-catalog-oom

cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_metadata.c main/metadata.c -o /tmp/pearl-metadata-tests
/tmp/pearl-metadata-tests

cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_wifi_policy.c -o /tmp/pearl-wifi-policy
/tmp/pearl-wifi-policy
node tests/test_portal.js

cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I vendor tests/test_wifi_config.c main/wifi_config.c vendor/tomlc17.c -lm -o /tmp/pearl-wifi-config
/tmp/pearl-wifi-config
python3 tests/test_recovery.py
