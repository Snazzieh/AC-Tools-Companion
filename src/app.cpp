#include "app.h"
#include "core/controller_session.h"
#include "core/power_price_service.h"
#include "ui/touch_input.h"

#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <time.h>
#include <vector>

namespace {
TFT_eSPI tft;
TouchInput touch;
ControllerSession session;
PowerPriceService powerPrices;
SPIClass sdSpi(HSPI);

constexpr uint16_t visible(uint16_t color) {
    return static_cast<uint16_t>(~color);
}

constexpr uint16_t BG = visible(TFT_BLACK);
constexpr uint16_t SURFACE = visible(0x0861);
constexpr uint16_t PANEL = visible(TFT_BLACK);
constexpr uint16_t PANEL_2 = visible(0x2965);
constexpr uint16_t TEXT = visible(TFT_WHITE);
constexpr uint16_t MUTED = visible(0x7BEF);
constexpr uint16_t ACCENT = 0x245F;
constexpr uint16_t ACCENT_DARK = 0x21F5;
constexpr uint16_t OK = visible(0x5F0C);
constexpr uint16_t WARN = visible(0xFDA0);
constexpr uint16_t FAIL = visible(0xF986);
constexpr uint8_t SD_CS_PIN = 5;
constexpr uint8_t SD_SCLK_PIN = 18;
constexpr uint8_t SD_MISO_PIN = 19;
constexpr uint8_t SD_MOSI_PIN = 23;

struct ButtonRect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

const ButtonRect primaryButton{28, 198, 264, 35};
const ButtonRect restartTouchArea{0, 0, 320, 240};
const ButtonRect controllerAppButton{28, 98, 264, 32};
const ButtonRect priceAppButton{28, 156, 264, 32};
const ButtonRect controllerHomeTouchArea{0, 0, 320, 120};
const ButtonRect priceHomeTouchArea{0, 120, 320, 120};
const ButtonRect priceMenuButton{270, 4, 46, 42};
const ButtonRect priceMenuVisualButton{270, 4, 46, 42};

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
uint32_t lastPriceDataUpdatedMs = 0;
int lastClockMinuteOfDay = -1;
int lastPriceHour = -1;
bool sdFontsReady = false;
bool sdChecked = false;

bool inRect(const TouchPoint &point, const ButtonRect &rect) {
    return point.pressed && point.x >= rect.x && point.x < rect.x + rect.w && point.y >= rect.y && point.y < rect.y + rect.h;
}

void drawSmoothText(const String &text, const char *fontName, int16_t x, int16_t y, uint16_t color, uint16_t bg, uint8_t fallbackFont);
void drawSmoothCentered(const String &text, const char *fontName, int16_t x, int16_t y, int16_t w, uint16_t color, uint16_t bg, uint8_t fallbackFont);

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

    const char *fontName = size >= 2 ? "Title22" : "Text14";
    drawSmoothText(text, fontName, x, y, color, bg, size >= 2 ? 4 : 2);
}

void drawCenteredText(const String &text, int16_t x, int16_t y, int16_t w, uint16_t color, uint8_t size, uint16_t bg) {
    if (text == "Strømpriser") {
        int16_t cursorX = x + (w - textWidth("Strompriser", size)) / 2;
        drawStrompriserText(cursorX < x + 4 ? x + 4 : cursorX, y, color, size, bg);
        return;
    }

    const char *fontName = size >= 2 ? "Title22" : "Text14";
    drawSmoothCentered(text, fontName, x, y, w, color, bg, size >= 2 ? 4 : 2);
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

void drawHomeButton(const ButtonRect &rect, const String &label, uint16_t fill) {
    tft.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 7, fill);
    tft.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 7, PANEL_2);
    drawSmoothCentered(label, "Text14", rect.x, rect.y + 9, rect.w, TEXT, fill, 2);
}

void drawTinyText(const String &text, int16_t x, int16_t y, uint16_t color, uint16_t bg = BG) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, bg);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.drawString(text, x, y, 1);
}

