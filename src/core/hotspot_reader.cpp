#include "hotspot_reader.h"

#include <NimBLEDevice.h>

static const char *UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

static bool tryConnect(NimBLEClient *client, const String &address, uint8_t addressType) {
    NimBLEAddress bleAddress(std::string(address.c_str()), addressType);
    Serial.printf("Connecting BLE using type %u...\n", addressType);
    return client->connect(bleAddress);
}

HotspotCredentials HotspotReader::read(const ControllerInfo &controller) {
    HotspotCredentials result;

    Serial.println("=== HotspotReader BLE connect test ===");
    Serial.printf("Target: %s | %s | type %u\n",
                  controller.name.c_str(), controller.address.c_str(), controller.addressType);

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->stop();
    delay(300);

    NimBLEClient *client = NimBLEDevice::createClient();
    if (!client) {
        result.error = "Create client failed";
        Serial.println(result.error);
        return result;
    }

    client->setConnectTimeout(8);

    bool connected = tryConnect(client, controller.address, controller.addressType);

    if (!connected && controller.addressType != BLE_ADDR_RANDOM) {
        connected = tryConnect(client, controller.address, BLE_ADDR_RANDOM);
    }

    if (!connected && controller.addressType != BLE_ADDR_PUBLIC) {
        connected = tryConnect(client, controller.address, BLE_ADDR_PUBLIC);
    }

    if (!connected) {
        result.error = "BLE connect failed";
        Serial.println(result.error);
        NimBLEDevice::deleteClient(client);
        return result;
    }

    result.bleConnected = true;
    Serial.println("BLE connected");

    NimBLERemoteService *service = client->getService(UART_SERVICE_UUID);
    if (!service) {
        result.error = "UART service not found";
        Serial.println(result.error);
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    result.serviceFound = true;
    Serial.println("UART service found");

    NimBLERemoteCharacteristic *rxChar = service->getCharacteristic(RX_UUID);
    NimBLERemoteCharacteristic *txChar = service->getCharacteristic(TX_UUID);

    result.rxFound = rxChar != nullptr;
    result.txFound = txChar != nullptr;

    Serial.printf("RX characteristic: %s\n", result.rxFound ? "found" : "missing");
    Serial.printf("TX characteristic: %s\n", result.txFound ? "found" : "missing");

    result.ok = result.bleConnected && result.serviceFound && result.rxFound && result.txFound;

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    Serial.printf("BLE connect test done. ok=%d\n", result.ok);
    return result;
}
