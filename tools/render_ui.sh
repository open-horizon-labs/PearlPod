#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
shared=shared/embedded-services/components
export C_INCLUDE_PATH="${C_INCLUDE_PATH:+$C_INCLUDE_PATH:}$PWD/$shared/device_input/include:$PWD/$shared/device_image/include:$PWD/$shared/device_power/include:$PWD/$shared/wifi_station/include:$PWD/$shared/wifi_profiles/include:$PWD/$shared/wifi_provisioning/include:$PWD/$shared/tomlc17/include"
mkdir -p "${1:-docs/ui/current}"
cc -O1 -DPEARL_UI_HOST -DLV_CONF_INCLUDE_SIMPLE -I tests/ui_host -I main -I vendor -I managed_components/lvgl__lvgl tests/ui_host/render.c main/library.c main/metadata.c main/playlist.c "$shared/device_input/device_input.c" main/artwork.c "$shared/device_image/device_image.c" main/lyrics.c main/fallback.c main/theme.c "$shared/tomlc17/tomlc17.c" $(find managed_components/lvgl__lvgl/src -name '*.c') -lm -o /tmp/pearl-ui-render
/tmp/pearl-ui-render "${1:-docs/ui/current}"
.venv/bin/python - "${1:-docs/ui/current}" <<'PY'
from PIL import Image
from pathlib import Path
import sys
for p in Path(sys.argv[1]).glob('*.ppm'):
    Image.open(p).save(p.with_suffix('.png'))
    p.unlink()
PY
