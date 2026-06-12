#pragma once
#include <Arduino.h>

class RCProcessor {
public:
    struct Config {
        bool smoothingEnabled = false;
        float smoothingCutoff = 50.0f; // Hz
        uint16_t deadband = 4;
    };

    void setConfig(const Config &cfg) { 
        _cfg = cfg; 
        if (_cfg.smoothingCutoff < 0.1f) _cfg.smoothingCutoff = 0.1f;
    }

    int16_t processAxis(uint16_t raw, int index, float dt) {
        // Mapping CRSF (172-1811) to HID (-32767 to 32767)
        int32_t target;
        if (abs((int)raw - 992) < _cfg.deadband) {
            target = 0;
        } else if (raw >= 992) {
            target = ((int32_t)(raw - 992) * 32767) / (1811 - 992);
        } else {
            target = ((int32_t)(raw - 992) * 32767) / (992 - 172);
        }
        
        target = constrain(target, -32767, 32767);

        if (!_cfg.smoothingEnabled) {
            _lastValues[index] = target;
            return (int16_t)target;
        }

        // PT1 Filter for smoothing
        float rc = 1.0f / (2.0f * PI * _cfg.smoothingCutoff);
        float alpha = dt / (rc + dt);
        
        _lastValues[index] += alpha * (target - _lastValues[index]);
        return (int16_t)_lastValues[index];
    }

private:
    Config _cfg;
    float _lastValues[16] = {0};
};
