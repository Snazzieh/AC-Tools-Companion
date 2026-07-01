#pragma once

#include <Arduino.h>
#include <vector>

struct ControllerInfo {
    String name;
    String address;
    uint8_t addressType = 0;
    int rssi = -127;
};

class BleScanner {
public:
    void begin();
    std::vector<ControllerInfo> scan(uint32_t seconds = 5);
};
