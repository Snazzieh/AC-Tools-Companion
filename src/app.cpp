#include "app.h"
#include "core/controller_session.h"
#include "core/power_price_service.h"
#include "ui/touch_input.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <time.h>
#include <vector>

namespace {
TFT_eSPI tft;
TouchInput touch;
ControllerSession session;
PowerPriceService powerPrices;

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
const ButtonRect controllerAppButton{28, 98, 264, 32};
const ButtonRect priceAppButton{28, 156, 264, 32};
const ButtonRect priceMenuButton{268, 6, 44, 38};

enum class ScreenMode {
    Home,
    Running,
    Done,
    Price
};

ScreenMode screenMode = ScreenMode::Home;
bool flowRequested = false;
bool flowComplete = false;
String finalTitle;
String finalDetail;
bool finalOk = false;
bool gatewayRunning = false;
uint32_t lastPriceDrawMs = 0;
bool lastPriceLoading = false;

bool inRect(const TouchPoint &point, const ButtonRect &rect) {
    return point.pressed && point.x >= rect.x && point.x < rect.x + rect.w && point.y >= rect.y && point.y < rect.y + rect.h;
}

int16_t textWidth(const String &text, uint8_t size) {
    return text.length() * 6 * size;
}

void drawOslash(int16_t x, int16_t y, uint16_t color, uint8_t size, uint16_t bg) {
    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print("o");

    int16_t w = 6 * size;
    int16_t h = 8 * size;
    tft.drawLine(x + 1, y + h - 1, x + w - 1, y, color);
    if (size > 1) {
        tft.drawLine(x + 2, y + h - 1, x + w - 1, y + 1, color);
    }
}

void drawStrompriserText(int16_t x, int16_t y, uint16_t color, uint8_t size, uint16_t bg) {
    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print("Str");

    int16_t oslashX = x + textWidth("Str", size);
    drawOslash(oslashX, y, color, size, bg);

    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    tft.setCursor(oslashX + textWidth("o", size), y);
    tft.print("mpriser");
}

void drawStromprisText(int16_t x, int16_t y, uint16_t color, uint8_t size, uint16_t bg) {
    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print("Str");

    int16_t oslashX = x + textWidth("Str", size);
    drawOslash(oslashX, y, color, size, bg);

    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    tft.setCursor(oslashX + textWidth("o", size), y);
    tft.print("mpris");
}

void drawTextFit(const String &text, int16_t x, int16_t y, uint16_t color, uint8_t size, uint16_t bg = BG) {
    if (text == "Strømpriser") {
        drawStrompriserText(x, y, color, size, bg);
        return;
    }

    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    tft.setCursor(x, y);
    tft.print(text);
}

void drawCenteredText(const String &text, int16_t x, int16_t y, int16_t w, uint16_t color, uint8_t size, uint16_t bg) {
    if (text == "Strømpriser") {
        int16_t cursorX = x + (w - textWidth("Strompriser", size)) / 2;
        drawStrompriserText(cursorX < x + 4 ? x + 4 : cursorX, y, color, size, bg);
        return;
    }

    tft.setTextColor(color, bg);
    tft.setTextSize(size);
    int16_t textW = textWidth(text, size);
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

void drawSmallButton(const ButtonRect &rect, const String &label, uint16_t fill, uint16_t outline = PANEL_2) {
    tft.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, fill);
    tft.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, outline);
    drawCenteredText(label, rect.x, rect.y + 8, rect.w, TEXT, 1, fill);
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

    drawShell("AC Tools Companion", "Choose app.");
    drawPanel(14, 82, 292, 122);
    drawTextFit("Controller laeser", 28, 90, TEXT, 1, SURFACE);
    drawTextFit("BLE / Hotspot / WiFi / EXOsocket gateway", 28, 135, MUTED, 1, SURFACE);
    drawButton(controllerAppButton, "Controller", ACCENT_DARK, PANEL_2);
    drawButton(priceAppButton, "Strømpriser", ACCENT, PANEL_2);
}

