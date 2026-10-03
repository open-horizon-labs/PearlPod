# PearlPod

Private personal firmware repository at Muness/PearlPod for Listener’s CS43131 FakePod Nano. The product aim is that Listener independently chooses and enjoys her music.

Work directly on main for now. Commit and push regularly at coherent checkpoints. No pull requests are needed yet. Build and test locally only; do not add hosted CI builds or publish releases unless requested. Keep repository visibility private.

Use ESP-IDF v5.5.5 and the checked-in tools/build.sh and tools/check.sh. Preserve fast startup, offline microSD playback, touchscreen navigation, physical volume and hold controls. WiFi scan and setup are authorized. The user deferred WebDAV, synchronization and playlist management; do not implement them in this phase. Borrow proven scanning/provisioning patterns from roon-knob and tdongle-tailnet-firmware, without coupling startup or offline playback to connectivity. Credentials belong in runtime configuration, never source or logs. Hardware and validation evidence are in docs/HARDWARE.md and docs/VERIFICATION.md.

Never commit the factory flash backup, credentials, virtual environments or temporary build directories. Packaged firmware in dist is intentional; refresh it with local builds when firmware changes. Record material unverified behavior honestly. Full merged-image flashing resets NVS preferences; ordinary app flashing can preserve them.

Keep Markdown paragraphs and list items on one source line, without fixed-width wrapping.
