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

HotspotCredentials ControllerSession::readHotspot(const ControllerInfo &controller) {
    return hotspotReader.read(controller);
}

WifiConnectResult ControllerSession::connectWifi(const HotspotCredentials &credentials) {
    return wifiConnector.connect(credentials);
}

HttpProbeResult ControllerSession::probeHttp(const WifiConnectResult &wifi) {
    return httpProbe.probe(wifi);
}

ExoSocketGatewayResult ControllerSession::startGateway(const WifiConnectResult &wifi) {
    return gateway.begin(wifi);
}

void ControllerSession::updateGateway() {
    gateway.update();
}
