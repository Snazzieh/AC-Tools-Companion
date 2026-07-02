#include "exosocket_gateway.h"

namespace {
constexpr const char *GATEWAY_AP_SSID = "AC Tools Companion";
constexpr const char *GATEWAY_AP_PASSWORD = "actools123";
constexpr uint16_t GATEWAY_PORT = 80;
constexpr uint32_t HEADER_TIMEOUT_MS = 5000;
constexpr uint32_t HTTP_IDLE_TIMEOUT_MS = 3000;

String rewriteHostHeader(String request, const String &host) {
    int lineStart = 0;

    while (lineStart >= 0 && lineStart < static_cast<int>(request.length())) {
        int lineEnd = request.indexOf("\r\n", lineStart);
        if (lineEnd < 0) break;

        String line = request.substring(lineStart, lineEnd);
        String lower = line;
        lower.toLowerCase();

        if (lower.startsWith("host:")) {
            String replacement = "Host: " + host;
            request = request.substring(0, lineStart) + replacement + request.substring(lineEnd);
            return request;
        }

        lineStart = lineEnd + 2;
    }

    int insertAt = request.indexOf("\r\n");
    if (insertAt > 0) {
        request = request.substring(0, insertAt + 2) + "Host: " + host + "\r\n" + request.substring(insertAt + 2);
    }

    return request;
}

String rewriteOrAddHeader(String request, const char *header, const String &value) {
    int lineStart = 0;
    String prefix = header;
    prefix.toLowerCase();
    prefix += ":";

    while (lineStart >= 0 && lineStart < static_cast<int>(request.length())) {
        int lineEnd = request.indexOf("\r\n", lineStart);
        if (lineEnd < 0) break;

        String line = request.substring(lineStart, lineEnd);
        String lower = line;
        lower.toLowerCase();

        if (lower.startsWith(prefix)) {
            String replacement = String(header) + ": " + value;
            return request.substring(0, lineStart) + replacement + request.substring(lineEnd);
        }

        lineStart = lineEnd + 2;
    }

    int insertAt = request.indexOf("\r\n");
    if (insertAt > 0) {
        request = request.substring(0, insertAt + 2) + String(header) + ": " + value + "\r\n" + request.substring(insertAt + 2);
    }

    return request;
}

bool containsHeaderValue(const String &request, const char *header, const char *value) {
    String lower = request;
    lower.toLowerCase();

    String needle = header;
    needle.toLowerCase();
    needle += ":";

    String expected = value;
    expected.toLowerCase();

    int headerStart = lower.indexOf(needle);
    if (headerStart < 0) return false;

    int lineEnd = lower.indexOf("\r\n", headerStart);
    String line = lineEnd > headerStart ? lower.substring(headerStart, lineEnd) : lower.substring(headerStart);
    return line.indexOf(expected) >= 0;
}

String prepareInitialRequest(String request, const String &host, bool isWebSocket) {
    request = rewriteHostHeader(request, host);

    if (!isWebSocket) {
        request = rewriteOrAddHeader(request, "Connection", "close");
    }

    return request;
}

size_t writeAll(WiFiClient &client, const uint8_t *data, size_t len) {
    size_t written = 0;
    uint32_t start = millis();

    while (written < len && client && client.connected()) {
        size_t chunk = client.write(data + written, len - written);
        if (chunk > 0) {
            written += chunk;
            start = millis();
            continue;
        }

        if (millis() - start > 1000) break;
        delay(1);
    }

    if (written < len) {
        Serial.printf("Gateway short write requested=%u written=%u\n", len, written);
    }

    return written;
}
}

ExoSocketGatewayResult ExoSocketGateway::begin(const WifiConnectResult &wifi) {
    ExoSocketGatewayResult result;
    result.upstreamHost = wifi.gatewayIp.length() > 0 ? wifi.gatewayIp : "192.168.254.1";
    result.apSsid = GATEWAY_AP_SSID;
    result.port = GATEWAY_PORT;

    Serial.println("=== EXOsocket gateway start ===");
    Serial.printf("WiFi ok=%s upstream=%s\n", wifi.ok ? "yes" : "no", result.upstreamHost.c_str());

    if (!wifi.ok) {
        result.error = "WiFi not connected";
        Serial.println(result.error);
        return result;
    }

    upstreamHost = result.upstreamHost;

    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleep(false);
    WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

    if (!WiFi.softAP(GATEWAY_AP_SSID, GATEWAY_AP_PASSWORD)) {
        result.error = "SoftAP start failed";
        Serial.println(result.error);
        return result;
    }

    server.begin();
    server.setNoDelay(true);
    running = true;

    result.ok = true;
    result.apIp = WiFi.softAPIP().toString();

    Serial.printf("Gateway AP SSID=%s password=%s ip=%s port=%u upstream=%s:80\n",
                  GATEWAY_AP_SSID,
                  GATEWAY_AP_PASSWORD,
                  result.apIp.c_str(),
                  GATEWAY_PORT,
                  upstreamHost.c_str());

    return result;
}

void ExoSocketGateway::update() {
    if (!running) return;

    acceptClient();

    for (auto &connection : connections) {
        updateConnection(connection);
    }
}

