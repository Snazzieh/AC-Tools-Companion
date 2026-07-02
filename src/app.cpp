#include "app.h"
#include "core/controller_session.h"
#include "ui/touch_input.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <vector>

namespace {
TFT_eSPI tft;
TouchInput touch;
ControllerSession session;

constexpr uint16_t visible(uint16_t color) {
    return static_cast<uint16_t>(~color);
}

constexpr uint16_t BG = visible(TFT_BLACK);
constexpr uint16_t SURFACE = visible(0x1082);
constexpr uint16_t PANEL = visible(TFT_BLACK);
constexpr uint16_t PANEL_2 = visible(0x2965);
constexpr uint16_t TEXT = visible(TFT_WHITE);
constexpr uint16_t MUTED = visible(0x9CF3);
constexpr uint16_t ACCENT = 0x245F;
constexpr uint16_t ACCENT_DARK = 0x21F5;
constexpr uint16_t OK = visible(0x47E9);
constexpr uint16_t WARN = visible(0xFDA0);
constexpr uint16_t FAIL = visible(0xF986);

struct ButtonRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

const ButtonRect primaryButton{28, 198, 264, 35};

enum class ScreenMode {
    Home,
    Running,
    Done
};

ScreenMode screenMode = ScreenMode::Home;
bool flowRequested = false;
bool flowComplete = false;
String finalTitle;
String finalDetail;
bool finalOk = false;

bool inRect(const TouchPoint &point, const ButtonRect &rect) {
    return point.pressed && point.x >= rect.x && point.x < rect.x + rect.w && point.y >= rect.y && point.y < rect.y + rect.h;
}

void drawTextFit(const String &text, int16_t x, int16_t y, uint16_t color, uint8_t size, uint16_t bg = BG) {
    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(text);
}

void drawCenteredText(const String &text, int16_t x, int16_t y, int16_t w, uint16_t color, uint8_t size, uint16_t bg) {
    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    int16_t textW = text.length() * 6 * size;
    int16_t cursorX = x + (w - textW) / 2;
    tft.setCursor(cursorX < x + 4 ? x + 4 : cursorX, y);
    tft.print(text);
}

void drawPanel(int16_t x, int16_t y, int16_t w, int16_t h) {
    tft.fillRoundRect(x, y, w, h, 7, SURFACE);
    tft.drawRoundRect(x, y, w, h, 7, PANEL_2);
}

void drawButton(const ButtonRect &rect, const String &label, uint16_t fill, uint16_t outline = TEXT) {
    tft.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 7, fill);
    tft.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 7, outline);
    drawCenteredText(label, rect.x, rect.y + 10, rect.w, TEXT, 2, fill);
}

void drawShell(const String &title, const String &subtitle = "") {
    tft.fillScreen(BG);
    tft.fillRect(0, 0, 320, 34, PANEL);
    tft.drawFastVLine(0, 0, 240, PANEL_2);
    tft.fillRect(0, 34, 320, 1, PANEL_2);
    tft.fillRoundRect(9, 8, 18, 18, 3, ACCENT);
    drawTextFit("AC", 13, 14, TEXT, 1, ACCENT);
    drawTextFit("AC Tools", 36, 10, TEXT, 1, PANEL);
    drawTextFit("Companion", 238, 10, MUTED, 1, PANEL);
    drawTextFit(title, 14, 52, TEXT, 2);
    if (subtitle.length() > 0) {
        drawTextFit(subtitle, 16, 80, MUTED, 1);
    }
}

void drawStatusRow(int16_t y, const String &label, const String &value, uint16_t color) {
    tft.fillRoundRect(14, y, 292, 25, 5, SURFACE);
    tft.drawFastVLine(132, y + 5, 15, PANEL_2);
    drawTextFit(label, 24, y + 8, MUTED, 1, SURFACE);
    drawTextFit(value, 146, y + 8, color, 1, SURFACE);
}

