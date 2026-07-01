#pragma once

#include <Arduino.h>
#include "ble_scanner.h"

struct HotspotCredentials {
    String ssid;
    String password;
    String ip;
    bool ok = false;

    bool bleConnected = false;
    bool serviceFound = false;
    bool rxFound = false;
    bool txFound = false;
    String error;
};

class HotspotReader {
public:
    HotspotCredentials read(const ControllerInfo &controller);
};
