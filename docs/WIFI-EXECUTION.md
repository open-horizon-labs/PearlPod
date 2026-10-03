# WiFi setup decision and execution

The aim is to let a grown-up prepare PearlPod for future connectivity while Listener can immediately listen to her card. WiFi setup is authorized; the user explicitly deferred sync, WebDAV and playlist management on October 3, 2026. No remote endpoint or sync protocol is required in this phase.

## Solution space

| Candidate | Main benefit | Decision-changing cost | Disposition |
|---|---|---|---|
| Touchscreen keyboard | Entirely on-device | Password entry on a 460 px player is cumbersome | Deferred |
| USB-only credentials | Small implementation | Requires a computer and secret-bearing terminal commands | Rejected for ordinary setup |
| On-demand secured AP and browser form | A familiar keyboard, supports hidden networks, no startup dependency | Must protect setup while also connected as a station | Selected |
| Always-on captive portal and automatic reconnection at boot | Immediate network availability | Startup, power and interaction cost before any network feature needs it | Rejected |

The first two assume provisioning belongs on the device or developer computer. The selected approach treats setup as an occasional parent action using a phone or computer. Success is asynchronous discovery, safe persistence, connection status, a working off control, and no WiFi initialization in the startup path. Sync architecture remains undecided.

## Borrowed patterns and provenance

roon-knob at a4b5a389e5c2d2f377426f03ed0f17488d21ed46: `common/wifi_manager.c` asynchronously handles scan completion, allocates scan records on the heap, deduplicates visible SSIDs, caches a bounded result list and clears the driver's scan allocation on all paths. `common/wifi_portal_form.c` separates discovered networks from manual SSID entry and escapes SSIDs. PearlPod independently implements this lifecycle; the browser uses JSON and DOM text rather than interpolating SSIDs into HTML. No roon-knob source was copied, and its PolyForm Noncommercial license was inspected.

tdongle-tailnet-firmware at ce0e1725fec8ebcf261b2969b2674aa710866b58: `alternative/tailnet/main/gateway_main.c` pauses reconnect attempts during scans and retries from a worker with backoff. Its `wifi_policy.h` strongest-network selection with stable ties and 12 dB hysteresis is adapted in `main/wifi_policy.h`, with its MIT notice preserved in `licenses/tdongle-MIT.txt`. No credential file was read or copied.

## Dissent and adjustments

The best case for an AP is familiar password entry without boot cost. The functional failure would be reconnect attempts preventing scans; the adoption failure would be exposing setup with an obscure USB command; the opportunity cost would be implementing sync before the music-management workflow is decided. These concerns led to a touch-accessible WiFi page, a command worker and reconnect pause, and the explicit removal of sync from this phase.

A setup server in AP+STA mode also listens on the station interface unless restricted. Requests are accepted only when addressed to the AP interface, mutations require a runtime session token, and the AP requires a random WPA2 password shown on the device. Neither stored passwords nor AP passwords appear in diagnostic output. The server stops with WiFi off. Credential storage is ordinary NVS, not encrypted against physical extraction; physical resistance is outside this personal-device setup scope.

## Execution checklist and risk retirement

| Risk | Tempting shortcut the check must fail | Required check |
|---|---|---|
| Startup waits for WiFi | Initialize radio in app_main | Inspect initialization call graph and measure offline device readiness |
| Reconnect starves scan | Connect immediately in disconnect handler | Scan during failed connection, verify completion and bounded retry |
| Empty/failed scans leak driver records | Clear only after successful record retrieval | Inspect every allocation/retrieval path; exercise repeated scans |
| Network names inject browser markup | Concatenate SSIDs into HTML | DOM-only option creation with hostile names in host tests |
| Setup leaks onto station interface | Bind to all interfaces without checks | Socket destination IP check on every route; physical station-side check requires a provisioned network |
| Credentials enter source/logs | Borrow existing profile secrets | No credential file access; diagnostics omit secrets; private NVS only |
| Radio prevents standby | Leave AP running when holding power | Shutdown requests off, waits for worker acknowledgement before sleep |
| Wrong password spins forever | Reconnect from event callback | 20-second attempt deadline, 60-second worker retry; setup remains usable |
| New features consume offline audio resources | Run radio at boot | Local build, existing host playback/UI checks, device playback after off |

Four profiles, twenty strongest scan records, 2.4 GHz, open or 8–63 byte WPA passphrases. Saving a fifth distinct network is explicitly rejected rather than evicting another network. Hidden networks connect immediately when saved and the retry path attempts a saved network when discovery finds none. There is no captive DNS; the player explicitly shows `http://192.168.4.1`.

## Hardware failure and correction

The first WiFi build boot-looped: device USB logs report stack overflow in task `wifi`. Xtensa disassembly showed an inlined worker frame of 0x1e90 (7,824 bytes) before nested calls against an 8,192-byte task stack. Host tests did not detect this device-specific failure. The correction uses small locked flag reads rather than repeatedly returning the complete scan cache, and keeps scan and startup routines outside the worker frame. The corrected worker frame is 0x290 (656 bytes), scan completion is 0x490 (1,168 bytes), and the LVGL update frame is 0x6a0 (1,696 bytes). Network compilation now warns/errors on frames above 2,048 bytes and emits stack-usage data. Device reflash and runtime validation are required before calling this fix verified.

## Current evidence gate

Retired by evidence: offline radio initialization is absent from the startup path (only the setup/connect worker calls it); the unsafe inlined stack frame is caught by the compiler limit and the reduced stack-usage report; strongest selection/ties/12 dB hysteresis pass ASan/UBSan tests; hostile SSIDs pass the portal DOM test; every scan retrieval path clears driver records by inspection; every HTTP route checks AP destination before content or mutations; stored passwords are absent from response/console handlers. Shutdown uses an explicit worker acknowledgement before standby, and a timeout prevents sleep while shutdown is pending.

Triggered: the first hardware image overflowed the worker stack. The design was adjusted and rebuilt; hardware risk is not retired until the corrected image runs. The local build, UI harness and package checksum verification pass. Physical repeated scans, connection/failed-password retry and playback after radio off remain pending. Saving credentials through the AP browser and checking rejection from the station interface require a provisioned network and human setup. Sync and playlist management are accepted outside this phase by the user’s explicit scope change.
