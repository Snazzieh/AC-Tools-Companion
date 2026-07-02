#include "touch_input.h"

#include <SPI.h>

namespace {
constexpr int TOUCH_CS = 33;
constexpr int TOUCH_IRQ = 36;
constexpr int TOUCH_CLK = 25;
constexpr int TOUCH_MISO = 39;
constexpr int TOUCH_MOSI = 32;

constexpr uint8_t CMD_READ_X = 0xD0;
constexpr uint8_t CMD_READ_Y = 0x90;
constexpr uint16_t RAW_X_MIN = 280;
constexpr uint16_t RAW_X_MAX = 3860;
constexpr uint16_t RAW_Y_MIN = 320;
constexpr uint16_t RAW_Y_MAX = 3860;
constexpr int16_t SCREEN_W = 320;
constexpr int16_t SCREEN_H = 240;

SPIClass touchSpi(VSPI);
}

void TouchInput::begin() {
    pinMode(TOUCH_CS, OUTPUT);
    digitalWrite(TOUCH_CS, HIGH);
    pinMode(TOUCH_IRQ, INPUT);
    touchSpi.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
}

uint16_t TouchInput::readAxis(uint8_t command) {
    touchSpi.transfer(command);
    uint16_t value = static_cast<uint16_t>(touchSpi.transfer(0x00)) << 8;
    value |= touchSpi.transfer(0x00);
    return value >> 3;
}

TouchPoint TouchInput::mapRaw(uint16_t rawX, uint16_t rawY) const {
    TouchPoint point;
    point.pressed = true;
    point.rawX = rawX;
    point.rawY = rawY;

    long mappedX = map(rawY, RAW_Y_MIN, RAW_Y_MAX, SCREEN_W - 1, 0);
    long mappedY = map(rawX, RAW_X_MIN, RAW_X_MAX, 0, SCREEN_H - 1);

    point.x = constrain(static_cast<int>(mappedX), 0, SCREEN_W - 1);
    point.y = constrain(static_cast<int>(mappedY), 0, SCREEN_H - 1);
    return point;
}

TouchPoint TouchInput::read() {
    TouchPoint point;
    if (digitalRead(TOUCH_IRQ) == HIGH) {
        return point;
    }

    constexpr uint8_t samples = 5;
    uint32_t sumX = 0;
    uint32_t sumY = 0;

    touchSpi.beginTransaction(SPISettings(2500000, MSBFIRST, SPI_MODE0));
    digitalWrite(TOUCH_CS, LOW);
    delayMicroseconds(20);

    for (uint8_t i = 0; i < samples; i++) {
        sumX += readAxis(CMD_READ_X);
        sumY += readAxis(CMD_READ_Y);
    }

    digitalWrite(TOUCH_CS, HIGH);
    touchSpi.endTransaction();

    uint16_t rawX = sumX / samples;
    uint16_t rawY = sumY / samples;
    if (rawX < 100 || rawY < 100) {
        return point;
    }

    return mapRaw(rawX, rawY);
}

bool TouchInput::wasTapped(uint16_t debounceMs) {
    TouchPoint point = read();
    if (!point.pressed) {
        return false;
    }

    uint32_t now = millis();
    if (now - lastTapMs < debounceMs) {
        return false;
    }

    lastTapMs = now;
    Serial.printf("Touch tap x=%d y=%d raw=(%u,%u)\n", point.x, point.y, point.rawX, point.rawY);
    return true;
}
