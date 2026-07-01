#include "hotspot_reader.h"

#include <NimBLEDevice.h>

static const char *UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

HotspotCredentials HotspotReader::read(const String &address) {
    HotspotCredentials result;

    Serial.println("=== HotspotReader test ===");
    Serial.printf("Target address: %s\n", address.c_str());

    NimBLEClient *client = NimBLEDevice::createClient();

    if (!client) {
        Serial.println("Failed to create BLE client");
        return result;
    }

    NimBLEAddress bleAddress(std::string(address.c_str()), BLE_ADDR_RANDOM);

    Serial.println("Connecting BLE...");

    bool connected = client->connect(bleAddress);

    if (!connected) {
        Serial.println("BLE connect failed");
        NimBLEDevice::deleteClient(client);
        return result;
    }

    Serial.println("BLE connected");

    NimBLERemoteService *service = client->getService(UART_SERVICE_UUID);

    if (!service) {
        Serial.println("UART service not found");
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    Serial.println("UART service found");

    NimBLERemoteCharacteristic *rxChar = service->getCharacteristic(RX_UUID);
    NimBLERemoteCharacteristic *txChar = service->getCharacteristic(TX_UUID);

    if (!rxChar) {
        Serial.println("RX characteristic not found");
    } else {
        Serial.println("RX characteristic found");
    }

    if (!txChar) {
        Serial.println("TX characteristic not found");
    } else {
        Serial.println("TX characteristic found");
    }

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    Serial.println("BLE test done");

    return result;
}