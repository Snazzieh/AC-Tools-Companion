#pragma once

#include <Arduino.h>
#include <WiFi.h>

#include "wifi_connector.h"

struct ExoSocketGatewayResult {
    bool ok = false;
    String apSsid;
    String apIp;
    String upstreamHost;
    uint16_t port = 80;
    String error;
};

class ExoSocketGateway {
public:
    ExoSocketGatewayResult begin(const WifiConnectResult &wifi);
    void update();

private:
    struct Connection {
        WiFiClient downstream;
        WiFiClient upstream;
        bool active = false;
        bool handshaking = false;
        bool isWebSocket = false;
        uint32_t lastActivityMs = 0;
        uint32_t clientToControllerBytes = 0;
        uint32_t controllerToClientBytes = 0;
        String requestBuffer;
    };

    static constexpr uint8_t MAX_CONNECTIONS = 12;

    WiFiServer server{80};
    bool running = false;
    String upstreamHost;
    Connection connections[MAX_CONNECTIONS];

    bool connectUpstream(Connection &connection);
    void closeConnection(Connection &connection);
    void acceptClient();
    void updateConnection(Connection &connection);
    void pumpClientToController(Connection &connection);
    void pumpControllerToClient(Connection &connection);
    bool forwardInitialRequest(Connection &connection);
};
