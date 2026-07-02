#include "exosocket_client.h"

#include <esp_system.h>
#include <WiFiClient.h>

namespace {
constexpr const char *EXOSOCKET_PROTOCOL = "EXOsocket";
constexpr const char *WS_KEY = "MTIzNDU2Nzg5MDEyMzQ1Ng==";
constexpr const char *WS_PATH = "/";
constexpr const char *VERSION_OFFER = "{\"method\":\"versionOffer\",\"params\":{\"version\":1,\"featureLevel\":0,\"capabilities\":0}}";

int parseStatusCode(const String &statusLine) {
    int firstSpace = statusLine.indexOf(' ');
    if (firstSpace < 0 || firstSpace + 4 > static_cast<int>(statusLine.length())) return 0;
    return statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();
}

String readLine(WiFiClient &client, uint32_t timeoutMs) {
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

void captureHeader(ExoSocketConnectResult &result, const String &line) {
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

void sendHandshake(WiFiClient &client, const String &host) {
    String request;
    request.reserve(260);
    request += "GET ";
    request += WS_PATH;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += host;
    request += "\r\n";
    request += "Upgrade: websocket\r\n";
    request += "Connection: Upgrade\r\n";
    request += "Sec-WebSocket-Key: ";
    request += WS_KEY;
    request += "\r\n";
    request += "Sec-WebSocket-Version: 13\r\n";
    request += "Sec-WebSocket-Protocol: ";
    request += EXOSOCKET_PROTOCOL;
    request += "\r\n";
    request += "User-Agent: Python/3.12 websockets/15.0.1\r\n";
    request += "\r\n";

    client.write(reinterpret_cast<const uint8_t *>(request.c_str()), request.length());
    client.flush();
}

bool sendTextFrame(WiFiClient &client, const String &text) {
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

bool readExact(WiFiClient &client, uint8_t *buffer, size_t len, uint32_t timeoutMs) {
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

String readTextFrame(WiFiClient &client, uint32_t timeoutMs) {
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
}

ExoSocketConnectResult ExoSocketClient::connectAndVersionOffer(const String &host) {
    ExoSocketConnectResult result;
    result.host = host;
    result.path = WS_PATH;

    WiFiClient client;
    client.setTimeout(5000);

    Serial.printf("EXO connect ws://%s/ protocol=%s\n", host.c_str(), EXOSOCKET_PROTOCOL);
    if (!client.connect(host.c_str(), 80)) {
        result.error = "TCP connect failed";
        Serial.println(result.error);
        return result;
    }

    sendHandshake(client, host);

    result.statusLine = readLine(client, 5000);
    result.statusCode = parseStatusCode(result.statusLine);

    uint32_t headerStart = millis();
    while (millis() - headerStart < 5000) {
        String line = readLine(client, 1000);
        if (line.length() == 0) break;
        captureHeader(result, line);
        Serial.printf("EXO header: %s\n", line.c_str());
    }

    if (result.statusCode != 101) {
        result.error = "WebSocket upgrade failed";
        client.stop();
        return result;
    }

    Serial.printf("EXO send versionOffer bytes=%u\n", String(VERSION_OFFER).length());
    if (!sendTextFrame(client, VERSION_OFFER)) {
        result.error = "Could not send versionOffer";
        client.stop();
        return result;
    }

    result.versionReply = readTextFrame(client, 5000);
    result.versionAck = result.versionReply.indexOf("versionAck") >= 0;
    result.ok = result.versionAck;

    if (!result.versionAck) {
        result.error = "versionAck missing";
    }

    Serial.printf("EXO version reply: %s\n", result.versionReply.c_str());
    client.stop();
    return result;
}
