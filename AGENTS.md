# PearlPod

Private personal firmware repository at Muness/PearlPod for Listener’s CS43131 FakePod Nano. The product aim is that Listener independently chooses and enjoys her music.

Work directly on main for now. Commit and push regularly at coherent checkpoints. No pull requests are needed yet. Build and test locally only; do not add hosted CI builds or publish releases unless requested. Keep repository visibility private.

Use ESP-IDF v5.5.5 and the checked-in tools/build.sh and tools/check.sh. Preserve fast startup, offline microSD playback, touchscreen navigation, physical volume and hold controls. WiFi scan and setup are authorized. The user has authorized a local Plex export and sync spike, followed by a small container deployment. Follow docs/PLAYLIST-EXPORT-SYNC-DESIGN.md and docs/EXPORT-MEDIA-CONTRACT.md; keep Plex credentials on the host and preserve offline startup. Nightly charging automation requires verified hardware sensing. Borrow proven scanning/provisioning patterns from roon-knob and tdongle-tailnet-firmware, without coupling startup or offline playback to connectivity. Credentials belong in runtime configuration, never source or logs. Hardware and validation evidence are in docs/HARDWARE.md and docs/VERIFICATION.md.

Never commit the factory flash backup, credentials, virtual environments or temporary build directories. Firmware binaries belong in private GitHub release assets, never Git history. Local builds write ignored dist/. Refresh release assets at explicit release checkpoints. Record material unverified behavior honestly. Full merged-image flashing resets NVS preferences; ordinary app flashing can preserve them.

Keep Markdown paragraphs and list items on one source line, without fixed-width wrapping.

Recovery: `tools/recover.sh` polls for this player’s USB identity and makes up to twenty bounded flash attempts, stopping on success. It restores bundled device-verified c9e6efd, app-only, preserving NVS. A tight USB polling loop successfully recovered the WiFi stack-overflow boot loop; prefer this documented approach over assuming USB unplug resets a battery-powered device. Read docs/RECOVERY.md before giving hardware reset instructions.
