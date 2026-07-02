#pragma once

#include <Arduino.h>

struct HourPrice {
    uint8_t hour = 0;
    float price = 0.0f;
    bool valid = false;
};

struct PowerPriceData {
    bool ok = false;
    bool loading = false;
    String error;
    String dateLabel;
    float currentPrice = 0.0f;
    HourPrice cheapestToday;
    HourPrice mostExpensiveToday;
    HourPrice nextHours[7];
    HourPrice cheapestTodayList[7];
    HourPrice cheapestTomorrowList[7];
    uint8_t nextHoursCount = 0;
    uint8_t cheapestTodayCount = 0;
    uint8_t cheapestTomorrowCount = 0;
    String status;
    uint32_t lastUpdatedMs = 0;
};

class PowerPriceService {
public:
    void begin();
    void update();
    void requestRefresh();
    PowerPriceData snapshot();

private:
    static void refreshTask(void *context);
    void refresh();
    bool ensureWifi(String &error);
    bool ensureTime(String &error);
    bool fetchDay(time_t day, HourPrice prices[24], uint8_t &count, String &dateLabel, String &error);
    void publish(const PowerPriceData &next);
    void setLoading(bool loading);

    PowerPriceData data;
    SemaphoreHandle_t mutex = nullptr;
    volatile bool refreshRunning = false;
    uint32_t lastRefreshRequestMs = 0;
};
