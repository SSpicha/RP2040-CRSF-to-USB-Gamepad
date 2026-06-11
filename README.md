# RP2040 CRSF to USB Gamepad

A high-performance bridge converting **CRSF (ExpressLRS/Crossfire)** signals into a standard **USB HID Gamepad**.

## Beta status
This branch, `feature/companion-app-v1`, is a **beta** release. The companion app and firmware changes are usable, but some edges are still rough. Expect follow-up fixes for stability and docs.

## Known limitations
- After roughly **1 minute of operation**, the web app may **freeze** and the RP2040 may also become unreachable over Serial. If that happens, close the tab, replug the device, and reopen `companion-app.html`.
- Companion-web forces `115200` baud. Older scripts/configs expecting `420000` will need updating.
- Mapping updates are sent one command at a time, so full remap can feel slow on lossy links.
- There is no retry/resend logic yet for mid-operation disconnects.
- Core 0/Core 1 synchronization uses a blocking mutex; extreme CRSF jitter can affect USB HID timing.
- Wiring diagram, Betaflight example config, and troubleshooting are not yet included.

## How to test (beta checklist)
- [ ] Flash `release/firmware.uf2`
- [ ] Open `release/companion-app.html` and verify the device appears in the browser Serial picker
- [ ] Complete an axis/button remap and reboot the board; confirm mapping persists
- [ ] Subscribe to telemetry (`app sub telemetry 100`) and watch RSSI/LQ/SNR for ~2 minutes
- [ ] Toggle demo mode in the web app to validate UI without hardware
- [ ] Verify no freezes after 1+ minutes; if any occur, note Serial log lines before disconnect
- [ ] Test passthrough mode if you depend on ELRS configurator workflows

## Key Features
*   **Low Latency**: Optimized signal processing on RP2040.
*   **Web Companion**: Full-featured web interface for mapping axes and buttons without reflashing the firmware.
*   **Dynamic Calibration**: Flexible Min/Max limit settings for each axis (sticks, triggers) directly from the browser for perfect precision.
*   **High Stability**: Optimized telemetry protocol (compressed JSON) ensures reliable operation without disconnects.
*   **Real-time Visualization**: Schematic gamepad in the browser to monitor all channels, button states, and trigger (LT/RT) travel.

## Getting Started
1. **Flashing**: Flash `firmware.uf2` to your RP2040 device using the bootloader mode.
2. **Connecting**: Open `companion-app.html` in a Web Serial API compatible browser (Chrome, Edge).
3. **Configuration**:
    - Click **Connect** and select your device.
    - Use the **Axes Mapping** table to assign channels.
    - **Calibration (LT/RT)**: Move your stick/trigger to the desired minimum/maximum position and click the **M (Min)** or **X (Max)** buttons to capture values. Click **Apply mapping** to save settings to device memory.

## Technical Details
*   **Protocol**: CRSF.
*   **HID**: Standard Gamepad (6 axes, 32 buttons).
*   **Baud Rate**: 230400 for stable telemetry performance.
*   **EEPROM**: Automatic configuration persistence.

---
*Created for using RC transmitters in simulators and games.*
