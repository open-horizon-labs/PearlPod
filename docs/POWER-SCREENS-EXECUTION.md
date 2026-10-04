# Boot and shutdown screens

## Execute

**Task:** Add welcoming boot and farewell shutdown screens so Listener can recognize when her player starts and turns off. Extend the existing anime artwork and palette, with no new assets or card/network dependency.

**Scope and decision:** Reuse the bundled 240 × 240 welcome illustration and Montserrat type. Boot presents one real frame, then hands off immediately when the card scan finishes; no minimum splash duration. Manual shutdown wakes a locked display and presents the farewell before cleanup. An already-dark automatic idle shutdown stays dark rather than lighting an unused player. A visible farewell lasts at least 600 ms, including cleanup time. Button mapping, wake hold duration, USB sleep blockers, audio format, WiFi and sync protocols remain as before. Changes to those mechanisms or an added startup gate would invalidate this bounded approach.

## Declared success criteria and delivered characteristics

- Personalized boot and farewell screens: met in production LVGL host renders. Boot says “Hi, Listener!” and “Finding your music…”; shutdown says “See you soon, Listener!” and “Closing music…”.
- Fast, offline startup: met in source and host transition checks; artwork is compiled into the app and browsing replaces welcome immediately on library readiness. Startup requests a render, not a dwell timer or extra decode/read.
- Visible shutdown feedback and recoverable failure: met under deterministic host hardware stubs. Farewell is rendered before trace/sync/network/audio cleanup. Card, sync, network and wake-configuration failure returns to the current library view with recovery copy.
- Stable navigation and resource lifetime: met in host UI checks. Hidden controls and gestures cannot navigate or start music; late artwork is freed; failed shutdown restores scroll position; a scan completion cannot replace farewell.
- Installed device behavior: pending. No PearlPod USB port was detected during this checkpoint, so no firmware was flashed.

## Changes

`main/ui.c` shares a centered lifecycle composition between boot and farewell, hides controls, releases cached artwork/lyrics at shutdown, and suppresses navigation/timer rendering while closing. Library readiness can still update its data during closing, allowing recovery to show the current library. `main/main.c` integrates the screen into actual shutdown, allows a bounded 100 ms wait for the final display DMA flush, and includes a minimum farewell duration only on visible shutdown. Scans are excluded before cleanup and checked again after sync stops. The existing display lock and power policy continue to own panel sleep and wake.

## Verification and risk retirement

| Risk / invalidation | Status | Tempting patch rejected by this check | Evidence |
| --- | --- | --- | --- |
| Boot delays browsing or depends on card artwork / WiFi | Retired by source and host evidence | Hold a splash timer or load welcome from the SD card | Bundled `pearl_welcome`; no startup timer; `pearl_ui_ready` immediately reveals browse, including an empty/missing-card library |
| Farewell disappears before cleanup or never reaches a rendered frame | Retired at host/source scope | Draw after cleanup, or power the panel off immediately | Production standby routines compiled with hardware stubs assert begin/refresh precede cleanup, off precedes deep sleep, visible duration ≥600 ms, and final DMA wait is bounded |
| Stuck farewell after shutdown failure | Retired by adversarial checks | Add farewell only to the successful shutdown branch | `tests/test_shutdown.py` injects card, sync, network and wake-configuration failures; LVGL test verifies cancellation restores track scroll and recovery copy |
| Old controls, gestures or artwork replace farewell | Retired by adversarial checks | Hide a panel but leave timer/gesture work active | Host taps old control locations, swipes, and injects a stale artwork result while closing |
| A sync-triggered scan replaces farewell or sleeps while scanning | Retired at deterministic host/source scope | Check scan ownership only once before cleanup | Host UI finishes scanning during closing; standby test initiates a scan from sync cleanup and verifies sleep is cancelled before audio shutdown |
| Manual screen lock prevents farewell, or idle shutdown lights a dark room | Retired by host checks | Wake the display for every automatic shutdown, or never wake it for manual power-off | Manual dark-screen shutdown asserts wake before farewell; automatic dark-screen shutdown asserts no wake/render/dwell |
| Firmware no longer builds or power blockers regress | Retired by local checks | Add UI code without compiling the real hardware path | ESP-IDF v5.5.5 build, `tools/check.sh`, and production LVGL harness pass |
| Physical panel timing, held-button power cycle and startup duration | Accepted with rationale | Claim host rendering proves hardware operation | No player serial USB device available. A real panel/boot timing observation still requires installation; no new hardware timing claim is made |

The host standby test compiles the actual final routines from `main/main.c` against deterministic hardware stubs. It exercises ordering and early-return branches but does not simulate the ESP-IDF driver, FreeRTOS scheduling or electrical power loss. The LVGL harness executes production UI code, including existing browsing, playback, lyrics and sync-state checks.

## Review

**Aim:** Make start and turn-off recognizable and friendly without slowing offline startup or altering power controls. **Decision:** Continue; no scope or authority drift. The built screens inherit the existing artwork and colors, keep copy centered and legible, and fit the 460 × 460 display without clipping. Recovery restores the original browsing layout. Manual power-off gains feedback; automatic idle shutdown preserves an already-dark display.

## Needs human / device verification

Installation is pending. After an app-only flash, observe the real welcome-to-library transition, manual shutdown while the screen is both awake and locked, and the existing long-hold wake cycle. Confirm farewell is readable and no unwanted delay appears on startup. Existing USB and power behavior has host coverage; physical device behavior remains unverified at this checkpoint.

## Captured production UI

![Boot](ui/power/boot.png)

![Shutdown](ui/power/shutdown.png)
