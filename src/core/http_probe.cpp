#include "http_probe.h"

#include <WiFiClient.h>

static const char *EXOSOCKET_PATHS[] = {
    "/",
    "/_EXOsocket",
    "/_EXOsocket/"
};
static const char *EXOSOCKET_PROTOCOL = "EXOsocket";
static const char *WS_KEY = "dGhlIHNhbXBsZSBub25jZQ==";

static int parseStatusCode(const String &statusLine) {
    int firstSpace = statusLine.indexOf(' ');
    if (firstSpace < 0 || firstSpace + 4 > static_cast<int>(statusLine.length())) return 0;
    return statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
}

static String readLine(WiFiClient &client, uint32_t timeoutMs) {
    String line;
    uint32_t start = millis();

    while (millis() - start < timeoutMs) {
        while (client.available()) {
            char c = static_cast<char>(client.read());
            if (c == '\r') continue;
            if (c == '\n') return line;
            if (line.length() < 180) line += c;
        }
        delay(5);
        yield();
    }

    return line;
}

static void captureHeader(HttpProbeResult &result, const String &line) {
    String lower = line;
    lower.toLowerCase();

    if (lower.startsWith("sec-websocket-protocol:")) {
        result.protocol = line.substring(line.indexOf(':') + 1);
        result.protocol.trim();
    } else if (lower.startsWith("sec-websocket-accept:")) {
        result.accept = line.substring(line.indexOf(':') + 1);
        result.accept.trim();
    }
}

static void sendWebSocketHandshake(WiFiClient &client, const String &host, const char *path) {
    client.printf("GET %s HTTP/1.1\r\n", path);
    client.printf("Host: %s\r\n", host.c_str());
    client.print("Upgrade: websocket\r\n");
    client.print("Connection: Upgrade\r\n");
    client.printf("Sec-WebSocket-Key: %s\r\n", WS_KEY);
    client.print("Sec-WebSocket-Version: 13\r\n");
    client.printf("Sec-WebSocket-Protocol: %s\r\n", EXOSOCKET_PROTOCOL);
    client.print("Origin: http://");
    client.print(host);
    client.print("\r\n\r\n");
}

static HttpProbeResult probePath(const String &host, const char *path) {
    HttpProbeResult result;
    result.host = host;
    result.path = path;

    WiFiClient client;
    client.setTimeout(5000);

    Serial.printf("WS probe ws://%s%s protocol=%s\n", result.host.c_str(), result.path.c_str(), EXOSOCKET_PROTOCOL);
    if (!client.connect(result.host.c_str(), 80)) {
        result.error = "TCP connect failed";
        Serial.println(result.error);
        return result;
    }

    sendWebSocketHandshake(client, result.host, path);

    result.statusLine = readLine(client, 5000);
    result.statusCode = parseStatusCode(result.statusLine);

    uint32_t headerStart = millis();
    while (millis() - headerStart < 5000) {
        String line = readLine(client, 1000);
        if (line.length() == 0) break;
        captureHeader(result, line);
        Serial.printf("WS header: %s\n", line.c_str());
    }

    result.ok = result.statusCode == 101;

    if (!result.ok) {
        uint32_t bodyStart = millis();
        while (millis() - bodyStart < 1000 && result.sample.length() < 160) {
            while (client.available() && result.sample.length() < 160) {
                char c = static_cast<char>(client.read());
                if (c >= 32 && c <= 126) result.sample += c;
                else if (c == '\n' || c == '\r' || c == '\t') result.sample += ' ';
            }
            delay(5);
            yield();
        }
        result.sample.trim();
        if (result.error.length() == 0) result.error = "WebSocket upgrade failed";
    }

    Serial.printf("WS result status=%d line=%s protocol=%s accept=%s sample=%s\n",
                  result.statusCode,
                  result.statusLine.c_str(),
                  result.protocol.c_str(),
                  result.accept.c_str(),
                  result.sample.c_str());

    client.stop();
    return result;
}

HttpProbeResult HttpProbe::probe(const WifiConnectResult &wifi) {
    HttpProbeResult last;
    String host = wifi.gatewayIp.length() > 0 ? wifi.gatewayIp : "192.168.254.1";

    Serial.println("=== EXOsocket WebSocket probe ===");
    Serial.printf("WiFi ok=%s host=%s\n", wifi.ok ? "yes" : "no", host.c_str());

    if (!wifi.ok) {
        last.host = host;
        last.error = "WiFi not connected";
        Serial.println(last.error);
        return last;
    }

    for (const char *path : EXOSOCKET_PATHS) {
        last = probePath(host, path);
        if (last.ok) return last;
        delay(200);
    }

    return last;
}
