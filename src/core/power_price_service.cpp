#include "power_price_service.h"

#include "../config/wifi_credentials.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <math.h>
#include <time.h>

namespace {
constexpr uint32_t REFRESH_INTERVAL_MS = 60UL * 60UL * 1000UL;
constexpr uint32_t RETRY_AFTER_ERROR_MS = 60UL * 1000UL;
constexpr uint32_t WIFI_TIMEOUT_MS = 30000;
constexpr uint32_t TIME_TIMEOUT_MS = 12000;
constexpr float PRICE_ADJUSTMENT_DKK_PER_KWH = 0.90f;
constexpr const char *TIMEZONE = "CET-1CEST,M3.5.0/2,M10.5.0/3";

bool validPrice(float price) {
    return !isnan(price) && isfinite(price);
}

String twoDigits(int value) {
    return value < 10 ? "0" + String(value) : String(value);
}

String endpointForDay(time_t day) {
    struct tm local;
    localtime_r(&day, &local);

    String url = "https://www.elprisenligenu.dk/api/v1/prices/";
    url += String(local.tm_year + 1900);
    url += "/";
    url += twoDigits(local.tm_mon + 1);
    url += "-";
    url += twoDigits(local.tm_mday);
    url += "_DK1.json";
    return url;
}

uint8_t hourFromTimeStart(const char *timeStart) {
    if (!timeStart) return 255;
    String value = timeStart;
    if (value.length() < 13) return 255;

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (sscanf(value.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6) {
        return static_cast<uint8_t>(value.substring(11, 13).toInt());
    }

    bool utcTimestamp = value.endsWith("Z") || value.endsWith("+00:00");
    if (!utcTimestamp) {
        return hour > 23 ? 255 : static_cast<uint8_t>(hour);
    }

    struct tm utc = {};
    utc.tm_year = year - 1900;
    utc.tm_mon = month - 1;
    utc.tm_mday = day;
    utc.tm_hour = hour;
    utc.tm_min = minute;
    utc.tm_sec = second;
    utc.tm_isdst = 0;

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t epoch = mktime(&utc);
    setenv("TZ", TIMEZONE, 1);
    tzset();

    struct tm local;
    localtime_r(&epoch, &local);
    return static_cast<uint8_t>(local.tm_hour);
}

void sortPrices(HourPrice *items, uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        uint8_t best = i;
        for (uint8_t j = i + 1; j < count; j++) {
            if (items[j].price < items[best].price) best = j;
        }
        if (best != i) {
            HourPrice tmp = items[i];
            items[i] = items[best];
            items[best] = tmp;
        }
    }
}

String hourLabel(const HourPrice &price) {
    if (!price.valid) return "-";
    return twoDigits(price.hour) + ":00";
}

const char *wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL: return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED: return "WL_SCAN_COMPLETED";
        case WL_CONNECTED: return "WL_CONNECTED";
        case WL_CONNECT_FAILED: return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED: return "WL_DISCONNECTED";
        default: return "WL_UNKNOWN";
    }
}
}

void PowerPriceService::begin() {
    if (!mutex) mutex = xSemaphoreCreateMutex();
    setenv("TZ", TIMEZONE, 1);
    tzset();
}

void PowerPriceService::update() {
    PowerPriceData copy = snapshot();
    if (copy.loading) return;

    if (copy.lastUpdatedMs == 0) {
        if (lastRefreshRequestMs == 0 || millis() - lastRefreshRequestMs >= RETRY_AFTER_ERROR_MS) {
            requestRefresh();
        }
        return;
    }

    if (millis() - copy.lastUpdatedMs >= REFRESH_INTERVAL_MS) {
        requestRefresh();
    }
}

void PowerPriceService::requestRefresh() {
    if (refreshRunning) return;

    refreshRunning = true;
    lastRefreshRequestMs = millis();
    setLoading(true);

    BaseType_t created = xTaskCreatePinnedToCore(
        refreshTask,
        "power-price",
        12288,
        this,
        1,
        nullptr,
        1
    );

    if (created != pdPASS) {
        PowerPriceData next = snapshot();
        next.ok = false;
        next.loading = false;
        next.error = "Could not start price task";
        publish(next);
        refreshRunning = false;
    }
}

PowerPriceData PowerPriceService::snapshot() {
    PowerPriceData copy;
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        copy = data;
        xSemaphoreGive(mutex);
    }
    return copy;
}

void PowerPriceService::refreshTask(void *context) {
    auto *service = static_cast<PowerPriceService *>(context);
    service->refresh();
    service->refreshRunning = false;
    vTaskDelete(nullptr);
}

