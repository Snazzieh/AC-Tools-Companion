#include "ble_connector.h"

static const char *UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

BleConnectResult BleConnector::testUart(const ControllerInfo &controller) {
    BleConnectResult result;

    Serial.println("=== BLE advertised-device connect test ===");
    Serial.printf("Target: %s | %s | type %u | adv=%s\n",
                  controller.name.c_str(),
                  controller.address.c_str(),
                  controller.addressType,
                  controller.hasAdvertisedDevice() ? "yes" : "no");

    if (!controller.hasAdvertisedDevice()) {
        result.error = "Missing advertisement";
        Serial.println(result.error);
        return result;
    }

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

    Serial.println("Connecting BLE using NimBLEAdvertisedDevice...");
    bool connected = client->connect(controller.advertisedDevice.get());
    if (!connected) {
        result.error = "BLE connect failed";
        Serial.printf("%s, lastError=%d\n", result.error.c_str(), client->getLastError());
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
    result.ok = result.bleConnected && result.serviceFound && result.rxFound && result.txFound;

    Serial.printf("RX characteristic: %s\n", result.rxFound ? "found" : "missing");
    Serial.printf("TX characteristic: %s\n", result.txFound ? "found" : "missing");

    client->disconnect();
    NimBLEDevice::deleteClient(client);

    Serial.printf("BLE UART test done. ok=%d\n", result.ok);
    return result;
}
