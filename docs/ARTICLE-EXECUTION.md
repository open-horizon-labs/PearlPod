# Article execution contract

Source: [Trying Out the FAKE_POD_NANO](https://text.tchncs.de/the-rose-garden/trying-out-the-fake-pod-nano), March 29, 2026. Read the complete firmware section, not only its summary.

Aim: Listener can find and reliably play her music without rearranging files to satisfy stock firmware restrictions. Preserve offline operation, fast first screen, headphone output, touch and button controls. Implement every firmware limitation identified by the article; hardware observations are not feature requests. WiFi/Bluetooth OTA stays deferred; documented local USB updates address inaccessible firmware delivery.

| Limitation | Success check / tempting shortcut rejected |
|---|---|
| Browse times out before long names can be read | No inactivity route change, paused browsing stays indefinitely, long titles reveal themselves; simply extending the timeout fails |
| 255 folders / 255 songs / one level | Dynamic indexing, more than 255 groups and songs, at least 20 nested directories; raising one constant fails |
| No real playlists | On-card M3U/M3U8 and XSPF with ordered relative references, UTF-8, missing/unsafe entries; treating folders as playlists fails |
| Rescan appends unsorted entries | Visible rescan action builds a fresh naturally/tag-sorted snapshot and swaps safely while playback is paused; appending to the old list fails |
| Tags only displayed / no artist sort / filename album ordering | Device reads ID3 and FLAC comments, groups albums using album artist, exposes artists and folders, sorts disc/track tags before filename; a preparation-only script fails |
| Certain valid FLAC requires re-encoding | Decoder matrix for different bit depths, rates, compression and native/Ogg containers; ID3-prefixed native FLAC compatibility, corrupt-file recovery; disabling CRC or claiming every unknown file fixed fails |
| Inaccessible updates | Local reproducible build, checked-in binaries and simple USB update tooling without OSHWhub access or phone number; promising future OTA fails |

Stop/pivot if indexing corrupts memory, rescan invalidates live audio/UI pointers, valid codec fixtures fail, or physical startup/output regress. Preserve a complete factory recovery image. Unknown original failing files require human reproduction; a codec matrix cannot prove those exact files work. Remaining finite RAM and path limits are hardware bounds, must be explicit rather than hidden truncation.

# Execution evidence

The core implementation is committed as c9e6efd and physically flashed. Device scan now recognizes two distinct tagged albums and two artists from the previously flattened two-song root. Collection playback progressed, rescans while playing returned a paused, ready player with the selected path preserved; repeated rescan requests and a simultaneous diagnostic list completed without a crash.

| Risk | Disposition | Evidence / shortcut rejected |
|---|---|---|
| Arbitrary folder/track/depth limits | Retired | Dynamic PSRAM tables and iterative traversal; host catalog exceeds 255 groups, 2,048 tracks and 12 levels. Raising constants or recursive stack growth fails the checks. |
| Filename-driven album order | Retired | Reverse filenames, disc/track fractions, same-title different artists and compilation album-artist tests. Filename-only grouping fails. |
| Playlists are just folders | Retired | Real ordered M3U8/XSPF, UTF-8 BOM, namespace prefixes, URI entities, alternative locations, intentional duplicate references, missing and escaping paths. Natural sorting of playlist entries fails. |
| Browse timeout / unreadable long titles | Retired | Actual LVGL pointer harness advances five paused minutes without routing away; row and playing labels use bounded scrolling. Extending an inactivity timer fails. |
| Rescan stale pointers / unsorted append | Retired | Fresh sorted snapshot plus allocator failure injection preserves the old one; audio detach acknowledges only after closing its decoder, preference/list locks protect swap. Physical repeated rescan/list/play checks completed. |
| Valid FLAC compatibility / corrupt acceptance | Retired for checked matrix | 40 valid native/Ogg FLAC cases at 16/24-bit, 44.1/48/96/192 kHz, compression 0/5/12 and ID3 prefixes match WAV PCM. Sixteen truncated/corrupted cases detect missing frames. CRC remains enabled; disabling validation fails these cases. |
| Reviewer’s unknown failing songs | Accepted with rationale | No sample or exact encoding was supplied. The matrix retires tested classes, but cannot identify every failure in another person’s collection. Obtain a reproducible file if one still fails. |
| Updates inaccessible | Retired | Private repo contains local build and binaries; USB updater verifies SHA256 and preserves NVS without OSHWhub or ESP-IDF. Checksums and esptool flash verification pass. |
| RAM/path/format physical bounds | Accepted with rationale | Finite 8 MB PSRAM, 511-byte paths, mono/stereo decoding up to 192 kHz and existing 16-bit/48 kHz output pipeline remain explicit. They are not the stock’s arbitrary 255/one-level restrictions. |

Aim status: firmware behavior and checks are delivered; Listener’s independent use, current touch feel and the original reviewer’s exact files still require human observation. The user subsequently authorized WiFi/setup/sync; its decision and risk checks are tracked separately rather than treating the former WiFi exclusion as binding.
