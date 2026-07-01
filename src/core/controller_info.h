#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <memory>

struct ControllerInfo {
    String name;
    String address;
    uint8_t addressType = 0;
    int rssi = -127;
    std::shared_ptr<NimBLEAdvertisedDevice> advertisedDevice;

    bool hasAdvertisedDevice() const {
        return advertisedDevice != nullptr;
    }
};
