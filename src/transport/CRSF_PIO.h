#pragma once
#include <Arduino.h>

class CRSFTransport {
public:
    void begin(uint32_t pin, uint32_t baud) {
        // RP2040 Serial1 (UART0) can be mapped to GP1 (RX) and GP0 (TX)
        Serial1.setRX(pin); 
        Serial1.begin(baud);
        // Ensure the pin has a pull-up to prevent noise
        gpio_pull_up(pin);
    }

    uint32_t available() {
        return Serial1.available();
    }

    uint8_t read() {
        return Serial1.read();
    }
};
