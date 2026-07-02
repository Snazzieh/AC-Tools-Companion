#include "http_probe.h"

#include <esp_system.h>
#include <WiFiClient.h>

static const char *EXOSOCKET_PATHS[] = {
    "/"
};
static const char *EXOSOCKET_PROTOCOL = "EXOsocket";

struct HandshakeVariant {
    const char *name;
    const char *key;
    bool includeUserAgent;
};

static const HandshakeVariant HANDSHAKE_VARIANTS[] = {
    {"python-websockets", "MTIzNDU2Nzg5MDEyMzQ1Ng==", true},
    {"minimal", "MTIzNDU2Nzg5MDEyMzQ1Ng==", false}
};

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

static void sendWebSocketHandshake(WiFiClient &client, const String &host, const char *path, const HandshakeVariant &variant) {
    String request;
    request.reserve(260);
    request += "GET ";
    request += path;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += host;
    request += "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: ";
    request += variant.key;
    request += "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "Sec-WebSocket-Protocol: ";
    request += EXOSOCKET_PROTOCOL;
    request += "\r\n";
    if (variant.includeUserAgent) {
        request += "User-Agent: Python/3.12 websockets/15.0.1\r\n";
    }
    request += "\r\n";

    client.write(reinterpret_cast<const uint8_t *>(request.c_str()), request.length());
    client.flush();
}

static bool sendWebSocketText(WiFiClient &client, const String &text) {
    size_t len = text.length();
    if (len > 125) return false;

    uint8_t mask[4];
    uint32_t randomValue = esp_random();
    mask[0] = randomValue & 0xFF;
    mask[1] = (randomValue >> 8) & 0xFF;
    mask[2] = (randomValue >> 16) & 0xFF;
    mask[3] = (randomValue >> 24) & 0xFF;

    uint8_t header[6] = {
        0x81,
        static_cast<uint8_t>(0x80 | len),
        mask[0],
        mask[1],
        mask[2],
        mask[3]
    };

    client.write(header, sizeof(header));

    for (size_t i = 0; i < len; i++) {
        uint8_t encoded = static_cast<uint8_t>(text[i]) ^ mask[i % 4];
        client.write(&encoded, 1);
    }

    client.flush();
    return true;
}

static bool readExact(WiFiClient &client, uint8_t *buffer, size_t len, uint32_t timeoutMs) {
    size_t readCount = 0;
    uint32_t start = millis();

    while (readCount < len && millis() - start < timeoutMs) {
        while (client.available() && readCount < len) {
            int value = client.read();
            if (value < 0) break;
            buffer[readCount++] = static_cast<uint8_t>(value);
        }
        delay(5);
        yield();
    }

    return readCount == len;
}

static String readWebSocketText(WiFiClient &client, uint32_t timeoutMs) {
    uint8_t header[2];
    if (!readExact(client, header, sizeof(header), timeoutMs)) return "";

    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t len = header[1] & 0x7F;

    if (len == 126) {
        uint8_t ext[2];
        if (!readExact(client, ext, sizeof(ext), timeoutMs)) return "";
        len = (static_cast<uint16_t>(ext[0]) << 8) | ext[1];
    } else if (len == 127) {
        return "";
    }

    if (len > 512) return "";

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked && !readExact(client, mask, sizeof(mask), timeoutMs)) return "";

    String text;
    text.reserve(static_cast<unsigned int>(len));

    for (uint64_t i = 0; i < len; i++) {
        uint8_t byte = 0;
        if (!readExact(client, &byte, 1, timeoutMs)) return "";
        if (masked) byte ^= mask[i % 4];
        if (opcode == 0x1 && byte >= 32 && byte <= 126) text += static_cast<char>(byte);
        else if (opcode == 0x1 && (byte == '\n' || byte == '\r' || byte == '\t')) text += ' ';
    }

    return text;
}

static HttpProbeResult probePath(const String &host, const char *path, const HandshakeVariant &variant) {
    HttpProbeResult result;
    result.host = host;
    result.path = path;

    WiFiClient client;
    client.setTimeout(5000);

    Serial.printf("WS probe ws://%s%s protocol=%s variant=%s\n",
                  result.host.c_str(),
                  result.path.c_str(),
                  EXOSOCKET_PROTOCOL,
                  variant.name);
    if (!client.connect(result.host.c_str(), 80)) {
        result.error = "TCP connect failed";
        Serial.println(result.error);
        return result;
    }

    sendWebSocketHandshake(client, result.host, path, variant);

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

    if (result.ok) {
        const String versionOffer = "{\"method\":\"versionOffer\",\"params\":{\"version\":1,\"featureLevel\":0,\"capabilities\":0}}";
        Serial.printf("WS send versionOffer bytes=%u\n", versionOffer.length());
        if (sendWebSocketText(client, versionOffer)) {
            result.sample = readWebSocketText(client, 5000);
            Serial.printf("WS version reply: %s\n", result.sample.c_str());
            if (result.sample.indexOf("versionAck") < 0) {
                result.error = "WebSocket OK, versionAck missing";
            }
        } else {
            result.error = "Could not send versionOffer";
        }
    }

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

    Serial.printf("WS result variant=%s status=%d line=%s protocol=%s accept=%s sample=%s\n",
                  variant.name,
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
        for (const auto &variant : HANDSHAKE_VARIANTS) {
            last = probePath(host, path, variant);
            if (last.ok) return last;
            delay(200);
        }
    }

    return last;
}