void drawShell(const String &title, const String &subtitle = "") {
    tft.fillScreen(BG);
    tft.fillRect(0, 0, 320, 34, PANEL);
    tft.drawFastVLine(0, 0, 240, PANEL_2);
    tft.fillRect(0, 34, 320, 1, PANEL_2);
    tft.fillRoundRect(9, 8, 18, 18, 3, ACCENT);
    tft.setTextColor(TEXT, ACCENT);
    tft.setTextSize(1);
    tft.setCursor(13, 14);
    tft.print("AC");
    drawTextFit("AC Tools", 36, 10, TEXT, 1, PANEL);
    drawTextFit("Companion", 214, 10, MUTED, 1, PANEL);
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
    drawTinyText("BLE / Hotspot / WiFi / EXOsocket gateway", 28, 136, MUTED, SURFACE);
    drawHomeButton(controllerAppButton, "Controller", ACCENT_DARK);
    drawHomeButton(priceAppButton, "StrQmpriser", ACCENT);
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
    if (price < 1.0f) price = 1.0f;
    return String(price, 2) + " kr/kWh";
}

String priceValueText(float price) {
    if (price < 1.0f) price = 1.0f;
    return String(price, 2);
}

void priceValueParts(float price, String &whole, String &fraction) {
    int cents = static_cast<int>(roundf(price * 100.0f));
    if (cents < 0) cents = 0;
    whole = String(cents / 100);
    int frac = cents % 100;
    fraction = frac < 10 ? "0" + String(frac) : String(frac);
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

String decisionText(float price) {
    if (price <= 1.6f) return "Brug nu";
    if (price <= 2.1f) return "Vent lidt";
    return "Undgaa nu";
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

void drawModernText(const String &text, int16_t x, int16_t y, uint16_t color, uint8_t font, uint16_t bg);
void drawModernCentered(const String &text, int16_t x, int16_t y, int16_t w, uint16_t color, uint8_t font, uint16_t bg);

bool smoothFontExists(const char *name) {
    if (!sdFontsReady) return false;
    static bool price72Checked = false;
    static bool price72Ready = false;
    static bool priceChecked = false;
    static bool priceReady = false;
    static bool titleChecked = false;
    static bool titleReady = false;
    static bool textChecked = false;
    static bool textReady = false;
    static bool statusChecked = false;
    static bool statusReady = false;

    bool *checked = nullptr;
    bool *ready = nullptr;
    if (strcmp(name, "Price72") == 0) {
        checked = &price72Checked;
        ready = &price72Ready;
    } else if (strcmp(name, "Price64") == 0) {
        checked = &priceChecked;
        ready = &priceReady;
    } else if (strcmp(name, "Title22") == 0) {
        checked = &titleChecked;
        ready = &titleReady;
    } else if (strcmp(name, "Text14") == 0) {
        checked = &textChecked;
        ready = &textReady;
    } else if (strcmp(name, "Status18") == 0) {
        checked = &statusChecked;
        ready = &statusReady;
    }

    if (checked && *checked) return ready && *ready;

    String path = "/";
    path += name;
    path += ".vlw";
    bool exists = SD.exists(path);
    if (checked && ready) {
        *checked = true;
        *ready = exists;
    }
    return exists;
}

void logSmoothFont(const char *name) {
    String path = "/";
    path += name;
    path += ".vlw";
    Serial.printf("SD font %-8s %s\n", path.c_str(), SD.exists(path) ? "found" : "missing");
}

void beginSdFonts() {
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    sdSpi.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    sdFontsReady = SD.begin(SD_CS_PIN, sdSpi, 4000000);
    sdChecked = true;
    if (!sdFontsReady || SD.cardType() == CARD_NONE) {
        sdFontsReady = false;
        Serial.println("SD smooth fonts: not available");
        return;
    }

    uint8_t cardType = SD.cardType();
    const char *type = cardType == CARD_MMC ? "MMC" : cardType == CARD_SD ? "SD" : cardType == CARD_SDHC ? "SDHC" : "UNKNOWN";
    Serial.printf("SD smooth fonts: ready type=%s size=%lluMB\n", type, SD.cardSize() / (1024ULL * 1024ULL));
    logSmoothFont("Price72");
    logSmoothFont("Price64");
    logSmoothFont("Title22");
    logSmoothFont("Text14");
    logSmoothFont("Status18");
}

void drawSmoothText(const String &text, const char *fontName, int16_t x, int16_t y, uint16_t color, uint16_t bg, uint8_t fallbackFont) {
    if (smoothFontExists(fontName)) {
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(color, bg);
        tft.loadFont(fontName, SD);
        tft.drawString(text, x, y);
        tft.unloadFont();
        return;
    }

    drawModernText(text, x, y, color, fallbackFont, bg);
}

void drawSmoothCentered(const String &text, const char *fontName, int16_t x, int16_t y, int16_t w, uint16_t color, uint16_t bg, uint8_t fallbackFont) {
    if (smoothFontExists(fontName)) {
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(color, bg);
        tft.loadFont(fontName, SD);
        tft.drawString(text, x + w / 2, y);
        tft.unloadFont();
        tft.setTextDatum(TL_DATUM);
        return;
    }

    drawModernCentered(text, x, y, w, color, fallbackFont, bg);
}

void drawModernText(const String &text, int16_t x, int16_t y, uint16_t color, uint8_t font, uint16_t bg = BG) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, bg);
    tft.setTextFont(font);
    tft.setTextSize(1);
    tft.drawString(text, x, y, font);
    tft.setTextFont(1);
}

