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
