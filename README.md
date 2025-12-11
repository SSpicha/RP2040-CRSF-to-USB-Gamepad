# CRSF to USB Gamepad Converter

This project turns an RP2040-based board (e.g., YD-RP2040 or Raspberry Pi Pico) into a CRSF (Crossfire/ELRS) receiver that acts as a USB HID gamepad. It includes an OLED display for telemetry monitoring, a NeoPixel for status indication, and supports passthrough mode for direct serial communication.

## Features
- **Gamepad Mode**: Maps 16 CRSF channels to a USB gamepad (axes and buttons).
- **Passthrough Mode**: Forwards serial data between CRSF and USB for configuration tools.
- **OLED Display**: Shows link stats (RSSI, LQ, SNR, power), channel values, and advanced telemetry across 4 pages.
- **NeoPixel Status**: Color-coded link quality (green: good, orange: medium, red: poor; blue: passthrough; blinking red: no link).
- **Controls**: User button or channel 10 for page switching; channel 9 for mode toggle.
- **Overclocked**: Runs at 200 MHz for low latency.
- **Low Latency USB**: 1 ms HID polling.

## Hardware Requirements
- RP2040 board (e.g., YD-RP2040 or Pico).
- SSD1306 OLED (128x64, I2C on pins 4/5).
- NeoPixel (WS2812 on pin 23).
- CRSF receiver connected to UART (RX: pin 1, TX: pin 0).
- User button on pin 24 (pull-up).
- Built-in LED on pin 25 for status.

## Setup
1. Install [PlatformIO](https://platformio.org/).
2. Clone this repository.
3. Update `platformio.ini` if needed (e.g., replace board with your variant).
4. Build and upload: `pio run --target upload`.
5. Connect CRSF receiver and power on.

## Usage
- **Mode Switching**: Set channel 9 >1500 for passthrough; <1500 for gamepad.
- **Page Navigation**: Press user button or momentary channel 10 >1500.
- **Display Pages**:
  - Page 1: RSSI, LQ, TX Power, SNR.
  - Page 2: Channels 1-8.
  - Page 3: Channels 9-16.
  - Page 4: Antenna, RF Mode, Downlink RSSI/LQ.
- The device appears as a USB gamepad in OS (Windows/Linux/Mac).

## Channel Mapping
- Axes: Channels 1-6 → X/Y/Z/RZ/RX/RY.
- Buttons: Channels 7-16 → Buttons 1-10 (threshold >1500).
- Throttle (Z) is unipolar mapping.

## Libraries
- Adafruit TinyUSB
- Adafruit GFX & SSD1306
- Adafruit NeoPixel

## License
MIT License. See [LICENSE](LICENSE) for details.

## Acknowledgments
Based on ELRS/Crossfire protocols. Uses Earlephilhower's RP2040 Arduino core.