void PowerPriceService::refresh() {
    PowerPriceData next;
    next.loading = true;

    String error;
    if (!ensureWifi(error)) {
        next.ok = false;
        next.loading = false;
        next.error = error;
        publish(next);
        return;
    }

    if (!ensureTime(error)) {
        next.ok = false;
        next.loading = false;
        next.error = error;
        publish(next);
        return;
    }

    time_t now = time(nullptr);
    struct tm local;
    localtime_r(&now, &local);
    int currentHour = local.tm_hour;

    HourPrice today[24];
    HourPrice tomorrow[24];
    uint8_t todayCount = 0;
    uint8_t tomorrowCount = 0;
    String dateLabel;

    if (!fetchDay(now, today, todayCount, dateLabel, error)) {
        next.ok = false;
        next.loading = false;
        next.error = error;
        publish(next);
        return;
    }

    String ignoredDate;
    time_t tomorrowTime = now + 24UL * 60UL * 60UL;
    bool hasTomorrow = fetchDay(tomorrowTime, tomorrow, tomorrowCount, ignoredDate, error);

    next.ok = true;
    next.loading = false;
    next.error = "";
    next.dateLabel = dateLabel;
    next.lastUpdatedMs = millis();

    float minPrice = 99999.0f;
    float maxPrice = -99999.0f;
    bool currentPriceFound = false;
    for (uint8_t i = 0; i < todayCount; i++) {
        if (!today[i].valid) continue;
        if (today[i].hour == currentHour) {
            next.currentPrice = today[i].price;
            currentPriceFound = true;
        }
        if (today[i].price < minPrice) {
            minPrice = today[i].price;
            next.cheapestToday = today[i];
        }
        if (today[i].price > maxPrice) {
            maxPrice = today[i].price;
            next.mostExpensiveToday = today[i];
        }
    }

    if (!currentPriceFound && todayCount > 0) {
        uint8_t fallbackIndex = 0;
        uint8_t smallestDistance = 24;
        for (uint8_t i = 0; i < todayCount; i++) {
            if (!today[i].valid) continue;
            uint8_t distance = today[i].hour > currentHour ? today[i].hour - currentHour : currentHour - today[i].hour;
            if (distance < smallestDistance) {
                smallestDistance = distance;
                fallbackIndex = i;
            }
        }
        next.currentPrice = today[fallbackIndex].price;
        Serial.printf("Power price current hour %d missing, using hour %u price=%.4f\n",
                      currentHour,
                      today[fallbackIndex].hour,
                      next.currentPrice);
    }

    if (!currentPriceFound && todayCount == 0) {
        next.ok = false;
        next.error = "No current price";
        publish(next);
        return;
    }

    Serial.printf("Power price summary currentHour=%d current=%.4f min=%.4f max=%.4f count=%u\n",
                  currentHour,
                  next.currentPrice,
                  minPrice,
                  maxPrice,
                  todayCount);

    for (uint8_t i = 0; i < todayCount && next.nextHoursCount < 7; i++) {
        if (today[i].valid && today[i].hour >= currentHour) {
            next.nextHours[next.nextHoursCount++] = today[i];
        }
    }

    if (hasTomorrow) {
        for (uint8_t i = 0; i < tomorrowCount && next.nextHoursCount < 7; i++) {
            if (tomorrow[i].valid) {
                next.nextHours[next.nextHoursCount++] = tomorrow[i];
            }
        }
    }

    sortPrices(today, todayCount);
    next.cheapestTodayCount = min<uint8_t>(7, todayCount);
    for (uint8_t i = 0; i < next.cheapestTodayCount; i++) {
        next.cheapestTodayList[i] = today[i];
    }

    if (hasTomorrow) {
        sortPrices(tomorrow, tomorrowCount);
        next.cheapestTomorrowCount = min<uint8_t>(7, tomorrowCount);
        for (uint8_t i = 0; i < next.cheapestTomorrowCount; i++) {
            next.cheapestTomorrowList[i] = tomorrow[i];
        }
    }

    float range = maxPrice - minPrice;
    if (range <= 0.001f || next.currentPrice <= minPrice + range * 0.34f) {
        next.status = "Billigt";
    } else if (next.currentPrice >= minPrice + range * 0.67f) {
        next.status = "Dyrt";
    } else {
        next.status = "Normalt";
    }

    publish(next);
}

