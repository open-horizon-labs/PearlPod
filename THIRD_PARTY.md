# Sources and licenses

Board display/touch support derives from the FakePod project attachment recorded in docs/HARDWARE.md. Preserve component license files and upstream notices.

Vendored decoder/image headers preserve their own license text: minimp3 from https://github.com/lieff/minimp3; dr_flac and dr_wav from https://github.com/mackron/dr_libs; stb_image from https://github.com/nothings/stb. Managed components are pinned by dependencies.lock: ESP-IDF v5.5.5, LVGL 8.4.0 and Espressif touch support. Consult each dependency’s included license before redistribution.

Artwork under theme-packs/ was generated with OpenAI image generation for this personal project, inspired by the user’s Sample soundtrack and Sample collection request. It is not official franchise artwork. PNG sources have fixed-size RGB565 derivatives for the card; no character artwork is compiled into firmware.

- `main/wifi_policy.h` adapts T-Dongle strongest-network selection with hysteresis, copyright 2026 白一百 baiyibai, MIT; the complete notice is in `licenses/tdongle-MIT.txt`. roon-knob scan/portal patterns informed the independent implementation; no roon-knob source or stored credentials were copied. Exact reference commits are recorded in `docs/WIFI-EXECUTION.md`.

- `vendor/tomlc17.c` and `.h`: CK Tan’s TOML parser, pinned at 8d3766ddafa6515417f4885ae6878ef69fac401e, MIT notice in `licenses/tomlc17-MIT.txt`. Local changes limit bracket/brace nesting to six for bounded ESP32 recursion and cast a uint32_t printf argument to unsigned for Xtensa ABI portability. The obsolete tomlc99 version was inspected and replaced by its successor before implementation.

Public packs contain original palettes and greetings; neutral synthetic image fixtures exercise parser/allocation tests. Personal character artwork is excluded from public source and packages.
