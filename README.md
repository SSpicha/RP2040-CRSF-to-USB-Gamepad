# RP2040 CRSF to USB Gamepad & Passthrough

This project converts a standard CRSF signal (typically from an ExpressLRS or Crossfire receiver) into a USB HID Gamepad using a Raspberry Pi Pico (RP2040). 

It allows you to use your RC radio transmitter as a joystick for FPV simulators on your computer. It also includes a **Passthrough Mode** to configure your ELRS receiver via WiFi/USB without rewiring.

## Features

* **Low Latency:** Configured for 1000Hz (1ms) USB polling rate.
* **High Performance:** RP2040 overclocked to 200MHz.
* **CRSF Support:** Full parsing of CRSF RC channels.
* **Passthrough Mode:** Toggleable Serial Passthrough (USB <-> UART) for ELRS Configurator (activates on Channel 9).
* **Mapping:** 6 Analog Axes + 10 Buttons.

## Hardware Requirements

* Raspberry Pi Pico (or any RP2040 based board).
* ELRS or Crossfire Receiver.

## Wiring

Connect the Receiver to the RP2040 UART0:

| Receiver Pad | RP2040 Pin | Note |
| :--- | :--- | :--- |
| **TX** | **GP1** (RX) | Serial1 RX |
| **RX** | **GP0** (TX) | Serial1 TX |
| **5V** | **VBUS** (5V) | Or 3.3V if supported |
| **GND** | **GND** | Ground |

## Installation

1.  Clone this repository.
2.  Open the project in **Visual Studio Code** with the **PlatformIO** extension installed.
3.  Connect your RP2040 board while holding the BOOTSEL button.
4.  Upload the firmware.

## Usage

### Simulator Mode (Default)
Once connected, the device appears as a standard Gamepad/Joystick in Windows/Linux/macOS. 
* **Sticks (Ch 1-4):** Mapped to X, Y, Z, RZ axes.
* **Aux (Ch 5-6):** Mapped to RX, RY axes.
* **Switches (Ch 7-16):** Mapped to buttons.

### Passthrough Mode (ELRS Configurator)
To update your receiver or change settings via the ELRS Configurator:
1.  Ensure the receiver and radio are bound and linked.
2.  Set **Channel 9 (AUX 5)** on your radio to a high value (>1500).
3.  The device will switch to Passthrough mode.
4.  In ELRS Configurator, select "Serial" as the flashing method and select the COM port of the Pico.

## Configuration

You can adjust pin definitions or baud rate in `main.cpp`:

```cpp
#define CRSF_RX_PIN     1
#define CRSF_TX_PIN     0
#define CRSF_BAUD       420000
