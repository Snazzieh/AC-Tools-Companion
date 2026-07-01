#include "ble_scanner.h"

#include <NimBLEDevice.h>
#include <algorithm>

static const uint16_t SYSTEMAIR_MANUFACTURER_ID = 2447;
static std::vector<ControllerInfo> scanResults;

static String cleanText(String text) {
    text.replace("\0", "");
    text.trim();
    return text;
}

static String extractNamePiece(const std::string &data) {
    String best = "";
    String current = "";

    for (uint8_t b : data) {
        if (b >= 32 && b <= 126) {
            current += static_cast<char>(b);
        } else {
            if (current.length() >= 2 && current.length() > best.length()) {
                best = current;
            }
            current = "";
        }
    }

    if (current.length() >= 2 && current.length() > best.length()) {
        best = current;
    }

    return cleanText(best);
}

static void addOrUpdateController(const ControllerInfo &item) {
    if (item.name.length() == 0 || item.address.length() == 0) return;

    for (auto &existing : scanResults) {
        if (existing.address == item.address) {
            existing.name = item.name;
            existing.rssi = item.rssi;
            existing.addressType = item.addressType;
            return;
        }
    }

    scanResults.push_back(item);
}

class ScannerCallbacks : public NimBLEScanCallbacks {
public:
    void onResult(const NimBLEAdvertisedDevice *device) override {
        std::string mfg = device->getManufacturerData();
        if (mfg.length() < 3) return;

        uint16_t manufacturerId = (static_cast<uint8_t>(mfg[1]) << 8) | static_cast<uint8_t>(mfg[0]);
        if (manufacturerId != SYSTEMAIR_MANUFACTURER_ID) return;

        ControllerInfo item;
        item.name = extractNamePiece(mfg.substr(2));
        item.address = device->getAddress().toString().c_str();
        item.addressType = device->getAddress().getType();
        item.rssi = device->getRSSI();

        addOrUpdateController(item);

        Serial.printf("Found controller: %s | %s | type %u | %d dBm\n",
                      item.name.c_str(), item.address.c_str(), item.addressType, item.rssi);
    }
};

static ScannerCallbacks scannerCallbacks;

void BleScanner::begin() {
    static bool initialized = false;
    if (initialized) return;

    NimBLEDevice::init("AC Tools Companion");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    initialized = true;
}

std::vector<ControllerInfo> BleScanner::scan(uint32_t seconds) {
    scanResults.clear();

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&scannerCallbacks, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);

    Serial.println("Starting scan...");
    scan->start(seconds * 1000, false, true);

    uint32_t waitStart = millis();
    while (scanResults.empty() && millis() - waitStart < 1500) {
        delay(50);
        yield();
    }

    std::sort(scanResults.begin(), scanResults.end(), [](const ControllerInfo &a, const ControllerInfo &b) {
        return a.rssi > b.rssi;
    });

    Serial.printf("Scan done. Returning %u controller(s)\n", static_cast<unsigned>(scanResults.size()));
    return scanResults;
}
