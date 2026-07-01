#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>

struct ControllerInfo {
    String name;
    String address;
    uint8_t addressType = 0;
    int rssi = -127;
    bool connectable = false;
    bool scannable = false;
    const NimBLEAdvertisedDevice *advertisedDevice = nullptr;

    bool hasAdvertisedDevice() const {
        return advertisedDevice != nullptr;
    }
};
