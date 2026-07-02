#pragma once

#include <Arduino.h>

struct ExoSocketConnectResult {
    bool ok = false;
    bool versionAck = false;
    String host;
    String path = "/";
    int statusCode = 0;
    String statusLine;
    String protocol;
    String accept;
    String versionReply;
    String error;
};

class ExoSocketClient {
public:
    ExoSocketConnectResult connectAndVersionOffer(const String &host);
};
