# microSD WiFi provisioning

## Aim and execution boundary

A grown-up can provision PearlPod’s WiFi from its microSD without joining a setup hotspot. The approved change adds file import alongside the existing browser setup. It preserves fast offline startup, leaves the radio off after importing, and keeps WebDAV/sync/playlist management outside this phase.

The configuration file is `wifi.toml` at the card root. It contains one to four `[[networks]]` entries or a single `[wlan]` section. The schema is intentionally for PearlPod: SSID, plaintext password and optional hidden flag; encrypted passwords and other Pi settings are rejected. The file defines the complete replacement list. A real TOML parser handles comments, UTF-8, quoted strings, escapes and repeated tables. Byte lengths and unknown fields are validated explicitly. The network worker imports after the library/UI are ready, and no network initialization or connection occurs because a credentials file exists.

## Accuracy report

| Claim | Status | Evidence | Recommended edit |
|---|---|---|---|
| There is a Pi convention using a TOML file with WiFi entries | Verified with edit | [Pi firstboot source](https://github.com/RPi-Distro/raspberrypi-sys-mods/blob/master/usr/lib/raspberrypi-sys-mods/firstboot), [init_config source](https://github.com/RPi-Distro/raspberrypi-sys-mods/blob/master/usr/lib/raspberrypi-sys-mods/init_config) | This first-boot implementation reads custom.toml with a single wlan table; repeated network entries here are a PearlPod extension. |
| The old wpa_supplicant.conf boot-folder trick works on current Pi OS | Stale/outdated | [Pi setup documentation](https://github.com/raspberrypi/documentation/blob/master/documentation/asciidoc/computers/getting-started/setting-up.adoc) | Official documentation says that behavior is unavailable from Bookworm onward. |
| Setup necessarily requires a phone/computer joined to the player hotspot | Verified with edit | The AP setup route needs a joined client; the new card-import path is separately tested | Hotspot joining is required only for browser provisioning, not microSD provisioning. |
| The complete imported list is saved before removing its file | Verified | Actual import code and callback fault-injection tests | Failed validation or storage preserves the file and previous profiles. |
| This candidate imports the real card and connects on the device | Needs citation | Local build and host tests only | Physical import/persistence/connection remain unverified; the device runs recovered c9e6efd. |

High-risk unresolved claims: device import and credential persistence across physical resets, WiFi connection and corrected radio runtime have not been observed. Claims requiring expert review: none for the checked schema/parser/source facts; physical device behavior requires device verification. Source gaps: the particular Pi OS image/version the user remembers is unspecified, so the existence of the checked implementation is established without promising universal current-image support. Corrections applied: custom.toml/wlan is the verified Pi precedent, multiple networks are a PearlPod addition, and browser joining is optional once the import-capable firmware is installed.

## Risk retirement

| Risk | Tempting wrong patch | Evidence and disposition |
|---|---|---|
| Quoting changes a password | Split each line on equals/hash | Retired by evidence: actual TOML parser handles literal/basic strings, quotes, backslashes, hashes, equals, Unicode, CRLF and BOM in tests. |
| A bad last entry partially overwrites profiles | Persist entries as they are read | Retired by evidence: parse uses a temporary list, duplicate/type/length/excess/unknown entries fail without changing outputs; injected persistence failure retains file/output. |
| Credentials disappear before persistence succeeds | Unlink before commit | Retired by evidence: test callback asserts file exists during persistence; failed persistence preserves it; successful persistence precedes removal. |
| Unicode SSID overflows fixed storage | Count characters instead of bytes | Retired by evidence: a 32-byte UTF-8 name passes, a 33-byte name fails. Embedded NUL and invalid UTF-8 are rejected. |
| Recursive parsing overflows the worker | Feed unbounded TOML directly to an 8 KiB task | Retired by local evidence: 8 KiB input cap, parser bracket/brace depth six, deep-input tests, source frame limits and stack reports. Hardware stack behavior remains part of the candidate device gate. |
| Provisioning delays music or starts radio | Connect in main startup when file exists | Retired by inspection: import runs after library readiness; WiFi driver initialization remains in explicit setup/connect actions. Actual startup timing with a real import remains unverified. |
| File persists on a read-only/problem card | Assume deletion always works | Explicit retained-file result and device status; repeated identical profiles avoid extra NVS commits. Physical cleanup-failure behavior needs a card fixture. |

## Validation

Local ESP-IDF v5.5.5 build and packaged SHA-256 checks pass. `tests/test_wifi_config.c` runs under ASan/UBSan against the vendored parser. Existing catalog/artwork/metadata, network selection, browser DOM and recovery tests remain in `tools/check.sh`. TOML example data uses placeholders and real `wifi.toml` files are ignored by Git. No real credential file was read or copied during implementation. `examples/wifi.toml.example` is the reviewed starting file.
