# RP2040 CRSF to USB Gamepad

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio&style=flat-square)](https://platformio.org/)
[![RP2040](https://img.shields.io/badge/Hardware-Raspberry%20Pi%20RP2040-blue?logo=raspberrypi&style=flat-square)](https://www.raspberrypi.com/products/rp2040/)
[![CRSF](https://img.shields.io/badge/Protocol-TBS%20CRSF%20/%20ELRS-red?style=flat-square)](https://www.expresslrs.org/)

High-performance USB adapter for connecting RC receivers (ExpressLRS, Crossfire) to a PC for use in simulators. Based on the Raspberry Pi Pico (RP2040) microcontroller.

## 🚀 Key Features

- **PIO + DMA Transport:** Hardware-based CRSF protocol reading with zero CPU involvement. No packet loss even at 420,000 baud.
- **Dual-Core Processing:** Protocol parsing on Core 1, USB HID, and CLI on Core 0. Lowest possible latency.
- **RC Smoothing (Optional):** Integrated PT1 filter for perfectly smooth stick movement in simulators (can be disabled for raw performance).
- **EEPROM Persistence:** Settings are saved to Flash memory and persist across reboots.
- **Betaflight CLI Passthrough:** Full support for receiver flashing and configuration via ELRS/Betaflight Configurator.
- **Link Quality Monitoring:** Real-time packet rate (Hz) monitoring via CLI.
- **RGB Status LED:** Visual status indication for link, failsafe, and mode.

## 🕹️ Operation Modes & Indication

| LED Color | State | Description |
|-----------|------|-------------|
| 🔴 Flashing | **Failsafe** | No connection to the transmitter (RC Link Down). |
| 🟢 Solid | **Raw Mode** | Link active, Smoothing OFF (minimum latency). |
| 🔵 Solid (Cyan) | **Smooth Mode** | Link active, Smoothing ON (maximum smoothness). |
| 🟠 Flashing | **Passthrough** | Receiver flashing mode via USB. |

## 🛠️ CLI Configuration

Connect to the device via any Serial Terminal (Baud: 115200) and use the following commands:

- `status` — View current settings, link status, and packet rate (Hz).
- `set smoothing on` / `set smoothing off` — Toggle stick smoothing.
- `set cutoff <Hz>` — Adjust filter aggressiveness (e.g., `set cutoff 30`). Lower values = smoother.
- `dfu` or `bootloader` — Reboot the device into USB Mass Storage mode for firmware updates.
- `reboot` — Reboot the device.

## 📡 Flashing the Receiver

The device fully supports **Serial Passthrough** mode.
1. Open ExpressLRS Configurator.
2. Select **Betaflight Passthrough** as the flashing method.
3. Click **Build & Flash**. The device will automatically switch to Orange flashing mode and relay the firmware.

## 🔌 Connection Diagram

| RP2040 Pin | Function | Receiver (RX) |
|------------|---------|--------------|
| **GPIO 1** | RX      | TX           |
| **GPIO 0** | TX      | RX (required for Passthrough) |
| **5V / VBUS**| Power | 5V           |
| **GND**    | Ground  | GND          |

## 🏗️ Build Instructions

Developed using **PlatformIO**.
- Platform: `Raspberry Pi Pico`
- Framework: `Arduino`
- Libraries: `Adafruit TinyUSB`, `Adafruit NeoPixel`
