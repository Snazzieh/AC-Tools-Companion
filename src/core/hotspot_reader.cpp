#include "hotspot_reader.h"

#include <NimBLEDevice.h>
#include <mbedtls/sha256.h>
#include <vector>

static const char *UART_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char *RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
static const char *TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
static const char *SECRET = "Can I haz cheeseburger for lunch?";

static const uint8_t SLIP_END = 0xC0;
static const uint8_t SLIP_ESC = 0xDB;
static const uint8_t SLIP_ESC_END = 0xDC;
static const uint8_t SLIP_ESC_ESC = 0xDD;

static const uint8_t DATA_AUTH = 3;
static const uint8_t DATA_HOTSPOT = 4;

struct ProtocolFrame {
    uint8_t type = 0;
    uint8_t frameId = 0;
    std::vector<uint8_t> payload;
};

struct SlipReceiver {
    std::vector<uint8_t> current;
    std::vector<uint8_t> frame;
    bool frameReady = false;
    bool escaped = false;

    void reset() {
        current.clear();
        frame.clear();
        frameReady = false;
        escaped = false;
    }

    void accept(const uint8_t *data, size_t length) {
        for (size_t i = 0; i < length; i++) {
            uint8_t b = data[i];

            if (b == SLIP_END) {
                if (!current.empty() && !frameReady) {
                    frame = current;
                    frameReady = true;
                }
                current.clear();
                escaped = false;
                continue;
            }

            if (b == SLIP_ESC) {
                escaped = true;
                continue;
            }

            if (escaped) {
                if (b == SLIP_ESC_END) {
                    current.push_back(SLIP_END);
                } else if (b == SLIP_ESC_ESC) {
                    current.push_back(SLIP_ESC);
                } else {
                    current.push_back(b);
                }
                escaped = false;
                continue;
            }

            current.push_back(b);
        }
    }
};

static std::vector<uint8_t> slipEncode(uint8_t type, uint8_t frameId, const std::vector<uint8_t> &payload) {
    std::vector<uint8_t> out;
    out.reserve(payload.size() + 5);
    out.push_back(SLIP_END);

    auto append = [&out](uint8_t b) {
        if (b == SLIP_END) {
            out.push_back(SLIP_ESC);
            out.push_back(SLIP_ESC_END);
        } else if (b == SLIP_ESC) {
            out.push_back(SLIP_ESC);
            out.push_back(SLIP_ESC_ESC);
        } else {
            out.push_back(b);
        }
    };

    append(type);
    append(frameId);
    for (uint8_t b : payload) append(b);
    out.push_back(SLIP_END);
    return out;
}

static bool parseProtocolFrame(const std::vector<uint8_t> &decoded, ProtocolFrame &frame) {
    if (decoded.size() < 2) return false;

    frame.type = decoded[0] & 0x3F;
    frame.frameId = decoded[1];
    frame.payload.assign(decoded.begin() + 2, decoded.end());
    return true;
}

static bool writeSlipFrame(NimBLERemoteCharacteristic *rxChar, uint8_t type, uint8_t frameId, const std::vector<uint8_t> &payload) {
    std::vector<uint8_t> frame = slipEncode(type, frameId, payload);
    const size_t chunkSize = 20;

    Serial.printf("Writing SLIP frame type=%u frameId=%u payload=%u encoded=%u\n",
                  type,
                  frameId,
                  static_cast<unsigned>(payload.size()),
                  static_cast<unsigned>(frame.size()));

    for (size_t offset = 0; offset < frame.size(); offset += chunkSize) {
        size_t length = min(chunkSize, frame.size() - offset);
        if (!rxChar->writeValue(frame.data() + offset, length, false)) {
            Serial.println("RX write failed");
            return false;
        }
        delay(20);
    }

    return true;
}

static bool waitForFrame(SlipReceiver &receiver, ProtocolFrame &frame, uint32_t timeoutMs) {
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (receiver.frameReady) {
            std::vector<uint8_t> decoded = receiver.frame;
            receiver.frame.clear();
            receiver.frameReady = false;
            if (!parseProtocolFrame(decoded, frame)) {
                Serial.printf("Received invalid SLIP frame len=%u\n", static_cast<unsigned>(decoded.size()));
                continue;
            }
            Serial.printf("Received SLIP frame len=%u type=%u frameId=%u payload=%u\n",
                          static_cast<unsigned>(decoded.size()),
                          frame.type,
                          frame.frameId,
                          static_cast<unsigned>(frame.payload.size()));
            return true;
        }
        delay(20);
        yield();
    }
    return false;
}