void drawProgress(const String &title, const String &line1, const String &line2 = "", uint8_t percent = 0, const String &label = "") {
    screenMode = ScreenMode::Running;
    drawShell(title, line1);
    if (line2.length() > 0) {
        drawTextFit(line2, 16, 98, MUTED, 1);
    }

    uint8_t clamped = percent > 100 ? 100 : percent;
    uint16_t fillWidth = map(clamped, 0, 100, 0, 220);

    drawPanel(32, 143, 256, 42);
    tft.fillRoundRect(50, 159, 220, 8, 4, PANEL_2);
    if (fillWidth > 0) {
        tft.fillRoundRect(50, 159, fillWidth, 8, 4, ACCENT);
    }

    String progressText = label.length() > 0 ? label : "Working";
    progressText += "  ";
    progressText += String(clamped);
    progressText += "%";
    drawCenteredText(progressText, 32, 199, 256, MUTED, 1, BG);
}

void finishProgress(const String &title, const String &line1, const String &line2 = "", const String &label = "Done") {
    drawProgress(title, line1, line2, 100, label);
    delay(250);
}

String priceText(float price) {
    String text = String(price, 2);
    text.replace(".", ",");
    return text + " kr/kWh";
}

String priceValueText(float price) {
    String text = String(price, 2);
    text.replace(".", ",");
    return text;
}

String hourPriceText(const HourPrice &price) {
    if (!price.valid) return "-";
    String text = price.hour < 10 ? "0" + String(price.hour) : String(price.hour);
    text += ":00 ";
    text += priceValueText(price.price);
    return text;
}

String cheapHoursText(const HourPrice *items, uint8_t count, uint8_t start, uint8_t limit) {
    if (count == 0) return "-";

    String text;
    uint8_t end = min<uint8_t>(count, start + limit);
    for (uint8_t i = start; i < end; i++) {
        if (i > start) text += " ";
        if (items[i].hour < 10) text += "0";
        text += String(items[i].hour);
        text += "=";
        text += priceValueText(items[i].price);
    }
    return text;
}

uint16_t statusColor(const String &status) {
    if (status == "Billigt") return OK;
    if (status == "Dyrt") return WARN;
    return TEXT;
}

uint16_t priceBandColor(float price) {
    if (price <= 1.6f) return OK;
    if (price <= 2.1f) return WARN;
    return FAIL;
}

String priceStatusText(float price) {
    if (price <= 1.6f) return "BILLIG";
    if (price <= 2.1f) return "VENT";
    return "DYR";
}

String recommendationText(float price) {
    if (price <= 1.6f) return "LAD";
    if (price <= 2.1f) return "VENT";
    return "UNDGAA";
}

HourPrice cheapestUpcoming(const PowerPriceData &prices) {
    HourPrice best;
    for (uint8_t i = 0; i < prices.nextHoursCount && i < 7; i++) {
        const HourPrice &item = prices.nextHours[i];
        if (!item.valid) continue;
        if (!best.valid || item.price < best.price) best = item;
    }
    return best;
}

int minutesUntilHour(uint8_t hour) {
    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    int currentMinutes = local.tm_hour * 60 + local.tm_min;
    int targetMinutes = hour * 60;
    if (targetMinutes < currentMinutes) targetMinutes += 24 * 60;
    return targetMinutes - currentMinutes;
}

void drawPriceCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline) {
    tft.fillRoundRect(x, y, w, h, 10, SURFACE);
    tft.drawRoundRect(x, y, w, h, 10, outline);
}

void drawSoftCard(int16_t x, int16_t y, int16_t w, int16_t h) {
    tft.fillRoundRect(x, y, w, h, 10, SURFACE);
    tft.drawRoundRect(x, y, w, h, 10, PANEL_2);
}

void drawBoltIcon(int16_t x, int16_t y, uint16_t color) {
    tft.fillTriangle(x + 10, y, x, y + 18, x + 10, y + 15, color);
    tft.fillTriangle(x + 8, y + 13, x + 20, y + 10, x + 7, y + 32, color);
}

void drawBulbIcon(int16_t x, int16_t y, uint16_t color) {
    tft.drawCircle(x + 8, y + 8, 7, color);
    tft.drawLine(x + 8, y + 15, x + 8, y + 21, color);
    tft.drawFastHLine(x + 4, y + 21, 9, color);
    tft.drawFastHLine(x + 5, y + 24, 7, color);
}