String signalText(int rssi) {
    if (rssi >= -60) return "GOOD";
    if (rssi >= -75) return "MID";
    return "LOW";
}

String shortText(String text, uint8_t maxLen) {
    if (text.length() <= maxLen) return text;
    return text.substring(0, maxLen - 1) + "~";
}

void drawHome() {
    screenMode = ScreenMode::Home;
    flowRequested = false;
    flowComplete = false;

    drawShell("Controller Connect", "Scan nearby Systemair Access controllers.");
    drawPanel(14, 96, 292, 52);
    drawTextFit("Professional Service Tools", 26, 111, ACCENT, 1, SURFACE);
    drawTextFit("BLE  /  Hotspot  /  WiFi  /  EXOsocket", 26, 132, MUTED, 1, SURFACE);
    drawButton(primaryButton, "Open", ACCENT_DARK, PANEL_2);
}

void drawProgress(const String &title, const String &line1, const String &line2 = "") {
    screenMode = ScreenMode::Running;
    drawShell(title, line1);
    if (line2.length() > 0) {
        drawTextFit(line2, 16, 98, MUTED, 1);
    }
    drawPanel(32, 143, 256, 42);
    tft.fillRoundRect(50, 159, 220, 8, 4, PANEL_2);
    tft.fillRoundRect(50, 159, 92, 8, 4, ACCENT);
    drawCenteredText("Working", 32, 199, 256, MUTED, 1, BG);
}

void drawControllerList(const std::vector<ControllerInfo> &controllers) {
    drawShell("Controllers", controllers.empty() ? "No controller found." : "Best signal selected automatically.");

    if (controllers.empty()) {
        drawStatusRow(108, "Result", "No controllers", FAIL);
        return;
    }

    int16_t y = 92;
    for (size_t i = 0; i < controllers.size() && i < 3; i++) {
        const auto &c = controllers[i];
        drawPanel(14, y, 292, 34);
        drawTextFit(shortText(c.name, 22), 24, y + 7, i == 0 ? OK : TEXT, 1, SURFACE);
        String detail = String(c.rssi) + " dBm  " + signalText(c.rssi);
        drawTextFit(detail, 198, y + 7, MUTED, 1, SURFACE);
        y += 42;
    }
}

void drawConnectResult(const BleConnectResult &result) {
    drawShell("BLE UART", result.ok ? "BLE connected and UART found." : "BLE test failed.");
    drawStatusRow(92, "BLE", result.bleConnected ? "connected" : "failed", result.bleConnected ? OK : FAIL);
    drawStatusRow(122, "Service", result.serviceFound ? "UART OK" : "missing", result.serviceFound ? OK : FAIL);
    drawStatusRow(152, "RX/TX", result.rxFound && result.txFound ? "OK" : "missing", result.rxFound && result.txFound ? OK : FAIL);
    if (result.error.length() > 0) drawTextFit(shortText(result.error, 36), 18, 198, FAIL, 1);
}

void drawHotspotResult(const HotspotCredentials &result) {
    drawShell("Hotspot", result.ok ? "Credentials received." : "Hotspot auth failed.");
    drawStatusRow(92, "Auth", result.ok ? "OK" : "failed", result.ok ? OK : FAIL);
    drawStatusRow(122, "SSID", shortText(result.ssid, 20), result.ok ? TEXT : MUTED);
    drawStatusRow(152, "IP", result.ip.length() > 0 ? result.ip : "192.168.10.1", TEXT);
    drawStatusRow(182, "Password", String(result.password.length()) + " chars", MUTED);
}

void drawWifiResult(const WifiConnectResult &result) {
    drawShell("WiFi", result.ok ? "Connected to controller hotspot." : "WiFi failed.");
    drawStatusRow(92, "Hotspot", result.ok ? "connected" : "failed", result.ok ? OK : FAIL);
    drawStatusRow(122, "Local", result.localIp.length() > 0 ? result.localIp : "-", TEXT);
    drawStatusRow(152, "Gateway", result.gatewayIp.length() > 0 ? result.gatewayIp : "-", TEXT);
    drawStatusRow(182, "RSSI", String(result.rssi) + " dBm", result.rssi > -65 ? OK : WARN);
}