static void sha256SecretChallenge(const std::vector<uint8_t> &challenge, std::vector<uint8_t> &digest) {
    digest.assign(32, 0);

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);
    mbedtls_sha256_update_ret(&ctx, reinterpret_cast<const unsigned char *>(SECRET), strlen(SECRET));
    mbedtls_sha256_update_ret(&ctx, challenge.data(), challenge.size());
    mbedtls_sha256_finish_ret(&ctx, digest.data());
    mbedtls_sha256_free(&ctx);
}

static bool readMsgpackString(const std::vector<uint8_t> &data, size_t &pos, String &value) {
    if (pos >= data.size()) return false;

    uint8_t marker = data[pos++];
    size_t length = 0;

    if ((marker & 0xE0) == 0xA0) {
        length = marker & 0x1F;
    } else if (marker == 0xD9) {
        if (pos + 1 > data.size()) return false;
        length = data[pos++];
    } else if (marker == 0xDA) {
        if (pos + 2 > data.size()) return false;
        length = (static_cast<size_t>(data[pos]) << 8) | data[pos + 1];
        pos += 2;
    } else if (marker == 0xDB) {
        if (pos + 4 > data.size()) return false;
        length = (static_cast<size_t>(data[pos]) << 24) |
                 (static_cast<size_t>(data[pos + 1]) << 16) |
                 (static_cast<size_t>(data[pos + 2]) << 8) |
                 data[pos + 3];
        pos += 4;
    } else {
        return false;
    }

    if (pos + length > data.size()) return false;
    value = "";
    for (size_t i = 0; i < length; i++) {
        value += static_cast<char>(data[pos + i]);
    }
    pos += length;
    return true;
}

static bool skipMsgpackValue(const std::vector<uint8_t> &data, size_t &pos) {
    String ignored;
    if (pos >= data.size()) return false;

    uint8_t marker = data[pos];
    if ((marker & 0xE0) == 0xA0 || marker == 0xD9 || marker == 0xDA || marker == 0xDB) {
        return readMsgpackString(data, pos, ignored);
    }

    pos++;
    if (marker <= 0x7F || marker >= 0xE0 || marker == 0xC0 || marker == 0xC2 || marker == 0xC3) return true;
    if (marker == 0xCC || marker == 0xD0) pos += 1;
    else if (marker == 0xCD || marker == 0xD1) pos += 2;
    else if (marker == 0xCE || marker == 0xD2 || marker == 0xCA) pos += 4;
    else if (marker == 0xCF || marker == 0xD3 || marker == 0xCB) pos += 8;
    else return false;

    return pos <= data.size();
}

static bool parseHotspotMap(const std::vector<uint8_t> &payload, HotspotCredentials &credentials) {
    size_t pos = 0;
    if (payload.empty()) return false;

    uint8_t marker = payload[pos++];
    size_t count = 0;

    if ((marker & 0xF0) == 0x80) {
        count = marker & 0x0F;
    } else if (marker == 0xDE) {
        if (pos + 2 > payload.size()) return false;
        count = (static_cast<size_t>(payload[pos]) << 8) | payload[pos + 1];
        pos += 2;
    } else if (marker == 0xDF) {
        if (pos + 4 > payload.size()) return false;
        count = (static_cast<size_t>(payload[pos]) << 24) |
                (static_cast<size_t>(payload[pos + 1]) << 16) |
                (static_cast<size_t>(payload[pos + 2]) << 8) |
                payload[pos + 3];
        pos += 4;
    } else {
        return false;
    }

    for (size_t i = 0; i < count; i++) {
        String key;
        String value;
        if (!readMsgpackString(payload, pos, key)) return false;

        size_t valuePos = pos;
        if (readMsgpackString(payload, pos, value)) {
            if (key == "SSID") credentials.ssid = value;
            else if (key == "Password") credentials.password = value;
            else if (key == "ip") credentials.ip = value;
        } else {
            pos = valuePos;
            if (!skipMsgpackValue(payload, pos)) return false;
        }
    }

    if (credentials.ip.length() == 0) credentials.ip = "192.168.10.1";
    return credentials.ssid.length() > 0 && credentials.password.length() > 0;
}

