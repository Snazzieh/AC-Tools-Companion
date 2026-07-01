#pragma once
#include <Arduino.h>

struct HotspotCredentials {
    String ssid;
    String password;
    String ip;
    bool ok = false;
};

class HotspotReader {
public:
    HotspotCredentials read(const String &address);
};