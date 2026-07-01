#pragma once

#include <Arduino.h>
#include "hotspot_reader.h"

struct WifiConnectResult {
    bool ok = false;
    String ssid;
    String localIp;
    String gatewayIp;
    int32_t rssi = 0;
    int status = 0;
    String error;
};

class WifiConnector {
public:
    WifiConnectResult connect(const HotspotCredentials &credentials, uint32_t timeoutMs = 30000);
};