void drawModernCentered(const String &text, int16_t x, int16_t y, int16_t w, uint16_t color, uint8_t font, uint16_t bg = BG) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(color, bg);
    tft.setTextFont(font);
    tft.setTextSize(1);
    tft.drawString(text, x + w / 2, y, font);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(1);
}

void drawFreeCentered(const String &text, const GFXfont *font, int16_t x, int16_t y, int16_t w, uint16_t color, uint16_t bg) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(color, bg);
    tft.setFreeFont(font);
    tft.setTextSize(1);
    tft.drawString(text, x + w / 2, y, 1);
    tft.setTextDatum(TL_DATUM);
    tft.setFreeFont(nullptr);
}

void drawFreeText(const String &text, const GFXfont *font, int16_t x, int16_t y, uint16_t color, uint16_t bg) {
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(color, bg);
    tft.setFreeFont(font);
    tft.setTextSize(1);
    tft.drawString(text, x, y, 1);
    tft.setFreeFont(nullptr);
}

void drawLargePriceValue(float price, int16_t x, int16_t y, int16_t w, uint16_t color, uint16_t bg) {
    String text = priceValueText(price);
    if (smoothFontExists("Price72")) {
        drawSmoothCentered(text, "Price72", x, y, w, color, bg, 4);
        return;
    }

    if (smoothFontExists("Price64")) {
        drawSmoothCentered(text, "Price64", x, y, w, color, bg, 4);
        return;
    }

    drawFreeCentered(text, &FreeSansBold24pt7b, x, y, w, color, bg);
}

void drawTablePriceValue(float price, int16_t x, int16_t y, uint16_t color, uint16_t bg) {
    drawSmoothText(priceValueText(price), "Text14", x, y, color, bg, 2);
}

void drawThickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    tft.drawLine(x0, y0, x1, y1, color);
    tft.drawLine(x0 + 1, y0, x1 + 1, y1, color);
    tft.drawLine(x0, y0 + 1, x1, y1 + 1, color);
}

void drawPriceCard(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t outline) {
    tft.fillRoundRect(x, y, w, h, 12, SURFACE);
    tft.drawRoundRect(x, y, w, h, 12, outline);
    tft.drawRoundRect(x + 1, y + 1, w - 2, h - 2, 11, outline);
}

void drawSoftCard(int16_t x, int16_t y, int16_t w, int16_t h) {
    tft.fillRoundRect(x, y, w, h, 12, SURFACE);
}

void drawBoltIcon(int16_t x, int16_t y, uint16_t color) {
    drawThickLine(x + 13, y, x + 4, y + 15, color);
    drawThickLine(x + 4, y + 15, x + 13, y + 13, color);
    drawThickLine(x + 13, y + 13, x + 7, y + 31, color);
}

void drawBulbIcon(int16_t x, int16_t y, uint16_t color) {
    tft.drawCircle(x + 8, y + 8, 7, color);
    tft.drawCircle(x + 8, y + 8, 6, color);
    tft.drawLine(x + 8, y + 15, x + 8, y + 21, color);
    tft.drawLine(x + 9, y + 15, x + 9, y + 21, color);
    tft.drawFastHLine(x + 4, y + 21, 9, color);
    tft.drawFastHLine(x + 4, y + 22, 9, color);
    tft.drawFastHLine(x + 5, y + 24, 7, color);
}

