# 🚀 RP2040 CRSF-to-USB Gamepad (v3.1)

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange?logo=platformio&style=flat-square)](https://platformio.org/)
[![RP2040](https://img.shields.io/badge/Hardware-Raspberry%20Pi%20RP2040-blue?logo=raspberrypi&style=flat-square)](https://www.raspberrypi.com/products/rp2040/)
[![CRSF](https://img.shields.io/badge/Protocol-TBS%20CRSF%20/%20ELRS-red?style=flat-square)](https://www.expresslrs.org/)

A high-performance, low-latency USB HID Gamepad bridge for FPV simulators. This project converts **CRSF** (Crossfire/ExpressLRS) signals into a **16-bit high-resolution** USB Gamepad using the Raspberry Pi Pico (RP2040).

---

## ✨ Key Features (v3.1)

- **⚡ Dual-Core Architecture:** 
  - **Core 1:** Dedicated to high-speed CRSF parsing (no dropped packets).
  - **Core 0:** Handles USB HID reports and LED status.
- **🎯 16-bit Precision:** High-resolution axis mapping (up to 65535 steps) for buttery smooth control in simulators like Liftoff, Velocidrone, or Tryp FPV.
- **🚀 1000Hz USB Polling:** Native 1ms polling interval for the lowest possible input lag.
- **🔗 16 Channels Supported:** 
  - 6 High-resolution Analog Axes (X, Y, Z, Rz, Rx, Ry).
  - 10 Digital Buttons (mapped from CH7 to CH16).
- **🛠️ Smart Passthrough Mode:** Built-in USB-to-UART bridge to configure your ELRS receiver via WiFi or Serial (activated by holding CH9 for 2 seconds).
- **🌈 RGB Status LED:** Visual feedback using WS2812 (NeoPixel).

---

## 📸 Connection Diagram

```text
      ┌─────────────────────────────────┐
      │         RP2040 (Pico)           │
      │                                 │
      │  [USB] ◄─── To Computer         │
      │                                 │
      │  GP0 (TX) ───► Receiver RX      │
      │  GP1 (RX) ◄─── Receiver TX      │
      │  VBUS (5V) ──► Receiver 5V      │
      │  GND      ───► Receiver GND     │
      │                                 │
      │  GP16 (LED) ──► WS2812 RGB      │
      └─────────────────────────────────┘
```
*(Place your hardware photo here: `./docs/wiring.png`)*

---

## 🚦 LED Status Indicators

| Color | Meaning |
| :--- | :--- |
| **🟢 Solid Green** | Link Established (RC Signal OK) |
| **🔴 Blinking Red** | No Signal / Failsafe |
| **🟠 Pulsing Orange** | Passthrough Mode Active (Configuring RX) |

---

## 🕹️ Channel Mapping

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

## 🛠️ Installation & Setup

1.  **Requirements:** Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO IDE](https://platformio.org/platformio-ide) extension.
2.  **Clone:** `git clone https://github.com/SSpicha/RP2040-CRSF-to-USB-Gamepad.git`
3.  **Build & Flash:** 
    - Connect your RP2040 board to your PC.
    - Click the **PlatformIO: Build** (✓) or **Upload** (→) button in the status bar.
    - The firmware uses the **Earle Philhower** core with **TinyUSB** for maximum performance.

---

## 🔄 Passthrough Mode (ELRS Configuration)

To update your ExpressLRS receiver or change settings via the ELRS Configurator:
1.  Connect your radio and receiver (ensure they are bound).
2.  Hold **Channel 9 (AUX 5)** at its maximum position (>1750) for **2 seconds**.
3.  The LED will pulse **Orange**.
4.  Open **ELRS Configurator**, select **Serial** method, and choose the Pico's COM port.
5.  *To exit:* Hold CH9 again for 2 seconds.

---

## 📜 License

This project is open-source. Feel free to modify and adapt it for your needs!

---
**Developed by [SSpicha](https://github.com/SSpicha)**
