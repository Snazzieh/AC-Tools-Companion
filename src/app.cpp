#include "app.h"
#include "core/controller_session.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;
static ControllerSession session;

static void drawHeader(const String &title) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(18, 20);
    tft.println(title);
}

static String signalText(int rssi) {
    if (rssi >= -60) return "GOOD";
    if (rssi >= -75) return "MID";
    return "LOW";
}

static void drawBoot() {
    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(22, 45);
    tft.println("AC Tools");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(22, 80);
    tft.println("Companion");

    tft.setTextSize(1);
    tft.setCursor(22, 135);
    tft.println("Standalone service tool");
}

static void drawScanning() {
    drawHeader("Scanning");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(20, 75);
    tft.println("Looking for Access controllers...");
}

static void drawResults(const std::vector<ControllerInfo> &controllers) {
    drawHeader("Controllers");
    tft.setTextSize(1);

    if (controllers.empty()) {
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(20, 80);
        tft.println("No controllers found");
        tft.setCursor(20, 110);
        tft.println("Move closer and reboot.");
        return;
    }

    int y = 65;
    for (size_t i = 0; i < controllers.size() && i < 6; i++) {
        const auto &c = controllers[i];

        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(15, y);
        tft.print(signalText(c.rssi));

        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setCursor(62, y);

        String safeName = c.name;
        if (safeName.length() > 18) safeName = safeName.substring(0, 18);
        tft.println(safeName);

        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(62, y + 14);
        tft.printf("%d dBm  T%u", c.rssi, c.addressType);

        y += 42;
    }
}

static void drawConnectResult(const BleConnectResult &result) {
    drawHeader("BLE Test");
    tft.setTextSize(1);

    tft.setTextColor(result.bleConnected ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(20, 70);
    tft.println(result.bleConnected ? "BLE connected" : "BLE failed");

    tft.setTextColor(result.serviceFound ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(20, 100);
    tft.println(result.serviceFound ? "UART service OK" : "UART service missing");

    tft.setTextColor(result.rxFound ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(20, 130);
    tft.println(result.rxFound ? "RX char OK" : "RX char missing");

    tft.setTextColor(result.txFound ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(20, 160);
    tft.println(result.txFound ? "TX char OK" : "TX char missing");

    if (result.error.length() > 0) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(20, 210);
        tft.println(result.error);
    }
}

static void drawHotspotResult(const HotspotCredentials &result) {
    drawHeader("Hotspot");
    tft.setTextSize(1);

    tft.setTextColor(result.ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(20, 70);
    tft.println(result.ok ? "Credentials OK" : "Hotspot failed");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(20, 105);
    String ssid = result.ssid;
    if (ssid.length() > 24) ssid = ssid.substring(0, 24);
    tft.print("SSID: ");
    tft.println(ssid);

    tft.setCursor(20, 130);
    tft.print("IP: ");
    tft.println(result.ip.length() > 0 ? result.ip : "192.168.10.1");

    tft.setCursor(20, 155);
    tft.printf("Password: %u chars", static_cast<unsigned>(result.password.length()));

    if (result.error.length() > 0) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(20, 210);
        tft.println(result.error);
    }
}

static void drawWifiResult(const WifiConnectResult &result) {
    drawHeader("WiFi");
    tft.setTextSize(1);

    tft.setTextColor(result.ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(20, 70);
    tft.println(result.ok ? "Hotspot connected" : "WiFi failed");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(20, 105);
    String ssid = result.ssid;
    if (ssid.length() > 24) ssid = ssid.substring(0, 24);
    tft.print("SSID: ");
    tft.println(ssid);

    tft.setCursor(20, 130);
    tft.print("Local: ");
    tft.println(result.localIp.length() > 0 ? result.localIp : "-");

    tft.setCursor(20, 155);
    tft.print("Gateway: ");
    tft.println(result.gatewayIp.length() > 0 ? result.gatewayIp : "-");

    tft.setCursor(20, 180);
    tft.printf("RSSI: %d dBm", result.rssi);

    if (result.error.length() > 0) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(20, 220);
        tft.println(result.error);
    }
}

static void drawHttpResult(const HttpProbeResult &result) {
    drawHeader("EXOsocket");
    tft.setTextSize(1);

    tft.setTextColor(result.ok ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.setCursor(20, 70);
    tft.println(result.ok ? "WebSocket OK" : "WebSocket failed");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(20, 105);
    tft.print("Host: ");
    tft.println(result.host);

    tft.setCursor(20, 130);
    tft.print("Path: ");
    tft.println(result.path.length() > 0 ? result.path : "-");

    tft.setCursor(20, 155);
    tft.printf("Status: %d", result.statusCode);

    if (result.error.length() > 0) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setCursor(20, 210);
        tft.println(result.error);
    }
}

void App::begin() {
    Serial.begin(115200);
    delay(300);

    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    tft.init();
    tft.setRotation(0);

    drawBoot();
    delay(1200);

    session.begin();

    drawScanning();
    auto controllers = session.scan(8);
    drawResults(controllers);

    if (!controllers.empty()) {
        delay(1500);
        drawHeader("Connecting");
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(20, 75);
        tft.println(controllers[0].name);
        tft.setCursor(20, 105);
        tft.println("Testing BLE link...");

        BleConnectResult result = session.connect(controllers[0]);
        drawConnectResult(result);

        if (result.ok) {
            delay(1500);
            drawHeader("Hotspot");
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setTextSize(1);
            tft.setCursor(20, 75);
            tft.println("Reading credentials...");

            HotspotCredentials credentials = session.readHotspot(controllers[0]);
            drawHotspotResult(credentials);

            if (credentials.ok) {
                delay(1500);
                drawHeader("WiFi");
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
                tft.setTextSize(1);
                tft.setCursor(20, 75);
                tft.println("Connecting hotspot...");

                WifiConnectResult wifi = session.connectWifi(credentials);
                drawWifiResult(wifi);

                if (wifi.ok) {
                    delay(1500);
                    drawHeader("EXOsocket");
                    tft.setTextColor(TFT_WHITE, TFT_BLACK);
                    tft.setTextSize(1);
                    tft.setCursor(20, 75);
                    tft.println("Testing WebSocket...");

                    HttpProbeResult http = session.probeHttp(wifi);
                    drawHttpResult(http);
                }
            }
        }
    }
}

void App::update() {
    delay(20);
}