void drawBackIconButton() {
    int16_t x = priceMenuButton.x + 6;
    int16_t y = priceMenuButton.y + 5;
    tft.fillRoundRect(x, y, 32, 28, 9, SURFACE);
    tft.drawRoundRect(x, y, 32, 28, 9, ACCENT_DARK);
    tft.drawLine(x + 19, y + 8, x + 10, y + 14, TEXT);
    tft.drawLine(x + 10, y + 14, x + 19, y + 20, TEXT);
    tft.drawLine(x + 11, y + 14, x + 24, y + 14, TEXT);
}

void drawBadge(int16_t x, int16_t y, int16_t w, const String &label, uint16_t fill) {
    tft.fillRoundRect(x, y, w, 18, 5, fill);
    drawCenteredText(label, x, y + 5, w, PANEL, 1, fill);
}

void drawPriceFact(int16_t x, int16_t y, const String &label, const HourPrice &price, uint16_t color) {
    drawPriceCard(x, y, 152, 36, color);
    drawTextFit(label, x + 10, y + 7, color, 1, SURFACE);
    drawTextFit(hourPriceText(price), x + 76, y + 7, TEXT, 1, SURFACE);
    drawTextFit(priceText(price.price), x + 76, y + 20, color, 1, SURFACE);
}

String hourText(const HourPrice &price) {
    if (!price.valid) return "-";
    String text = price.hour < 10 ? "0" + String(price.hour) : String(price.hour);
    text += ":00";
    return text;
}

bool cheapestIsInChargeWindow(const PowerPriceData &prices) {
    return prices.cheapestToday.valid && prices.cheapestToday.hour >= 7 && prices.cheapestToday.hour <= 16;
}

void drawMenuCard() {
    drawBackIconButton();
}

void drawPriceLoadingCard(const String &title, const String &detail, uint16_t color) {
    tft.fillScreen(BG);
    drawSoftCard(18, 72, 284, 86);
    drawCenteredText(title, 18, 94, 284, color, 2, SURFACE);
    drawCenteredText(shortText(detail, 28), 18, 126, 284, MUTED, 1, SURFACE);
    drawMenuCard();
}

void drawNextHoursCard(const PowerPriceData &prices) {
    HourPrice cheapest = cheapestUpcoming(prices);
    drawSoftCard(8, 150, 304, 82);
    drawTextFit("NAESTE TIMER", 20, 160, ACCENT, 1, SURFACE);
    drawTextFit("kr/kWh", 250, 160, MUTED, 1, SURFACE);

    if (prices.nextHoursCount == 0) {
        drawCenteredText("-", 8, 184, 304, MUTED, 2, SURFACE);
        return;
    }

    for (uint8_t i = 0; i < prices.nextHoursCount && i < 7; i++) {
        const HourPrice &item = prices.nextHours[i];
        int16_t col = i < 4 ? 0 : 1;
        int16_t row = i < 4 ? i : i - 4;
        int16_t x = col == 0 ? 20 : 166;
        int16_t y = 176 + row * 14;
        bool isCheapest = cheapest.valid && item.hour == cheapest.hour;
        uint16_t rowColor = isCheapest ? OK : priceBandColor(item.price);
        tft.fillCircle(x, y + 4, isCheapest ? 4 : 3, rowColor);
        drawTextFit(String(item.hour), x + 16, y, i == 0 ? TEXT : MUTED, 1, SURFACE);
        drawTextFit(priceValueText(item.price), x + 62, y, isCheapest ? OK : TEXT, 1, SURFACE);
        if (isCheapest) drawTextFit("*", x + 110, y, OK, 1, SURFACE);
    }
}

