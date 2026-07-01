#include "ble_scanner.h"
#include <NimBLEDevice.h>

static const uint16_t SYSTEMAIR_MANUFACTURER_ID = 2447;
static std::vector<ControllerInfo> foundControllers;

static String cleanText(String text) {
    text.replace("\0", "");
    text.trim();
    return text;
}

static String extractNamePiece(const std::string &data) {
    String best = "";
    String current = "";

    for (uint8_t b : data) {
        if (b >= 32 && b <= 126) current += (char)b;
        else {
            if (current.length() >= 2 && current.length() > best.length()) best = current;
            current = "";
        }
    }

    if (current.length() >= 2 && current.length() > best.length()) best = current;
    return cleanText(best);
}

class ScanCallbacks : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice *device) override {
        std::string mfg = device->getManufacturerData();
        if (mfg.length() < 3) return;

        uint16_t manufacturerId = ((uint8_t)mfg[1] << 8) | (uint8_t)mfg[0];
        if (manufacturerId != SYSTEMAIR_MANUFACTURER_ID) return;

        String name = extractNamePiece(mfg.substr(2));
        if (name.length() == 0) return;

        String address = device->getAddress().toString().c_str();
        int rssi = device->getRSSI();

        if (foundControllers.empty()) {
            foundControllers.push_back({name, address, rssi});
        } else {
            foundControllers[0].name = name;
            foundControllers[0].address = address;
            foundControllers[0].rssi = rssi;
        }

        Serial.printf("Found controller: %s | %s | %d dBm\n",
                      name.c_str(), address.c_str(), rssi);
    }
};

static ScanCallbacks scanCallbacks;

void BleScanner::begin() {
    NimBLEDevice::init("AC Tools Companion");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
}

std::vector<ControllerInfo> BleScanner::scan(uint32_t seconds) {
    foundControllers.clear();

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scanCallbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);

    Serial.println("Starting scan...");
    scan->start(seconds * 1000, false, true);

    uint32_t waitStart = millis();
    while (foundControllers.empty() && millis() - waitStart < 2000) {
        delay(50);
        yield();
    }

    Serial.printf("Scan stopped. Returning %d controllers\n", foundControllers.size());

    std::vector<ControllerInfo> result = foundControllers;
    return result;
}