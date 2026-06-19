#pragma once
#include <Arduino.h>

class RCProcessor {
public:
    struct AxisConfig {
        uint16_t min = 172;
        uint16_t max = 1811;
        uint8_t invert = 0; // 0 = normal, 1 = inverted
    };

    struct Config {
        bool smoothingEnabled = false;
        float smoothingCutoff = 50.0f; // Hz
        uint16_t deadband = 4;
        AxisConfig axes[6];
    };

    void setConfig(const Config &cfg) { 
        _cfg = cfg; 
        if (_cfg.smoothingCutoff < 0.1f) _cfg.smoothingCutoff = 0.1f;
    }

    int16_t processAxis(uint16_t raw, int index, float dt) {
        // Dynamic mapping based on calibration min/max values per axis
        uint16_t minVal = (index >= 0 && index < 6) ? _cfg.axes[index].min : 172;
        uint16_t maxVal = (index >= 0 && index < 6) ? _cfg.axes[index].max : 1811;
        uint16_t centerVal = (minVal + maxVal) / 2;

        int32_t target;
        if (abs((int)raw - centerVal) < _cfg.deadband) {
            target = 0;
        } else if (raw >= centerVal) {
            if (maxVal > centerVal) {
                target = ((int32_t)(raw - centerVal) * 32767) / (maxVal - centerVal);
            } else {
                target = 0;
            }
        } else {
            if (centerVal > minVal) {
                target = ((int32_t)(raw - centerVal) * 32767) / (centerVal - minVal);
            } else {
                target = 0;
            }
        }
        
        target = constrain(target, -32767, 32767);

        if (index >= 0 && index < 6 && _cfg.axes[index].invert) {
            target = -target;
        }

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