static bool requestHotspot(NimBLERemoteCharacteristic *rxChar, SlipReceiver &receiver, uint8_t frameId, HotspotCredentials &result) {
    receiver.reset();
    if (!writeSlipFrame(rxChar, DATA_HOTSPOT, frameId, std::vector<uint8_t>{0x00})) {
        result.error = "Hotspot request write failed";
        return false;
    }

    ProtocolFrame frame;
    if (!waitForFrame(receiver, frame, 10000)) {
        result.error = "Hotspot response timeout";
        Serial.println(result.error);
        return false;
    }

    if (frame.type != DATA_HOTSPOT) {
        result.error = "Invalid hotspot response";
        Serial.printf("%s type=%u frameId=%u payload=%u\n",
                      result.error.c_str(),
                      frame.type,
                      frame.frameId,
                      static_cast<unsigned>(frame.payload.size()));
        return false;
    }

    if (!parseHotspotMap(frame.payload, result)) {
        result.error = "Hotspot parse failed";
        Serial.println(result.error);
        return false;
    }

    result.ok = true;
    Serial.printf("Hotspot OK SSID=%s ip=%s passwordLen=%u\n",
                  result.ssid.c_str(),
                  result.ip.c_str(),
                  static_cast<unsigned>(result.password.length()));
    return true;
}

HotspotCredentials HotspotReader::read(const ControllerInfo &controller) {
    HotspotCredentials result;

    Serial.println("=== HotspotReader auth test ===");
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
    scan->setScanCallbacks(nullptr, false);
    scan->stop();
    scan->setActiveScan(false);
    Serial.printf("Scan stopped before hotspot read: scanning=%s\n", scan->isScanning() ? "yes" : "no");
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
    Serial.printf("connect() returned %s | lastError=%d\n", connected ? "true" : "false", lastError);
    if (!connected) {
        result.error = "BLE connect failed";
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

    if (!rxChar || !txChar) {
        result.error = "UART characteristics missing";
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    SlipReceiver receiver;
    receiver.current.reserve(256);
    receiver.frame.reserve(256);

    bool subscribed = txChar->subscribe(true, [&receiver](NimBLERemoteCharacteristic *, uint8_t *data, size_t length, bool) {
        receiver.accept(data, length);
    }, true);

    Serial.printf("TX subscribe: %s\n", subscribed ? "ok" : "failed");
    if (!subscribed) {
        result.error = "TX subscribe failed";
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    uint8_t nextFrameId = 1;

    receiver.reset();
    if (!writeSlipFrame(rxChar, DATA_AUTH, nextFrameId++, std::vector<uint8_t>{0x00})) {
        result.error = "Auth init write failed";
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    ProtocolFrame frame;
    if (!waitForFrame(receiver, frame, 10000)) {
        result.error = "Auth challenge timeout";
        Serial.println(result.error);
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    if (frame.type != DATA_AUTH) {
        result.error = "Invalid auth challenge";
        Serial.printf("%s type=%u frameId=%u payload=%u\n",
                      result.error.c_str(),
                      frame.type,
                      frame.frameId,
                      static_cast<unsigned>(frame.payload.size()));
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    if (frame.payload.size() == 1 && frame.payload[0] == 0x01) {
        Serial.println("Auth OK");
        if (!requestHotspot(rxChar, receiver, nextFrameId++, result)) {
            client->disconnect();
            NimBLEDevice::deleteClient(client);
            return result;
        }

        txChar->unsubscribe(false);
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    if (frame.payload.size() != 32) {
        result.error = "Unknown auth payload";
        Serial.printf("%s size=%u\n",
                      result.error.c_str(),
                      static_cast<unsigned>(frame.payload.size()));
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    Serial.printf("Auth challenge bytes=%u\n", static_cast<unsigned>(frame.payload.size()));

    std::vector<uint8_t> digest;
    sha256SecretChallenge(frame.payload, digest);

    receiver.reset();
    if (!writeSlipFrame(rxChar, DATA_AUTH, nextFrameId++, digest)) {
        result.error = "Auth response write failed";
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    if (!waitForFrame(receiver, frame, 10000)) {
        result.error = "Auth OK timeout";
        Serial.println(result.error);
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    if (frame.type != DATA_AUTH || frame.payload.size() != 1 || frame.payload[0] != 0x01) {
        result.error = "Auth rejected";
        Serial.printf("%s type=%u frameId=%u payload=%u payload0=%u\n",
                      result.error.c_str(),
                      frame.type,
                      frame.frameId,
                      static_cast<unsigned>(frame.payload.size()),
                      frame.payload.empty() ? 255 : frame.payload[0]);
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    Serial.println("Auth OK");

    if (!requestHotspot(rxChar, receiver, nextFrameId++, result)) {
        client->disconnect();
        NimBLEDevice::deleteClient(client);
        return result;
    }

    txChar->unsubscribe(false);
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    return result;
}
