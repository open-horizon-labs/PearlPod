#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_library.c main/library.c -o /tmp/pearl-library-tests
/tmp/pearl-library-tests
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main -I vendor tests/test_artwork.c main/artwork.c -lm -o /tmp/pearl-art-tests
/tmp/pearl-art-tests
.venv/bin/python tests/test_prepare.py