void drawPricePage() {
    screenMode = ScreenMode::Price;
    PowerPriceData prices = powerPrices.snapshot();
    lastPriceDrawMs = millis();
    lastPriceLoading = prices.loading;

    drawShell("Strømpriser", prices.dateLabel.length() > 0 ? "ElprisenLigeNu " + prices.dateLabel : "ElprisenLigeNu");

    if (prices.loading) {
        drawProgress("Strømpriser", "Henter priser fra ElprisenLigeNu...", "", 0, "API");
        screenMode = ScreenMode::Price;
        drawButton(priceMenuButton, "MENU", ACCENT_DARK, PANEL_2);
        return;
    }

    if (!prices.ok) {
        drawStatusRow(86, "Status", "Fejl", FAIL);
        drawStatusRow(116, "Detail", shortText(prices.error.length() > 0 ? prices.error : "Ingen data", 22), FAIL);
        drawTextFit("Aabn siden igen for nyt forsoeg.", 28, 162, MUTED, 1);
        drawButton(priceMenuButton, "MENU", ACCENT_DARK, PANEL_2);
        return;
    }

    uint16_t nowColor = statusColor(prices.status);
    drawPriceCard(14, 88, 132, 78, nowColor);
    drawTextFit("PRIS NU", 24, 99, nowColor, 1, SURFACE);
    drawTextFit(priceValueText(prices.currentPrice), 34, 118, nowColor, 3, SURFACE);
    drawTextFit("kr/kWh", 55, 145, TEXT, 1, SURFACE);
    drawBadge(36, 163, 88, prices.status, nowColor);

    drawPriceFact(154, 88, "BILLIGST", prices.cheapestToday, OK);
    drawPriceFact(154, 130, "DYREST", prices.mostExpensiveToday, FAIL);

    drawTextFit("I dag", 18, 176, OK, 1);
    drawTextFit(cheapHoursText(prices.cheapestTodayList, prices.cheapestTodayCount, 0, 3), 72, 176, MUTED, 1);
    drawTextFit(cheapHoursText(prices.cheapestTodayList, prices.cheapestTodayCount, 3, 4), 72, 185, MUTED, 1);
    drawTextFit("I morgen", 18, 194, ACCENT, 1);
    drawTextFit(cheapHoursText(prices.cheapestTomorrowList, prices.cheapestTomorrowCount, 0, 3), 72, 194, MUTED, 1);
    drawTextFit(cheapHoursText(prices.cheapestTomorrowList, prices.cheapestTomorrowCount, 3, 4), 72, 203, MUTED, 1);
    drawButton(priceMenuButton, "MENU", ACCENT_DARK, PANEL_2);
}

