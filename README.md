# 🚀 RP2040 CRSF-to-USB Gamepad (v4.0)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio&style=flat-square)](https://platformio.org/)
[![RP2040](https://img.shields.io/badge/Hardware-Raspberry%20Pi%20RP2040-blue?logo=raspberrypi&style=flat-square)](https://www.raspberrypi.com/products/rp2040/)
[![CRSF](https://img.shields.io/badge/Protocol-TBS%20CRSF%20/%20ELRS-red?style=flat-square)](https://www.expresslrs.org/)

**The ultimate low-latency bridge for FPV simulators.**  
This version (v4.0) introduces a high-fidelity **Betaflight CLI Emulation**, allowing you to configure and flash your ExpressLRS receiver directly through the ELRS Configurator as if it were connected to a real Flight Controller.

---

## 🔥 What's New in v4.0

- **🖥️ Full Betaflight CLI Emulation:** Emulates a Betaflight FC to satisfy ELRS Configurator's environment checks (SerialRX, Inversion, Duplex, etc.).
- **⚡ Zero-Config Passthrough:** Simply connect your RP2040, open **ELRS Configurator**, and flash! The device automatically handles port speeds and passthrough logic.
- **🛡️ Smart Watchdog:** Automatic exit from Passthrough mode if no data is received for 5 seconds, returning you to Gamepad mode.
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
| **🟠 Pulsing Orange** | **Passthrough Mode Active** (Configuring/Flashing RX) |

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

## 🛠️ How to use Passthrough

1.  Connect your RP2040 to your PC via USB.
2.  Open **ELRS Configurator**.
3.  Select **"Betaflight Passthrough"** (or **"Serial"**) as the flashing method.
4.  Choose the Pico's COM port.
5.  Click **"Build & Flash"**.
6.  The device will automatically switch to Passthrough mode (LED turns Orange) and return to Gamepad mode when finished.

---

## 📜 Installation

1.  Use **VS Code** with **PlatformIO**.
2.  The project uses the **Earle Philhower** core and **Adafruit TinyUSB** for maximum performance.
3.  Build & Upload to your RP2040 board.

---

## 🔧 3D printed parts

[Thingiverse](https://www.thingiverse.com/thing:7326230)
[Printables](https://www.printables.com/model/1657670-rp2040-crsf-to-usb-gamepad)

---
**Developed by [SSpicha](https://github.com/SSpicha)**
