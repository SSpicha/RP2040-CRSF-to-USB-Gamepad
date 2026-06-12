#pragma once
#include <Arduino.h>

class CRSFTransport {
public:
    struct Stats {
        uint32_t overflowCount = 0;
        uint32_t maxBufferedBytes = 0;
    };
    
    void begin(uint32_t pin, uint32_t baud) {
        // RP2040 Hardware UART0 is GP0 (TX) and GP1 (RX)
        // We use Serial1 which is mapped to UART0
        Serial1.setRX(pin);
        Serial1.setTX(0); // TX is always 0 in this config
        Serial1.begin(baud);
    }

    uint32_t available() {
        return Serial1.available();
    }

    uint8_t read() {
        return (uint8_t)Serial1.read();
    }

    Stats getStats() const {
        // Serial1 handles stats internally, returning empty for API compatibility
        return _stats;
    }

private:
    Stats _stats;
};
