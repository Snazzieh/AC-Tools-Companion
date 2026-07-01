#pragma once

#include <Arduino.h>
#include <vector>

struct ControllerInfo {
    String name;
    String address;
    int rssi;
};

class BleScanner {
public:
    void begin();
    std::vector<ControllerInfo> scan(uint32_t seconds = 5);
};