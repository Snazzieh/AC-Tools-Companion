#include "http_probe.h"

#include "exosocket_client.h"

HttpProbeResult HttpProbe::probe(const WifiConnectResult &wifi) {
    HttpProbeResult result;
    String host = wifi.gatewayIp.length() > 0 ? wifi.gatewayIp : "192.168.254.1";

    Serial.println("=== EXOsocket connect test ===");
    Serial.printf("WiFi ok=%s host=%s\n", wifi.ok ? "yes" : "no", host.c_str());

    if (!wifi.ok) {
        result.host = host;
        result.error = "WiFi not connected";
        Serial.println(result.error);
        return result;
    }

    ExoSocketClient client;
    ExoSocketConnectResult exo = client.connectAndVersionOffer(host);

    result.ok = exo.ok;
    result.host = exo.host;
    result.path = exo.path;
    result.statusCode = exo.statusCode;
    result.statusLine = exo.statusLine;
    result.protocol = exo.protocol;
    result.accept = exo.accept;
    result.sample = exo.versionReply;
    result.error = exo.error;

    Serial.printf("EXO result ok=%s status=%d line=%s protocol=%s accept=%s versionAck=%s reply=%s\n",
                  result.ok ? "yes" : "no",
                  result.statusCode,
                  result.statusLine.c_str(),
                  result.protocol.c_str(),
                  result.accept.c_str(),
                  exo.versionAck ? "yes" : "no",
                  result.sample.c_str());

    return result;
}
