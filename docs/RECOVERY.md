# Recovering PearlPod from a firmware boot loop

Run `tools/recover.sh` from this checkout. Missing firmware is fetched from the private GitHub release, requiring GitHub CLI authentication. It makes up to twenty automatic attempts and stops after the first successful flash. It polls every 50 ms for the player’s USB identity, allowing it to catch short enumeration windows during a reset loop. Each attempt waits up to three seconds for detection and bounds the esptool process to sixty seconds. No manual supervision is required between attempts.

The default recovery image is the application from device-tested commit c9e6efd, distributed as the private release asset `known-working-c9e6efd.bin`, downloaded to ignored `dist/`. Its SHA-256 is verified before starting. Only offset 0x10000 is written, preserving the bootloader, partition table and NVS preferences. The script selects the ESP USB serial identity 02:00:00:00:00:01, so another ESP32 such as T-Dongle or roon-knob is not selected accidentally. Use `--port /dev/cu.usbmodemDEVICE` to further constrain the port, or `--check` to verify the image and inspect detection without writing flash. Esptool 4.12.0 is installed into the ignored local `.flash-venv` if necessary.

Logs are saved under the ignored `backups/` directory. Success means esptool finished writing and verified flash data. Confirm that the player boots and plays music afterward; this is a separate check.

## Observed recovery

October 3, 2026: a WiFi worker stack overflow caused a boot loop and intermittently disappearing USB port. A script polling every 50 ms detected the brief USB window and used `--before usb_reset` to flash c9e6efd at 0x10000. Esptool verified the data hash, and the user confirmed the player flashed and worked. This was the successful first attempt of an earlier bounded retry script. The reusable twenty-attempt script preserves the successful pattern; its identity filtering, stop-on-success behavior, app-only command and absent-device bounds are covered by host tests. Its `--check` detected the restored player successfully. Twenty actual flash attempts were neither needed nor performed.

## Fact check

| Claim | Status | Evidence | Correction or limit |
|---|---|---|---|
| GPIO0 held low during reset enters the ESP32-S3 ROM downloader | Verified | [Espressif boot-mode documentation](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) | GPIO46 must also be low or floating. A button press without an actual reset is insufficient. |
| Unplugging USB necessarily resets this battery-powered player | Unsupported | No reset/power-path measurement establishes that | Do not use this as the sole recovery assumption. |
| A polling script can catch this device and recover this firmware loop | Verified | Flash hash verification and user confirmation from October 3, 2026 | One observed recovery; no guarantee for every hardware or firmware failure. |
| The twenty-attempt tool stops on success and avoids other ESP devices | Verified | Host tests and USB serial identity check | Detection depends on the native USB serial identity remaining available. |

High-risk unresolved claim: there is no verified external hardware RESET control or board-specific battery-isolation procedure. No hardware-short instructions are inferred from generic development-board documentation. Source gap: exact EN wiring on this board. Expert review would be needed before using board test pads or modifying hardware. Correction applied: the successful USB-window recovery is the first practical option for another application boot loop.
