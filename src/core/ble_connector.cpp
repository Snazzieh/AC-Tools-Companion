#include "ble_connector.h"

static const char *UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

static const char *nimbleErrorName(int error) {
    switch (error) {
        case 0: return "OK";
        case 1: return "BLE_HS_EAGAIN";
        case 2: return "BLE_HS_EALREADY";
        case 3: return "BLE_HS_EINVAL";
        case 4: return "BLE_HS_EMSGSIZE";
        case 5: return "BLE_HS_ENOENT";
        case 6: return "BLE_HS_ENOMEM";
        case 7: return "BLE_HS_ENOTCONN";
        case 8: return "BLE_HS_ENOTSUP";
        case 9: return "BLE_HS_EAPP";
        case 10: return "BLE_HS_EBADDATA";
        case 11: return "BLE_HS_EOS";
        case 12: return "BLE_HS_ECONTROLLER";
        case 13: return "BLE_HS_ETIMEOUT";
        case 14: return "BLE_HS_EDONE";
        case 15: return "BLE_HS_EBUSY";
        case 16: return "BLE_HS_EREJECT";
        case 17: return "BLE_HS_EUNKNOWN";
        case 18: return "BLE_HS_EROLE";
        case 19: return "BLE_HS_ETIMEOUT_HCI";
        case 20: return "BLE_HS_ENOMEM_EVT";
        case 21: return "BLE_HS_ENOADDR";
        case 22: return "BLE_HS_ENOTSYNCED";
        case 23: return "BLE_HS_EAUTHEN";
        case 24: return "BLE_HS_EAUTHOR";
        case 25: return "BLE_HS_EENCRYPT";
        case 26: return "BLE_HS_EENCRYPT_KEY_SZ";
        case 27: return "BLE_HS_ESTORE_CAP";
        case 28: return "BLE_HS_ESTORE_FAIL";
        case 29: return "BLE_HS_EPREEMPTED";
        case 30: return "BLE_HS_EDISABLED";
        case 31: return "BLE_HS_ESTALLED";
        default: return "BLE_HS_UNKNOWN";
    }
}

BleConnectResult BleConnector::testUart(const ControllerInfo &controller) {
    BleConnectResult result;

    Serial.println("=== BLE advertised-device connect test ===");
    Serial.printf("Target: %s | %s | type %u | adv=%s\n",
                  controller.name.c_str(),
                  controller.address.c_str(),
                  controller.addressType,
                  controller.hasAdvertisedDevice() ? "yes" : "no");
    Serial.printf("Advertisement: connectable=%s | scannable=%s\n",
                  controller.connectable ? "yes" : "no",
                  controller.scannable ? "yes" : "no");

    if (!controller.hasAdvertisedDevice()) {
        result.error = "Missing advertisement";
        Serial.println(result.error);
        return result;
    }

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(nullptr, false);
    scan->stop();
    scan->setActiveScan(false);
    Serial.printf("Scan stopped before connect: scanning=%s\n", scan->isScanning() ? "yes" : "no");
    delay(750);

    NimBLEClient *client = NimBLEDevice::createClient();
    if (!client) {
        result.error = "Create client failed";
        Serial.println(result.error);
        return result;
    }

    client->setConnectTimeout(30000);
    client->setConnectionParams(24, 40, 0, 400, 80, 80);

    Serial.println("Connecting BLE using NimBLEAdvertisedDevice...");
    bool connected = client->connect(controller.advertisedDevice, true, false, false);
    int lastError = client->getLastError();
    Serial.printf("connect() returned %s | lastError=%d %s\n",
                  connected ? "true" : "false",
                  lastError,
                  nimbleErrorName(lastError));
    if (!connected) {
        result.error = "BLE connect failed";
        Serial.printf("%s, lastError=%d %s\n",
                      result.error.c_str(),
                      lastError,
                      nimbleErrorName(lastError));
        NimBLEDevice::deleteClient(client);
        return result;
    }

    result.bleConnected = true;
    Serial.println("BLE connected");
    Serial.println("Pairing/bonding: not requested before GATT discovery");

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