void drawPricePageV2() {
    screenMode = ScreenMode::Price;
    PowerPriceData prices = powerPrices.snapshot();
    lastPriceDrawMs = millis();
    lastPriceLoading = prices.loading;

    if (prices.loading) {
        drawPriceLoadingCard("HENTER DATA", "Forbinder og henter priser", ACCENT);
        return;
    }

    if (!prices.ok) {
        drawPriceLoadingCard("FEJL", prices.error.length() > 0 ? prices.error : "Ingen data", FAIL);
        return;
    }

    tft.fillScreen(BG);
    drawMenuCard();

    HourPrice cheapest = cheapestUpcoming(prices);
    uint16_t nowColor = priceBandColor(prices.currentPrice);
    drawSoftCard(8, 8, 196, 132);
    drawBoltIcon(24, 22, nowColor);
    drawTextFit("PRIS NU", 56, 28, MUTED, 1, SURFACE);
    drawCenteredText(priceValueText(prices.currentPrice), 8, 54, 196, nowColor, 4, SURFACE);
    drawCenteredText("kr/kWh", 8, 94, 196, TEXT, 1, SURFACE);
    drawBadge(52, 114, 108, priceStatusText(prices.currentPrice), nowColor);

    uint16_t bestColor = cheapest.valid ? priceBandColor(cheapest.price) : ACCENT_DARK;
    drawSoftCard(212, 52, 100, 88);
    drawBulbIcon(226, 66, bestColor);
    if (cheapest.valid) {
        int minutes = minutesUntilHour(cheapest.hour);
        drawTextFit(minutes <= 59 ? "Billigst om" : "Billigst igen", 222, 91, MUTED, 1, SURFACE);
        if (minutes <= 59) {
            drawCenteredText(String(minutes), 212, 105, 100, bestColor, 2, SURFACE);
            drawCenteredText("min", 212, 124, 100, bestColor, 1, SURFACE);
        } else {
            drawCenteredText(hourText(cheapest), 212, 112, 100, bestColor, 2, SURFACE);
        }
    } else {
        drawCenteredText("-", 212, 106, 100, MUTED, 2, SURFACE);
    }

    drawNextHoursCard(prices);
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

void drawGatewayResult(const ExoSocketGatewayResult &result) {
    finalOk = result.ok;
    finalTitle = result.ok ? "Gateway Ready" : "Gateway Failed";
    finalDetail = result.ok ? "Connect AC Tools to CYD WiFi." : result.error;
    gatewayRunning = result.ok;

    screenMode = ScreenMode::Done;
    flowComplete = true;
    drawShell(finalTitle, finalDetail);
    drawStatusRow(92, "SSID", result.apSsid, result.ok ? TEXT : MUTED);
    drawStatusRow(122, "Password", "actools123", result.ok ? TEXT : MUTED);
    drawStatusRow(152, "Gateway", result.apIp.length() > 0 ? result.apIp : "-", result.ok ? OK : FAIL);
    drawStatusRow(182, "Upstream", result.upstreamHost.length() > 0 ? result.upstreamHost : "-", MUTED);
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
    drawProgress("Scanning", "Looking for Access controllers...", "", 0, "BLE scan");
    auto controllers = session.scan(8);
    finishProgress("Scanning", "Looking for Access controllers...", "", "BLE scan");
    drawControllerList(controllers);
    delay(900);

    if (controllers.empty()) {
        drawFinalFailure("No Controller", "Move closer and restart.");
        return;
    }

    drawProgress("BLE UART", shortText(controllers[0].name, 30), "Testing advertised-device connect...", 0, "BLE");
    BleConnectResult ble = session.connect(controllers[0]);
    finishProgress("BLE UART", shortText(controllers[0].name, 30), "Testing advertised-device connect...", "BLE");
    drawConnectResult(ble);
    delay(900);
    if (!ble.ok) {
        drawFinalFailure("BLE Failed", ble.error.length() > 0 ? shortText(ble.error, 32) : "UART service not ready.");
        return;
    }

    drawProgress("Hotspot", "Reading credentials over BLE...", "", 0, "Auth");
    HotspotCredentials credentials = session.readHotspot(controllers[0]);
    finishProgress("Hotspot", "Reading credentials over BLE...", "", "Auth");
    drawHotspotResult(credentials);
    delay(900);
    if (!credentials.ok) {
        drawFinalFailure("Hotspot Failed", credentials.error.length() > 0 ? shortText(credentials.error, 32) : "Auth or MessagePack failed.");
        return;
    }

    drawProgress("WiFi", "Connecting to controller hotspot...", shortText(credentials.ssid, 30), 0, "WiFi");
    WifiConnectResult wifi = session.connectWifi(credentials);
    finishProgress("WiFi", "Connecting to controller hotspot...", shortText(credentials.ssid, 30), "WiFi");
    drawWifiResult(wifi);
    delay(900);
    if (!wifi.ok) {
        drawFinalFailure("WiFi Failed", wifi.error.length() > 0 ? shortText(wifi.error, 32) : "Could not join hotspot.");
        return;
    }

    drawProgress("EXOsocket", "Testing WebSocket handshake on port 80...", "", 0, "Handshake");
    HttpProbeResult http = session.probeHttp(wifi);
    finishProgress("EXOsocket", "Testing WebSocket handshake on port 80...", "", "Handshake");
    drawHttpResult(http);
    delay(900);
    if (!http.ok) return;

    drawProgress("Gateway", "Starting AC Tools bridge...", "AP: AC Tools Companion", 0, "Gateway");
    ExoSocketGatewayResult gateway = session.startGateway(wifi);
    finishProgress("Gateway", "Starting AC Tools bridge...", "AP: AC Tools Companion", "Gateway");
    drawGatewayResult(gateway);
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
    powerPrices.begin();
    drawHome();
}

void App::update() {
    session.updateGateway();

    if (screenMode == ScreenMode::Home) {
        TouchPoint point = touch.read();
        if (inRect(point, controllerAppButton) && touch.wasTapped()) {
            flowRequested = true;
        } else if (inRect(point, priceAppButton) && touch.wasTapped()) {
            powerPrices.requestRefresh();
            drawPricePageV2();
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
    } else if (screenMode == ScreenMode::Price) {
        powerPrices.update();

        PowerPriceData prices = powerPrices.snapshot();
        bool shouldRedraw = prices.loading != lastPriceLoading;
        if (shouldRedraw) {
            drawPricePageV2();
        }

        TouchPoint point = touch.read();
        if (inRect(point, priceMenuButton) && touch.wasTapped()) {
            drawHome();
        }
    }

    delay(20);
}
