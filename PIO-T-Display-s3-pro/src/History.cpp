#include "History.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "Settings.h"

#define HISTORY_TASK_STACK 12288
#define RETRY_DELAY_MS 60000

struct Series
{
    char name[TICKER_LEN];
    float points[HISTORY_POINTS];
    uint8_t count;
    unsigned long bucketStart; // millis() when the last point's bucket began
    unsigned long nextTry;     // loader: retry time after a failure
};

// Written by the UI (names), the loader task (points) and the network task
// (latest price); guarded by historyMux.
static portMUX_TYPE historyMux = portMUX_INITIALIZER_UNLOCKED;
static Series series[MAX_TICKERS];
static int seriesCount = 0;
static volatile uint32_t version = 0;
static TaskHandle_t taskHandle = nullptr;

// Fetches the closes of the last HISTORY_POINTS 15-minute candles
static bool fetchKlines(const char *name, float *out, int *count)
{
    char url[128];
    char symbol[TICKER_LEN + 4];
    int i = 0;
    for (; name[i]; i++)
        symbol[i] = toupper((unsigned char)name[i]);
    strcpy(symbol + i, "USDT");
    snprintf(url, sizeof(url), "https://api.binance.com/api/v3/klines?symbol=%s&interval=15m&limit=%d", symbol, HISTORY_POINTS);

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, url))
        return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        Serial.printf("[HIST] %s: HTTP %d\n", symbol, code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    // [[openTime, "open", "high", "low", "close", ...], ...]
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error)
    {
        Serial.printf("[HIST] %s: %s\n", symbol, error.c_str());
        return false;
    }

    int n = 0;
    for (JsonArray kline : doc.as<JsonArray>())
    {
        if (n >= HISTORY_POINTS)
            break;
        out[n++] = strtof(kline[4] | "0", nullptr);
    }
    *count = n;
    Serial.printf("[HIST] %s: %d points\n", symbol, n);
    return n > 0;
}

static void historyTask(void *)
{
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (WiFi.status() != WL_CONNECTED)
            continue;

        // Pick one pair that still needs loading
        char name[TICKER_LEN] = {0};
        portENTER_CRITICAL(&historyMux);
        for (int i = 0; i < seriesCount; i++)
        {
            if (series[i].count == 0 && (long)(millis() - series[i].nextTry) >= 0)
            {
                memcpy(name, series[i].name, TICKER_LEN);
                break;
            }
        }
        portEXIT_CRITICAL(&historyMux);
        if (!name[0])
            continue;

        static float points[HISTORY_POINTS];
        int count = 0;
        bool ok = fetchKlines(name, points, &count);

        portENTER_CRITICAL(&historyMux);
        for (int i = 0; i < seriesCount; i++)
        {
            if (strcmp(series[i].name, name) != 0)
                continue;
            if (ok)
            {
                memcpy(series[i].points, points, sizeof(float) * count);
                series[i].count = count;
                series[i].bucketStart = millis();
                version++;
            }
            else
            {
                series[i].nextTry = millis() + RETRY_DELAY_MS;
            }
        }
        portEXIT_CRITICAL(&historyMux);
    }
}

void historyBegin()
{
    if (taskHandle)
        return;
    xTaskCreatePinnedToCore(historyTask, "history", HISTORY_TASK_STACK, nullptr, 1, &taskHandle, 0);
}

void historySetTickers(const char names[][TICKER_LEN], int count)
{
    static Series updated[MAX_TICKERS];

    portENTER_CRITICAL(&historyMux);
    for (int i = 0; i < count; i++)
    {
        memset(&updated[i], 0, sizeof(Series));
        strlcpy(updated[i].name, names[i], TICKER_LEN);
        updated[i].nextTry = millis();
        for (int j = 0; j < seriesCount; j++)
        {
            if (strcmp(series[j].name, names[i]) == 0)
                updated[i] = series[j]; // keep loaded data
        }
    }
    memcpy(series, updated, sizeof(Series) * count);
    seriesCount = count;
    version++;
    portEXIT_CRITICAL(&historyMux);
}

void historyOnPrice(const char *name, double price)
{
    portENTER_CRITICAL(&historyMux);
    for (int i = 0; i < seriesCount; i++)
    {
        Series &s = series[i];
        if (strcmp(s.name, name) != 0 || s.count == 0)
            continue;

        // Start new 15-minute buckets as time passes (also after gaps)
        while (millis() - s.bucketStart >= HISTORY_BUCKET_MS)
        {
            if (s.count == HISTORY_POINTS)
                memmove(s.points, s.points + 1, sizeof(float) * (HISTORY_POINTS - 1));
            else
                s.count++;
            s.points[s.count - 1] = price;
            s.bucketStart += HISTORY_BUCKET_MS;
        }
        s.points[s.count - 1] = price; // current bucket follows the live price
        version++;
        break;
    }
    portEXIT_CRITICAL(&historyMux);
}

int historyGet(int idx, float *out)
{
    int count = 0;
    portENTER_CRITICAL(&historyMux);
    if (idx >= 0 && idx < seriesCount)
    {
        count = series[idx].count;
        memcpy(out, series[idx].points, sizeof(float) * count);
    }
    portEXIT_CRITICAL(&historyMux);
    return count;
}

uint32_t historyVersion()
{
    return version;
}
