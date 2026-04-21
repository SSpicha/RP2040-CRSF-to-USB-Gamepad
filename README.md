# RP2040 CRSF to USB Gamepad

A high-performance bridge converting **CRSF (ExpressLRS/Crossfire)** signals into a standard **USB HID Gamepad**.

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
