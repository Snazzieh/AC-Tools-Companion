#pragma once

#include <Arduino.h>

struct TouchPoint {
    bool pressed = false;
    int16_t x = 0;
    int16_t y = 0;
    uint16_t rawX = 0;
    uint16_t rawY = 0;
};

class TouchInput {
public:
    void begin();
    TouchPoint read();
    bool wasTapped(uint16_t debounceMs = 220);

private:
    uint16_t readAxis(uint8_t command);
    TouchPoint mapRaw(uint16_t rawX, uint16_t rawY) const;
    uint32_t lastTapMs = 0;
};
