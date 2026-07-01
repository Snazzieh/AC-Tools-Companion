#include "hotspot_reader.h"

HotspotCredentials HotspotReader::read(const ControllerInfo &controller) {
    HotspotCredentials result;
    result.error = "Hotspot auth not implemented in v0.3 connect test";
    Serial.printf("HotspotReader deferred for %s\n", controller.name.c_str());
    return result;
}