bool PowerPriceService::ensureWifi(String &error) {
    if (WiFi.status() == WL_CONNECTED) {
        for (int i = 0; i < WIFI_NETWORK_COUNT; i++) {
            if (WiFi.SSID() == WIFI_NETWORKS[i].ssid) return true;
        }
    }

    Serial.println("Power price WiFi scanning for known networks...");
    WiFi.persistent(false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    delay(300);
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);
    WiFi.disconnect(false, false);
    delay(200);

    int foundNetworks = WiFi.scanNetworks(false, true);
    int selectedNetwork = -1;
    int selectedRssi = -127;
    for (int known = 0; known < WIFI_NETWORK_COUNT; known++) {
        for (int scanned = 0; scanned < foundNetworks; scanned++) {
            if (WiFi.SSID(scanned) == WIFI_NETWORKS[known].ssid && WiFi.RSSI(scanned) > selectedRssi) {
                selectedNetwork = known;
                selectedRssi = WiFi.RSSI(scanned);
            }
        }
    }
    WiFi.scanDelete();

    if (selectedNetwork < 0) {
        error = "Known SSID not found";
        Serial.printf("Power price WiFi failed: no known SSID found among %d networks\n", foundNetworks);
        return false;
    }

    const WifiCredential &network = WIFI_NETWORKS[selectedNetwork];
    Serial.printf("Power price WiFi connecting to %s rssi=%d...\n", network.ssid, selectedRssi);
    WiFi.begin(network.ssid, network.password);

    uint32_t start = millis();
    uint32_t lastLog = 0;
    wl_status_t lastStatus = WL_IDLE_STATUS;
    while (millis() - start < WIFI_TIMEOUT_MS) {
        wl_status_t status = WiFi.status();
        lastStatus = status;

        if (status == WL_CONNECTED) {
            Serial.printf("Power price WiFi connected ip=%s gateway=%s dns=%s\n",
                          WiFi.localIP().toString().c_str(),
                          WiFi.gatewayIP().toString().c_str(),
                          WiFi.dnsIP().toString().c_str());
            return true;
        }

        if (millis() - lastLog >= 1000) {
            lastLog = millis();
            Serial.printf("Power price WiFi status=%d %s\n", status, wifiStatusName(status));
        }

        delay(100);
        yield();
    }

    error = "WiFi timeout";
    Serial.printf("Power price WiFi failed ssid=%s status=%d %s rssi=%d\n",
                  network.ssid,
                  lastStatus,
                  wifiStatusName(lastStatus),
                  selectedRssi);
    return false;
}

bool PowerPriceService::ensureTime(String &error) {
    time_t now = time(nullptr);
    if (now > 1700000000) return true;

    configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

    uint32_t start = millis();
    while (millis() - start < TIME_TIMEOUT_MS) {
        now = time(nullptr);
        if (now > 1700000000) return true;
        delay(100);
        yield();
    }

    error = "No time from NTP";
    Serial.println("Power price NTP failed");
    return false;
}

bool PowerPriceService::fetchDay(time_t day, HourPrice prices[24], uint8_t &count, String &dateLabel, String &error) {
    count = 0;

    struct tm local;
    localtime_r(&day, &local);
    dateLabel = twoDigits(local.tm_mday) + "/" + twoDigits(local.tm_mon + 1);

    String url = endpointForDay(day);
    Serial.printf("Power price GET %s\n", url.c_str());

    WiFiClientSecure secure;
    secure.setInsecure();

    HTTPClient http;
    http.setTimeout(12000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(secure, url)) {
        error = "API begin failed";
        return false;
    }

    int status = http.GET();
    if (status != HTTP_CODE_OK) {
        error = "API HTTP " + String(status);
        http.end();
        Serial.println(error);
        return false;
    }

    String payload = http.getString();
    http.end();

    DynamicJsonDocument doc(32768);
    DeserializationError jsonError = deserializeJson(doc, payload);
    if (jsonError) {
        error = "Invalid JSON";
        Serial.printf("Power price JSON error: %s\n", jsonError.c_str());
        return false;
    }

    if (!doc.is<JsonArray>()) {
        error = "Missing JSON array";
        return false;
    }

    JsonArray array = doc.as<JsonArray>();
    for (JsonObject item : array) {
        if (count >= 24) break;
        float price = item["DKK_per_kWh"] | NAN;
        uint8_t hour = hourFromTimeStart(item["time_start"] | "");
        if (!validPrice(price) || hour > 23) continue;

        prices[count].hour = hour;
        prices[count].price = price + PRICE_ADJUSTMENT_DKK_PER_KWH;
        prices[count].valid = true;
        count++;
    }

    if (count == 0) {
        error = "No prices in JSON";
        return false;
    }

    return true;
}

void PowerPriceService::publish(const PowerPriceData &next) {
    if (mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        data = next;
        xSemaphoreGive(mutex);
    }
}

void PowerPriceService::setLoading(bool loading) {
    PowerPriceData next = snapshot();
    next.loading = loading;
    if (loading) {
        next.error = "";
    }
    publish(next);
}
