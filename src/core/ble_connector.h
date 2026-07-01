#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "controller_info.h"

struct BleConnectResult {
    bool bleConnected = false;
    bool serviceFound = false;
    bool rxFound = false;
    bool txFound = false;
    bool ok = false;
    String error;
};

class BleConnector {
public:
    BleConnectResult testUart(const ControllerInfo &controller);
};