void drawBackIconButton() {
    tft.fillRect(priceMenuVisualButton.x, priceMenuVisualButton.y, priceMenuVisualButton.w, priceMenuVisualButton.h, BG);
    int16_t cx = priceMenuVisualButton.x + priceMenuVisualButton.w / 2;
    int16_t cy = priceMenuVisualButton.y + priceMenuVisualButton.h / 2;
    tft.drawWideLine(cx + 6, cy - 8, cx - 6, cy, 2.4f, MUTED, BG);
    tft.drawWideLine(cx - 6, cy, cx + 6, cy + 8, 2.4f, MUTED, BG);
    tft.drawWideLine(cx - 4, cy, cx + 9, cy, 2.0f, MUTED, BG);
}

void drawBadge(int16_t x, int16_t y, int16_t w, const String &label, uint16_t fill) {
    tft.fillRoundRect(x, y, w, 20, 10, fill);
    drawSmoothCentered(label, "Status18", x, y + 2, w, PANEL, fill, 2);
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

String hourNumberText(uint8_t hour) {
    String text = hour < 10 ? "0" + String(hour) : String(hour);
    text += ":00";
    return text;
}

String currentClockText() {
    time_t now = time(nullptr);
    if (now < 100000) return "--:--";

    struct tm local;
    localtime_r(&now, &local);
    char buffer[6];
    snprintf(buffer, sizeof(buffer), "%02d:%02d", local.tm_hour, local.tm_min);
    return String(buffer);
}

int currentMinuteOfDay() {
    time_t now = time(nullptr);
    if (now < 100000) return -1;

    struct tm local;
    localtime_r(&now, &local);
    return local.tm_hour * 60 + local.tm_min;
}

String actionTitle(float currentPrice, const HourPrice &currentHour, const HourPrice &cheapest) {
    if (!cheapest.valid) return "Afventer";
    if (currentHour.valid && cheapest.hour == currentHour.hour) return "Billigst nu";
    if (currentPrice <= 1.6f) return "Lav pris";
    return "Vent";
}

String actionDetail(const HourPrice &currentHour, const HourPrice &cheapest) {
    if (!cheapest.valid) return "-";
    if (currentHour.valid && cheapest.hour == currentHour.hour) return "Brug nu";

    int minutes = minutesUntilHour(cheapest.hour);
    if (minutes <= 59) return "om " + String(minutes) + " min";
    return hourText(cheapest);
}

String insightStatusText(float price) {
    if (price <= 1.6f) return "Lav pris";
    if (price <= 2.1f) return "Normal";
    return "Hoj pris";
}

bool cheapestIsInChargeWindow(const PowerPriceData &prices) {
    return prices.cheapestToday.valid && prices.cheapestToday.hour >= 7 && prices.cheapestToday.hour <= 16;
}

void drawMenuCard() {
    tft.fillRect(190, 0, 130, 48, BG);
    drawSmoothCentered(currentClockText(), "Price64", 218, 8, 96, TEXT, BG, 4);
}

void drawPriceLoadingCard(const String &title, const String &detail, uint16_t color) {
    tft.fillScreen(BG);
    drawSoftCard(20, 74, 280, 82);
    drawSmoothCentered(title, "Title22", 20, 91, 280, color, SURFACE, 4);
    drawSmoothCentered(shortText(detail, 28), "Text14", 20, 126, 280, MUTED, SURFACE, 2);
    drawMenuCard();
}

void drawNextHoursCard(const PowerPriceData &prices) {
    tft.fillRect(0, 150, 320, 90, BG);
    HourPrice cheapest = cheapestUpcoming(prices);
    drawSmoothText("Kommende timer", "Text14", 24, 156, MUTED, BG, 2);

    if (prices.nextHoursCount == 0) {
        drawSmoothCentered("-", "Title22", 20, 190, 280, MUTED, BG, 4);
        return;
    }

    uint8_t count = min<uint8_t>(prices.nextHoursCount, 6);
    for (uint8_t i = 0; i < count; i++) {
        const HourPrice &item = prices.nextHours[i];
        int16_t centerX = 42 + i * 47;
        int16_t x = centerX - 24;
        bool isCheapest = cheapest.valid && item.hour == cheapest.hour;
        drawSmoothCentered(hourNumberText(item.hour), "Text14", x, 182, 48, MUTED, BG, 2);
        drawSmoothCentered(priceValueText(item.price), "Text14", x, 206, 48, TEXT, BG, 2);
        if (isCheapest) {
            tft.fillRoundRect(centerX - 11, 228, 22, 2, 1, OK);
        }
    }
}

void drawPricePage() {
    screenMode = ScreenMode::Price;
    PowerPriceData prices = powerPrices.snapshot();
    lastPriceDrawMs = millis();
    lastPriceLoading = prices.loading;
    lastPriceDataUpdatedMs = prices.lastUpdatedMs;
    lastClockMinuteOfDay = currentMinuteOfDay();
    lastPriceHour = lastClockMinuteOfDay >= 0 ? lastClockMinuteOfDay / 60 : -1;

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
    lastPriceDataUpdatedMs = prices.lastUpdatedMs;
    lastClockMinuteOfDay = currentMinuteOfDay();
    lastPriceHour = lastClockMinuteOfDay >= 0 ? lastClockMinuteOfDay / 60 : -1;

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
    HourPrice best = cheapest.valid ? cheapest : prices.cheapestToday;
    uint16_t nowColor = priceBandColor(prices.currentPrice);
    drawSmoothText("Pris nu", "Text14", 20, 18, MUTED, BG, 2);
    drawLargePriceValue(prices.currentPrice, 0, 56, 222, TEXT, BG);
    drawSmoothCentered("kr/kWh", "Text14", 6, 132, 214, MUTED, BG, 2);

    if (best.valid) {
        drawSmoothCentered(insightStatusText(prices.currentPrice), "Text14", 224, 55, 86, nowColor, BG, 2);
        drawSmoothCentered("Bedste time", "Text14", 224, 84, 86, MUTED, BG, 2);
        drawSmoothCentered(hourText(best), "Title22", 218, 106, 96, TEXT, BG, 4);
        drawSmoothCentered(priceValueText(best.price), "Text14", 214, 132, 104, MUTED, BG, 2);
    } else {
        drawSmoothCentered("-", "Title22", 218, 104, 94, MUTED, BG, 4);
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

    beginSdFonts();

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
        if (touch.wasTapped()) {
            if (inRect(point, controllerHomeTouchArea)) {
                flowRequested = true;
            } else if (inRect(point, priceHomeTouchArea)) {
                powerPrices.requestRefresh();
                drawPricePageV2();
            }
        }

        if (flowRequested) {
            flowRequested = false;
            runFlow();
        }
    } else if (screenMode == ScreenMode::Done) {
        TouchPoint point = touch.read();
        if (inRect(point, restartTouchArea) && touch.wasTapped()) {
            Serial.println("Restart requested from touch UI");
            delay(150);
            ESP.restart();
        }
    } else if (screenMode == ScreenMode::Price) {
        powerPrices.update();

        PowerPriceData prices = powerPrices.snapshot();
        int minuteOfDay = currentMinuteOfDay();
        int currentHour = minuteOfDay >= 0 ? minuteOfDay / 60 : -1;
        bool clockMinuteChanged = minuteOfDay >= 0 && minuteOfDay != lastClockMinuteOfDay;
        bool hourChanged = currentHour >= 0 && lastPriceHour >= 0 && currentHour != lastPriceHour;

        if (hourChanged && !prices.loading) {
            powerPrices.requestRefresh();
            prices = powerPrices.snapshot();
        }

        bool shouldRedraw = prices.loading != lastPriceLoading || prices.lastUpdatedMs != lastPriceDataUpdatedMs || hourChanged;
        if (shouldRedraw) {
            drawPricePageV2();
        } else if (clockMinuteChanged) {
            lastClockMinuteOfDay = minuteOfDay;
            lastPriceHour = currentHour;
            drawMenuCard();
        }

        TouchPoint point = touch.read();
        if (inRect(point, priceMenuButton) && touch.wasTapped()) {
            drawHome();
        }
    }

    delay(20);
}
