#include "http_probe.h"

#include <WiFiClient.h>

static const char *PROBE_PATHS[] = {
    "/",
    "/api",
    "/api/v1",
    "/api/v1/version",
    "/api/v1/system"
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
            if (line.length() < 160) line += c;
        }
        delay(5);
        yield();
    }

    return line;
}

static String readSample(WiFiClient &client, uint32_t timeoutMs) {
    String sample;
    uint32_t start = millis();

    while (millis() - start < timeoutMs && sample.length() < 160) {
        while (client.available() && sample.length() < 160) {
            char c = static_cast<char>(client.read());
            if (c >= 32 && c <= 126) {
                sample += c;
            } else if (c == '\n' || c == '\r' || c == '\t') {
                sample += ' ';
            }
        }
        delay(5);
        yield();
    }

    sample.trim();
    return sample;
}

static HttpProbeResult probePath(const String &host, const char *path) {
    HttpProbeResult result;
    result.host = host;
    result.path = path;

    WiFiClient client;
    client.setTimeout(4000);

    Serial.printf("HTTP probe GET http://%s%s\n", host.c_str(), path);
    if (!client.connect(host.c_str(), 80)) {
        result.error = "TCP connect failed";
        Serial.println(result.error);
        return result;
    }

    client.printf("GET %s HTTP/1.0\r\n\r\n", path);

    result.statusLine = readLine(client, 4000);
    result.statusCode = parseStatusCode(result.statusLine);

    bool headersDone = false;
    uint32_t headerStart = millis();
    while (millis() - headerStart < 4000) {
        String line = readLine(client, 1000);
        if (line.length() == 0) {
            headersDone = true;
            break;
        }
    }

    if (headersDone) {
        result.sample = readSample(client, 1000);
    }

    client.stop();

    result.ok = result.statusCode >= 200 && result.statusCode < 400;
    Serial.printf("HTTP result path=%s status=%d line=%s sample=%s\n",
                  path,
                  result.statusCode,
                  result.statusLine.c_str(),
                  result.sample.c_str());
    return result;
}

HttpProbeResult HttpProbe::probe(const WifiConnectResult &wifi) {
    HttpProbeResult last;
    String host = wifi.gatewayIp.length() > 0 ? wifi.gatewayIp : "192.168.254.1";

    Serial.println("=== Controller HTTP probe ===");
    Serial.printf("WiFi ok=%s host=%s\n", wifi.ok ? "yes" : "no", host.c_str());

    if (!wifi.ok) {
        last.host = host;
        last.error = "WiFi not connected";
        Serial.println(last.error);
        return last;
    }

    for (const char *path : PROBE_PATHS) {
        last = probePath(host, path);
        if (last.statusCode >= 200 && last.statusCode < 400) {
            return last;
        }
        delay(150);
    }

    if (last.error.length() == 0 && last.statusCode == 0) {
        last.error = "No HTTP response";
    }
    return last;
}
