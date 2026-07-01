#include "ble_scanner.h"

#include <algorithm>

static const uint16_t SYSTEMAIR_MANUFACTURER_ID = 2447;

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

static void addOrUpdateController(std::vector<ControllerInfo> &results, const ControllerInfo &item) {
    if (item.name.length() == 0 || item.address.length() == 0) return;

    for (auto &existing : results) {
        if (existing.address == item.address) {
            existing.name = item.name;
            existing.rssi = item.rssi;
            existing.addressType = item.addressType;
            existing.advertisedDevice = item.advertisedDevice;
            return;
        }
    }

    results.push_back(item);
}

void BleScanner::begin() {
    static bool initialized = false;
    if (initialized) return;

    NimBLEDevice::init("AC Tools Companion");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    initialized = true;
}

std::vector<ControllerInfo> BleScanner::scan(uint32_t seconds) {
    std::vector<ControllerInfo> results;

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(nullptr, false);
    scan->setActiveScan(false);
    scan->setInterval(100);
    scan->setWindow(80);

    Serial.println("Starting passive scan...");
    NimBLEScanResults scanResults = scan->getResults(seconds * 1000, false);

    for (int i = 0; i < scanResults.getCount(); i++) {
        const NimBLEAdvertisedDevice *device = scanResults.getDevice(i);
        if (!device) continue;

        std::string mfg = device->getManufacturerData();
        if (mfg.length() < 3) continue;

        uint16_t manufacturerId = (static_cast<uint8_t>(mfg[1]) << 8) | static_cast<uint8_t>(mfg[0]);
        if (manufacturerId != SYSTEMAIR_MANUFACTURER_ID) continue;

        ControllerInfo item;
        item.name = extractNamePiece(mfg.substr(2));
        item.address = device->getAddress().toString().c_str();
        item.addressType = device->getAddress().getType();
        item.rssi = device->getRSSI();
        item.connectable = device->isConnectable();
        item.scannable = device->isScannable();
        item.advertisedDevice = device;

        addOrUpdateController(results, item);

        Serial.printf("Found controller: %s | %s | type %u | %d dBm | adv=%s | conn=%s | scan=%s\n",
                      item.name.c_str(),
                      item.address.c_str(),
                      item.addressType,
                      item.rssi,
                      item.hasAdvertisedDevice() ? "yes" : "no",
                      item.connectable ? "yes" : "no",
                      item.scannable ? "yes" : "no");
    }

    std::sort(results.begin(), results.end(), [](const ControllerInfo &a, const ControllerInfo &b) {
        return a.rssi > b.rssi;
    });

    Serial.printf("Scan done. Returning %u controller(s)\n", static_cast<unsigned>(results.size()));
    return results;
}