void ExoSocketGateway::updateConnection(Connection &connection) {
    if (!connection.active) return;

    uint32_t now = millis();

    if (connection.handshaking && now - connection.lastActivityMs > HEADER_TIMEOUT_MS) {
        Serial.println("Gateway client closed: header timeout");
        closeConnection(connection);
        return;
    }

    if (!connection.handshaking && !connection.isWebSocket && now - connection.lastActivityMs > HTTP_IDLE_TIMEOUT_MS) {
        Serial.println("Gateway HTTP client closed: idle timeout");
        closeConnection(connection);
        return;
    }

    if (!connection.downstream) {
        closeConnection(connection);
        return;
    }

    if (!connection.upstream) {
        closeConnection(connection);
        return;
    }

    if (connection.handshaking) {
        if (!forwardInitialRequest(connection)) {
            return;
        }
        connection.handshaking = false;
    }

    if (connection.isWebSocket) {
        pumpClientToController(connection);
    }
    pumpControllerToClient(connection);

    bool downstreamDone = !connection.downstream.connected() && connection.downstream.available() == 0;
    bool upstreamDone = !connection.upstream.connected() && connection.upstream.available() == 0;
    if (downstreamDone || upstreamDone) {
        closeConnection(connection);
    }
}

bool ExoSocketGateway::connectUpstream(Connection &connection) {
    connection.upstream.stop();

    Serial.printf("Gateway connecting upstream %s:80...\n", upstreamHost.c_str());
    if (!connection.upstream.connect(upstreamHost.c_str(), 80)) {
        Serial.println("Gateway upstream connect failed");
        return false;
    }

    connection.upstream.setNoDelay(true);
    Serial.println("Gateway upstream connected");
    return true;
}

void ExoSocketGateway::closeConnection(Connection &connection) {
    if (connection.active) {
        Serial.printf("Gateway connection closed c2u=%u u2c=%u ws=%s\n",
                      connection.clientToControllerBytes,
                      connection.controllerToClientBytes,
                      connection.isWebSocket ? "yes" : "no");
    }

    if (connection.downstream) connection.downstream.stop();
    if (connection.upstream) connection.upstream.stop();
    connection.requestBuffer = "";
    connection.handshaking = false;
    connection.isWebSocket = false;
    connection.clientToControllerBytes = 0;
    connection.controllerToClientBytes = 0;
    connection.active = false;
}

void ExoSocketGateway::acceptClient() {
    for (uint8_t accepted = 0; accepted < MAX_CONNECTIONS; accepted++) {
        WiFiClient candidate = server.available();
        if (!candidate) return;

        Connection *slot = nullptr;
        for (auto &connection : connections) {
            if (!connection.active) {
                slot = &connection;
                break;
            }
        }

        if (!slot) {
            Serial.println("Gateway client rejected: no free connection slots");
            candidate.stop();
            return;
        }

        candidate.setNoDelay(true);
        slot->downstream = candidate;
        slot->requestBuffer = "";
        slot->handshaking = true;
        slot->isWebSocket = false;
        slot->lastActivityMs = millis();
        slot->clientToControllerBytes = 0;
        slot->controllerToClientBytes = 0;
        slot->active = true;

        Serial.printf("Gateway client connected from %s\n", slot->downstream.remoteIP().toString().c_str());

        if (!connectUpstream(*slot)) {
            closeConnection(*slot);
        }
    }
}

bool ExoSocketGateway::forwardInitialRequest(Connection &connection) {
    while (connection.downstream.available()) {
        char c = static_cast<char>(connection.downstream.read());
        if (connection.requestBuffer.length() < 1536) connection.requestBuffer += c;
        connection.lastActivityMs = millis();

        if (connection.requestBuffer.endsWith("\r\n\r\n")) {
            bool isWebSocket = containsHeaderValue(connection.requestBuffer, "Upgrade", "websocket");
            bool isExoSocket = containsHeaderValue(connection.requestBuffer, "Sec-WebSocket-Protocol", "EXOsocket");
            connection.isWebSocket = isWebSocket;
            String rewritten = prepareInitialRequest(connection.requestBuffer, upstreamHost, isWebSocket);
            int firstLineEnd = rewritten.indexOf("\r\n");
            String firstLine = firstLineEnd > 0 ? rewritten.substring(0, firstLineEnd) : rewritten;
            size_t written = writeAll(connection.upstream,
                                      reinterpret_cast<const uint8_t *>(rewritten.c_str()),
                                      rewritten.length());
            connection.upstream.flush();
            connection.clientToControllerBytes += written;
            Serial.printf("Gateway forwarded initial request bytes=%u ws=%s exo=%s line=%s\n",
                          rewritten.length(),
                          isWebSocket ? "yes" : "no",
                          isExoSocket ? "yes" : "no",
                          firstLine.c_str());
            connection.requestBuffer = "";
            return true;
        }
    }

    if (connection.requestBuffer.length() >= 1536) {
        Serial.println("Gateway initial request too large");
        closeConnection(connection);
    }

    return false;
}

void ExoSocketGateway::pumpClientToController(Connection &connection) {
    uint8_t buffer[1024];

    while (connection.downstream.available()) {
        size_t len = connection.downstream.read(buffer, sizeof(buffer));
        if (len == 0) break;
        size_t written = writeAll(connection.upstream, buffer, len);
        connection.clientToControllerBytes += written;
        connection.lastActivityMs = millis();
        if (written < len) {
            closeConnection(connection);
            return;
        }
    }
}

void ExoSocketGateway::pumpControllerToClient(Connection &connection) {
    uint8_t buffer[1024];

    while (connection.upstream.available()) {
        size_t len = connection.upstream.read(buffer, sizeof(buffer));
        if (len == 0) break;
        size_t written = writeAll(connection.downstream, buffer, len);
        connection.controllerToClientBytes += written;
        connection.lastActivityMs = millis();
        if (written < len) {
            closeConnection(connection);
            return;
        }
    }
}
