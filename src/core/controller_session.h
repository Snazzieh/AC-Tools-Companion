#pragma once

#include <Arduino.h>
#include <vector>
#include "ble_connector.h"
#include "ble_scanner.h"
#include "hotspot_reader.h"
#include "wifi_connector.h"

class ControllerSession {
public:
    void begin();
    std::vector<ControllerInfo> scan(uint32_t seconds = 5);
    BleConnectResult connect(const ControllerInfo &controller);
    HotspotCredentials readHotspot(const ControllerInfo &controller);
    WifiConnectResult connectWifi(const HotspotCredentials &credentials);

private:
    BleScanner scanner;
    BleConnector connector;
    HotspotReader hotspotReader;
    WifiConnector wifiConnector;
};
