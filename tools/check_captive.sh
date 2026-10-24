#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
shared=shared/embedded-services/components
export C_INCLUDE_PATH="${C_INCLUDE_PATH:+$C_INCLUDE_PATH:}$PWD/$shared/device_input/include:$PWD/$shared/device_image/include:$PWD/$shared/device_power/include:$PWD/$shared/wifi_station/include:$PWD/$shared/wifi_profiles/include:$PWD/$shared/wifi_provisioning/include:$PWD/$shared/tomlc17/include"
cc -Wall -Wextra -Werror -fsanitize=address,undefined -I main tests/test_captive_dns.c "$shared/captive_dns/captive_dns.c" -I "$shared/captive_dns/include" -o /tmp/pearl-captive-dns-tests
/tmp/pearl-captive-dns-tests
node tests/test_portal.js
