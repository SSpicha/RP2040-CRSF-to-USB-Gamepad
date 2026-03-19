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

    int16_t processAxis(uint16_t raw, int index, float dt) {
        // Mapping CRSF (172-1811, center 992) to HID (-32767 to 32767)
        int32_t diff = (int32_t)raw - 992;
        int16_t target;

        if (abs(diff) <= (int32_t)_cfg.deadband) {
            target = 0;
        } else {
            // Smooth exit from deadband: (raw - center - deadband) / (max - center - deadband)
            if (diff > 0) {
                target = (int16_t)(((diff - _cfg.deadband) * 32767) / (1811 - 992 - _cfg.deadband));
            } else {
                target = (int16_t)(((diff + _cfg.deadband) * 32767) / (992 - 172 - _cfg.deadband));
            }
        }
        
        target = constrain(target, -32767, 32767);

        if (!_cfg.smoothingEnabled || dt <= 0) {
            _lastValues[index] = (float)target;
            return target;
        }

        // PT1 Filter for smoothing
        // Limit dt to prevent filter explosion after long pauses
        if (dt > 0.1f) dt = 0.01f; 

        float rc = 1.0f / (2.0f * PI * _cfg.smoothingCutoff);
        float alpha = dt / (rc + dt);
        
        _lastValues[index] += alpha * ((float)target - _lastValues[index]);

        return (int16_t)_lastValues[index];
    }

    int16_t processThrottle(uint16_t raw) {
        int32_t val = ((int32_t)(raw - 172) * 65534) / (1811 - 172) - 32767;
        return (int16_t)constrain(val, -32767, 32767);
    }

private:
    Config _cfg;
    float _lastValues[16] = {0};
};
