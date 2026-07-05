#pragma once

struct WifiCredential {
    const char *ssid;
    const char *password;
};

constexpr WifiCredential WIFI_NETWORKS[] = {
    {"Tullenet", "snazzy12"},
    {"Bambulab", "systemair12"},
    {"Skolesvinget_62", "snazzy12"},

};

constexpr int WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
constexpr const char *DEFAULT_WIFI_SSID = WIFI_NETWORKS[0].ssid;
constexpr const char *DEFAULT_WIFI_PASSWORD = WIFI_NETWORKS[0].password;
