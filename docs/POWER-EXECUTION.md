# Power, memory and diagnostic behavior

The aim is that Listener can leave her player alone without draining the battery, while playback and recovery remain independent of WiFi. The roon-knob family's explicit sleep blockers, display sleep/wake discipline, bounded connection retries and truthful power diagnostics informed this implementation. Source references: `hiphi-repos/roon-knob/frame_app/main/frame_power_policy.c`, `idf_app/main/display_sleep.c`, `common/wifi_manager.c`, `common/platform/platform_power.h`, and `common/power_debug_web.c`. These behaviors were independently implemented; no PolyForm-licensed source was copied. The existing MIT T-Dongle network selection policy and attribution remain.

## Defaults

The screen sends AMOLED DISPOFF/SLPIN after 45 seconds without touch or button activity, including during music. Album art buffers are released and pending artwork requests cancelled. Touch wakes an automatically sleeping screen; the waking contact is consumed until release so it cannot select a track. Wake restores the same view and scroll position. Holding volume-down manually locks the screen; holding it again unlocks it.

After five minutes of both inactivity and paused/stopped playback, the player enters deep sleep. Playback, library startup/rescan, enabled WiFi, a connected USB host, held buttons, or an unsupported RTC wake pin block automatic deep sleep. USB host detection is not a battery or charging measurement. A wall charger may therefore permit automatic sleep. Holding volume-up manually shuts down WiFi and audio, saves preferences and sleeps; holding GPIO0/volume-up for 1.8 seconds wakes it. A short wake press returns to sleep before display/library initialization. Non-RTC button configurations retain the earlier light-sleep fallback.

Brightness defaults to 192/255. Timeouts and brightness are build-time menuconfig settings under PearlPod power and diagnostics. Zero disables either automatic screen or idle sleep timeout.

## WiFi lifetime and recovery

WiFi remains off at boot. Setup sessions last five minutes; explicitly connected diagnostic sessions last fifteen minutes. Starting the same mode again renews its lease. Expiry and the Off action stop HTTP, stop/clear scans, stop and deinitialize the radio. STA uses modem power saving. Connection attempts have a twenty-second deadline, exponential retry delay and a five-attempt limit; authentication failure stops retries. Setup can remain available for correction until its lease ends. Failed scan starts/retrievals count against the attempt budget. Queue, scan and HTTP response sizes are bounded.

After a panic or task/interrupt watchdog reset, the optional WiFi worker and card WiFi parser are delayed until an explicit WiFi action. This allows offline playback to recover from an optional networking failure. It does not guarantee recovery from every possible firmware fault. The device-verified recovery image and twenty-attempt USB polling script remain available.

## Memory budget and status

Two 460×24 RGB565 display DMA buffers consume 44,160 bytes of internal RAM. Ordinary allocations larger than 1 KiB prefer PSRAM; ESP-IDF's WiFi/lwIP PSRAM allocation option is enabled. WiFi uses six static RX/TX buffers, eight dynamic RX/cache TX buffers and a six-frame RX BA window. The internal DMA/task-stack reserve remains 32 KiB. Audio/task stacks stay internal. Radio startup checks both free internal memory and its largest contiguous block; failure leaves music available. Scan records and JSON diagnostics use bounded heap allocations rather than large worker-stack snapshots.

`GET http://<connected-IP>/status` provides read-only JSON: uptime, reset/wake reason, audio state, catalog counts, screen/idle/USB state, internal free/minimum/largest block, free PSRAM, WiFi worker stack high-water mark, connection retries/reason and remaining session time. It is also available on the setup AP. It returns no SSIDs, passwords, token, track titles or mutation controls. The existing provisioning routes remain restricted to the AP destination; mutations also require the session token. Battery percentage is `null` because no calibrated battery gauge is established. Serial `power`, `memory` and `wifi status` offer offline diagnostics.

## Validation

ASan/UBSan host checks cover every automatic sleep blocker, pause grace, disabled timeouts, uint32 timer rollover and retry backoff saturation. The actual LVGL host harness checks artwork release, same-page/scroll wake restoration and existing touch/swipe/playback controls. ESP-IDF 5.5.5 builds locally with worker stack frame 656 bytes and scan completion frame 1,184 bytes against an 8,192-byte worker stack; compiler frame limit remains 2,048 bytes.

The first on-device power candidate booted with UI ready at 836 ms and library at 1,135 ms; after 45 seconds serial reported screen asleep and playback still ready. Its WiFi memory preflight rejected startup, identifying an inadequate internal-RAM budget before any radio crash. The budget was then corrected as described above. Final hardware results are recorded below after verification. Physical touch wake, deep-sleep wake and actual battery runtime require device observation; no runtime improvement percentage is claimed.

The revised RAM build was flashed app-only with esptool hash verification. USB logs showed UI at 806–809 ms and library at 1,138–1,141 ms. Awake offline internal free RAM was 87,131 bytes. Setup and two successive scans completed; Off recovered roughly 47 KiB (31,071 → 78,231 bytes). A further three setup/scan/off cycles during playback completed with ready=1, paused=0, no audio error and playback progressing through 56 seconds after the screen slept. Post-Off RAM was approximately 75–76 KiB and PSRAM remained stable across those cycles. This is a short stress check, not a proof of zero leaks or battery runtime. Station credentials/authentication, LAN `/status`, physical wake controls and automatic session expiry remain separate checks.
