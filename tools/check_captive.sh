#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_captive_dns.c main/captive_dns.c -o /tmp/pearl-captive-dns-tests
/tmp/pearl-captive-dns-tests
node tests/test_portal.js
