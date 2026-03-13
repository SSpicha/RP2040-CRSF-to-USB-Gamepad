# 🚀 RP2040 CRSF-to-USB Gamepad PRO (v4.0-test)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio&style=flat-square)](https://platformio.org/)
[![RP2040](https://img.shields.io/badge/Hardware-Raspberry%20Pi%20RP2040-blue?logo=raspberrypi&style=flat-square)](https://www.raspberrypi.com/products/rp2040/)
[![CRSF](https://img.shields.io/badge/Protocol-TBS%20CRSF%20/%20ELRS-red?style=flat-square)](https://www.expresslrs.org/)

**The ultimate low-latency bridge for FPV simulators.**  
This version (v4.0) introduces **Fake CLI Mode**, allowing you to configure your ExpressLRS receiver directly through the ELRS Configurator without touching your radio.

---

## 🔥 What's New in v4.0 (Test Branch)

- **🖥️ Integrated Fake CLI:** The device now emulates a Betaflight Flight Controller. Just open **ELRS Configurator**, click "Connect", and it will handle the rest!
- **⚡ No More Radio Toggles:** Manual channel switching for Passthrough has been removed. All 16 channels are now free for your sim controls.
- **🛡️ Smart Watchdog:** Automatic exit from Passthrough mode if no data is received for 5 seconds.
- **🎯 Enhanced Dual-Core Engine:** Core 1 is dedicated to bit-perfect CRSF parsing, while Core 0 handles USB HID and CLI logic.

---

## ✨ Core Features

- **⚡ Zero-Jitter Dual-Core Architecture:** 
  - **Core 1:** High-speed CRSF parsing (bit-by-bit).
  - **Core 0:** USB HID reports (1000Hz) and CLI handler.
- **🎯 16-bit Precision:** High-resolution axis mapping (up to 65535 steps) for buttery smooth control.
- **🚀 1000Hz USB Polling:** Native 1ms polling interval for the lowest possible input lag.
- **🌈 RGB Status LED:** WS2812 (NeoPixel) visual feedback for link and mode status.

---

## 🚦 LED Status Indicators

| Color | Meaning |
| :--- | :--- |
| **🟢 Solid Green** | Link Established (RC Signal OK) |
| **🔴 Blinking Red** | No Signal / Failsafe |
| **🟠 Pulsing Orange** | **CLI / Passthrough Mode Active** (Configuring RX) |

---

## 🕹️ Channel Mapping (All 16 CH Free!)

| Channel | HID Axis/Button | Range |
| :--- | :--- | :--- |
| **CH 1** | **X Axis** (Roll) | 16-bit (-32767 to 32767) |
| **CH 2** | **Y Axis** (Pitch) | 16-bit (-32767 to 32767) |
| **CH 3** | **RY Axis** (Throttle) | 16-bit (Full Range) |
| **CH 4** | **RX Axis** (Yaw) | 16-bit (-32767 to 32767) |
| **CH 5** | **Z Axis** | 16-bit |
| **CH 6** | **RZ Axis** | 16-bit |
| **CH 7-16** | **Buttons 1-10** | Digital (ON/OFF) |

---

## 🛠️ How to use CLI Mode

1.  Connect your RP2040 to your PC.
2.  Open **ELRS Configurator**.
3.  Select **"Serial"** as the flashing method.
4.  Choose the Pico's COM port.
5.  Click **"Build & Flash"** (or use the "WiFi" tab if your RX supports it).
6.  The device will automatically switch to Passthrough mode (LED turns Orange) and return to Gamepad mode when finished.

---

## 📜 Installation

1.  Use **VS Code** with **PlatformIO**.
2.  The project uses the **Earle Philhower** core and **Adafruit TinyUSB** for maximum performance.
3.  Build & Upload to your RP2040 board.

---
**Developed by [SSpicha](https://github.com/SSpicha)**
