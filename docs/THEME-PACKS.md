# Person settings and theme packs

Firmware has a neutral music-note fallback, generic phrases and no compiled character artwork or personal name. Listener's content lives in `theme-packs/Default/`: Sample soundtrack and Sample collection themes, each with two welcome and two farewell scenes. A picture and its phrase are selected together. Album artwork takes priority during playback; absent artwork can use the theme's welcome illustration.

Copy the contents of `theme-packs/Default/` into the card's `music/` folder, alongside Artist/Album and Playlists. `Person.toml` contains the display name, favorite theme IDs and `rotate`. With rotation enabled, each boot advances to the next favorite; the next time a theme returns it advances its scene. With rotation disabled, the first theme remains selected while its scenes advance. The active palette and scenes remain stable throughout the listening session. The boot counter lives in NVS, avoiding writes to the music card. Names are rendered separately using `{name}`, never painted into artwork.

```toml
name = "Listener"
themes = ["midnight", "sakura"]
rotate = true
```

A theme directory contains `theme.toml`, its RGB565 image files and optional PNG sources. The manifest uses `version = 1`, a `title`, `[palette]` entries (`background`, `surface`, `text`, `accent`, `secondary`, all `#RRGGBB`) and up to eight `[[welcome]]` and eight `[[farewell]]` scenes. Each scene defines `image`, `heading` and `phrase`. Image names are basenames only. Firmware bounds each manifest to 8 KiB, names to 48 UTF-8 bytes, headings to 95 bytes and phrases to 127 bytes; invalid fields retain neutral defaults. Unsupported versions and invalid packs fall back. Missing, malformed or wrong-sized artwork retains the generic icon, with valid personalized text still available.

Images are exactly 240×240 RGB565, high byte first, matching the player’s `LV_COLOR_16_SWAP` configuration: 115,200 bytes each. `tools/prepare-theme-art.py` converts a generated 2×2 sheet into four PNG sources and card-ready images. Only the chosen welcome and farewell images load, using PSRAM on device (230,400 bytes total). The initial neutral welcome renders before SD mounting. Card loading runs outside the display task; shutdown uses the already-loaded farewell with no card reads. A failed mount retains the neutral fallback. Changes take effect on the next boot. Theme loading does not enable WiFi or alter playback/control layouts.

For a mounted card:

```sh
python3 tools/install-theme-pack.py theme-packs/Default --card /Volumes/CARD/music
```

The installer also supports `--ftp PLAYER_IP` while existing diagnostic FTP is active. It copies only theme manifests/images, not music, and publishes Person.toml last. Theme files are independent of the music catalog and normal sync does not manage or remove them. PNGs need not be copied to the card. Serial command `theme` reports the selected name, theme, scene headings and image presence.

Acceptance checks: sanitizer-backed production parser tests cover name independence, rotation, missing card, invalid paths, unsupported/malformed/oversized manifests and missing/wrong-sized images. The production LVGL harness renders both packs and all scene variants, preserving navigation, album-art precedence, lyrics and lifecycle races. Physical startup timing and the selected pack are checked separately; Listener's preference for the finished artwork remains her choice.

## Device evidence and risk retirement

The connected player was app-flashed with checksum verification. All eleven pack files installed through the existing diagnostic transfer mode; no music resync ran. On the subsequent USB chip reset, it reported `sakura`, `name=Listener`, both image buffers loaded, playback ready without an error, live USB and screen awake. The initial neutral UI was ready at 894 ms; the card theme at 1,352 ms; the library remained six albums and 91 songs. Internal free RAM after loading was 138,131 bytes, PSRAM 7,976,792 bytes. The compiled app is roughly 110 KiB smaller after removing the old character-image array.

| Risk | Status | Evidence that fails the tempting shortcut |
|---|---|---|
| Theme assets delay the initial welcome | Retired | Startup UI contains no card calls; actual neutral frame precedes SD mounting/theme loading. Missing-card UI harness stays navigable, unlike blocking startup on a pack. |
| Bad files or insufficient image memory crash startup | Retired | Production loader under sanitizers rejects malformed/oversized manifests and short/missing/path-traversing assets; injected image-allocation failure retains text and the generic icon. |
| Personal name remains coupled to images/code | Retired | Tests load the same Midnight pack for Listener and Alex with unchanged image files and different rendered names. Firmware contains no personal greeting or character artwork. |
| Variety distracts during music playback | Accepted with rationale | Selection is fixed per boot; no animation timer or rotating scenes during browsing/playback. Preference for the artwork remains Listener’s judgment. |
| Theme files disturb music sync/indexing | Retired | Theme files install outside managed catalog objects. Physical restart retains six albums and 91 tracks; existing sync cleanup only targets its managed object paths. |

Review outcome: aligned with hybrid packs, no layout/skin engine or new network service added. Appearance preference changes require editing Person.toml and rebooting; a touchscreen pack picker is not included. Physical screen appearance and Listener’s subjective preference require human verification; production UI previews cover all eight scenes.

A second app flash and boot selected Midnight’s second scene (`Ready, Listener?` / `See you soon, Listener!`), confirming persistent rotation across the two packs. The neutral UI was ready at 893 ms and the pack at 1,351 ms. Both images were loaded, playback remained ready and paused, WiFi was off and USB live. Permanent navigation/playback buttons are recolored alongside the dynamic content when the pack loads; layout and input behavior remain fixed.
