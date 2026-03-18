#pragma once
#include <Arduino.h>

class RCProcessor {
public:
    struct Config {
        bool smoothingEnabled = false;
        float smoothingCutoff = 50.0f; // Hz
        uint16_t deadband = 4;
    };

    void setConfig(const Config &cfg) { _cfg = cfg; }

    int16_t processAxis(uint16_t raw, int index) {
        // Mapping CRSF (172-1811) to HID (-32767 to 32767)
        int16_t target;
        if (abs((int)raw - 992) < _cfg.deadband) {
            target = 0;
        } else if (raw >= 992) {
            target = ((int32_t)(raw - 992) * 32767) / (1811 - 992);
        } else {
            target = ((int32_t)(raw - 992) * 32767) / (992 - 172);
        }

        if (!_cfg.smoothingEnabled) {
            _lastValues[index] = target;
            return target;
        }

        // PT1 Filter for smoothing
        float dt = (micros() - _lastTime) / 1000000.0f;
        if (dt > 0.1f) dt = 0.001f; // Reset if too long gap

        float rc = 1.0f / (2.0f * PI * _cfg.smoothingCutoff);
        float alpha = dt / (rc + dt);
        
        _lastValues[index] += alpha * (target - _lastValues[index]);
        _lastTime = micros();

        return (int16_t)_lastValues[index];
    }

    int16_t processThrottle(uint16_t raw) {
        int32_t val = ((int32_t)(raw - 172) * 65534) / (1811 - 172) - 32767;
        return (int16_t)constrain(val, -32767, 32767);
    }

private:
    Config _cfg;
    float _lastValues[16] = {0};
    uint32_t _lastTime = 0;
};
