#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p "${1:-docs/ui/current}"
cc -O1 -DPEARL_UI_HOST -DLV_CONF_INCLUDE_SIMPLE -I tests/ui_host -I main -I vendor -I managed_components/lvgl__lvgl tests/ui_host/render.c main/library.c main/artwork.c main/welcome.c $(find managed_components/lvgl__lvgl/src -name '*.c') -lm -o /tmp/pearl-ui-render
/tmp/pearl-ui-render "${1:-docs/ui/current}"
.venv/bin/python - "${1:-docs/ui/current}" <<'PY'
from PIL import Image
from pathlib import Path
import sys
for p in Path(sys.argv[1]).glob('*.ppm'):
    Image.open(p).save(p.with_suffix('.png'))
    p.unlink()
PY
