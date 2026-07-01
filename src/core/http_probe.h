#pragma once

#include <Arduino.h>
#include "wifi_connector.h"

struct HttpProbeResult {
    bool ok = false;
    String host;
    String path;
    int statusCode = 0;
    String statusLine;
    String sample;
    String error;
};

class HttpProbe {
public:
    HttpProbeResult probe(const WifiConnectResult &wifi);
};