void drawHttpResult(const HttpProbeResult &result) {
    finalOk = result.ok;
    finalTitle = result.ok ? "EXOsocket OK" : "EXOsocket probe done";
    finalDetail = result.path.length() > 0 ? result.path : "No accepted WebSocket path yet";

    screenMode = ScreenMode::Done;
    flowComplete = true;
    drawShell(finalTitle, finalDetail);
    drawStatusRow(92, "Host", result.host.length() > 0 ? result.host : "-", TEXT);
    drawStatusRow(122, "Status", String(result.statusCode), result.ok ? OK : WARN);
    drawStatusRow(152, "Path", result.path.length() > 0 ? shortText(result.path, 22) : "-", TEXT);
    drawButton(primaryButton, "RESTART", result.ok ? OK : ACCENT, TEXT);
}

void drawFinalFailure(const String &title, const String &detail) {
    finalOk = false;
    finalTitle = title;
    finalDetail = detail;
    screenMode = ScreenMode::Done;
    flowComplete = true;
    drawShell(title, detail);
    drawStatusRow(106, "Result", "stopped", FAIL);
    drawButton(primaryButton, "RESTART", ACCENT, TEXT);
}

void runFlow() {
    drawProgress("Scanning", "Looking for Access controllers...");
    auto controllers = session.scan(8);
    drawControllerList(controllers);
    delay(900);

    if (controllers.empty()) {
        drawFinalFailure("No Controller", "Move closer and restart.");
        return;
    }

    drawProgress("BLE UART", shortText(controllers[0].name, 30), "Testing advertised-device connect...");
    BleConnectResult ble = session.connect(controllers[0]);
    drawConnectResult(ble);
    delay(900);
    if (!ble.ok) {
        drawFinalFailure("BLE Failed", ble.error.length() > 0 ? shortText(ble.error, 32) : "UART service not ready.");
        return;
    }

    drawProgress("Hotspot", "Reading credentials over BLE...");
    HotspotCredentials credentials = session.readHotspot(controllers[0]);
    drawHotspotResult(credentials);
    delay(900);
    if (!credentials.ok) {
        drawFinalFailure("Hotspot Failed", credentials.error.length() > 0 ? shortText(credentials.error, 32) : "Auth or MessagePack failed.");
        return;
    }

    drawProgress("WiFi", "Connecting to controller hotspot...", shortText(credentials.ssid, 30));
    WifiConnectResult wifi = session.connectWifi(credentials);
    drawWifiResult(wifi);
    delay(900);
    if (!wifi.ok) {
        drawFinalFailure("WiFi Failed", wifi.error.length() > 0 ? shortText(wifi.error, 32) : "Could not join hotspot.");
        return;
    }

    drawProgress("EXOsocket", "Testing WebSocket handshake on port 80...");
    HttpProbeResult http = session.probeHttp(wifi);
    drawHttpResult(http);
}
}

void App::begin() {
    Serial.begin(115200);
    delay(300);

    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.invertDisplay(true);
    tft.fillScreen(BG);

    touch.begin();
    session.begin();
    drawHome();
}

void App::update() {
    if (screenMode == ScreenMode::Home) {
        TouchPoint point = touch.read();
        if (inRect(point, primaryButton) && touch.wasTapped()) {
            flowRequested = true;
        }

        if (flowRequested) {
            flowRequested = false;
            runFlow();
        }
    } else if (screenMode == ScreenMode::Done) {
        TouchPoint point = touch.read();
        if (inRect(point, primaryButton) && touch.wasTapped()) {
            Serial.println("Restart requested from touch UI");
            delay(150);
            ESP.restart();
        }
    }

    delay(20);
}
