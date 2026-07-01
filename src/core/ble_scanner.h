#pragma once

#include <Arduino.h>
#include "controller_info.h"
#include <vector>

class BleScanner {
public:
    void begin();
    std::vector<ControllerInfo> scan(uint32_t seconds = 5);
};
