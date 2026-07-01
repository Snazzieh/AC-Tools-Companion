#include "app.h"
#include "core/ble_scanner.h"
#include "core/hotspot_reader.h"

#include <Arduino.h>
#include <TFT_eSPI.h>

static TFT_eSPI tft;
static BleScanner bleScanner;
static HotspotReader hotspotReader;

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
        if (safeName.length() > 18) {
            safeName = safeName.substring(0, 18);
        }

        tft.println(safeName);

        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(62, y + 14);
        tft.printf("%d dBm", c.rssi);

        y += 42;
    }
}

void App::begin() {
    Serial.begin(115200);

    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    tft.init();
    tft.setRotation(0);

    drawBoot();
    delay(1200);

    bleScanner.begin();

    drawScanning();

    auto controllers = bleScanner.scan(8);

    drawResults(controllers);

    if (!controllers.empty()) {
        delay(1500);
        hotspotReader.read(controllers[0].address);
    }
}

void App::update() {
}