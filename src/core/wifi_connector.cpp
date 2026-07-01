#include "wifi_connector.h"

#include <WiFi.h>

static const char *wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
        case WL_CONNECTED: return "WL_CONNECTED";
        case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED: return "WL_DISCONNECTED";
        default: return "WL_UNKNOWN";
    }
}

WifiConnectResult WifiConnector::connect(const HotspotCredentials &credentials, uint32_t timeoutMs) {
    WifiConnectResult result;
    result.ssid = credentials.ssid;
    result.gatewayIp = credentials.ip.length() > 0 ? credentials.ip : "192.168.10.1";

    Serial.println("=== WiFi hotspot connect test ===");
    Serial.printf("SSID=%s passwordLen=%u controllerIp=%s\n",
                  credentials.ssid.c_str(),
                  static_cast<unsigned>(credentials.password.length()),
                  result.gatewayIp.c_str());

    if (!credentials.ok || credentials.ssid.length() == 0 || credentials.password.length() == 0) {
        result.error = "Missing hotspot credentials";
        Serial.println(result.error);
        return result;
    }

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(true, true);
    delay(300);

    Serial.println("Connecting to controller hotspot...");
    WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());

    uint32_t start = millis();
    uint32_t lastLog = 0;
    while (millis() - start < timeoutMs) {
        wl_status_t status = WiFi.status();
        result.status = static_cast<int>(status);

        if (status == WL_CONNECTED) {
            result.ok = true;
            result.localIp = WiFi.localIP().toString();
            result.gatewayIp = WiFi.gatewayIP().toString();
            result.rssi = WiFi.RSSI();

            Serial.printf("WiFi connected localIp=%s gateway=%s rssi=%d\n",
                          result.localIp.c_str(),
                          result.gatewayIp.c_str(),
                          result.rssi);
            return result;
        }

        if (millis() - lastLog >= 1000) {
            lastLog = millis();
            Serial.printf("WiFi status=%d %s\n", status, wifiStatusName(status));
        }

        delay(100);
        yield();
    }

    result.status = static_cast<int>(WiFi.status());
    result.error = "WiFi connect timeout";
    Serial.printf("%s status=%d %s\n",
                  result.error.c_str(),
                  result.status,
                  wifiStatusName(static_cast<wl_status_t>(result.status)));
    return result;
}
