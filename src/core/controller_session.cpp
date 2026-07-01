#include "controller_session.h"

void ControllerSession::begin() {
    scanner.begin();
}

std::vector<ControllerInfo> ControllerSession::scan(uint32_t seconds) {
    return scanner.scan(seconds);
}

BleConnectResult ControllerSession::connect(const ControllerInfo &controller) {
    return connector.testUart(controller);
}
