# Execution contract

Aim: Listener independently chooses and enjoys her music on a player that feels hers.

Selected approach: preserve upstream display/touch board support and implement a bounded local player because the published source attachment is a hardware demo, not the shipped player.

Criteria: offline playback reliability, simple controls, fast startup, then personal appeal and maintenance cost. Target ESP-IDF 5.5.5 stable. No networking dependency.

Required checks: build real flash artifacts; preserve factory image and SHA256; identify actual DAC and button wiring; exercise nested directories, multiple albums, invalid media, missing/malformed art, MP3/FLAC/WAV decoding, volume clamps, short/long button classification, power-on hold, sustained audio concurrent with touch/artwork, restart and safe paused resume. A boot-image-only patch, idle UI demo, guessed board configuration, or green compile cannot satisfy these checks.

Stop/pivot: missing recovery, unknown destructive pin behavior, failed DAC detection, unreadable SD, playback starvation, or incorrect controls. Human verification: listening through the actual 3.5 mm headphones, physical controls and touch, Listener's usability/theme preference, and comparative boot speed.
