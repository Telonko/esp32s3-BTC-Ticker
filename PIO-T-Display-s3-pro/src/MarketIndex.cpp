#include "MarketIndex.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#define FNG_URL "https://api.alternative.me/fng/?limit=1"
#define FNG_REFRESH_MS (6 * 3600 * 1000UL)
#define FNG_RETRY_MS (10 * 60 * 1000UL)

static volatile int value = -1;
static unsigned long nextFetch = 0;

// {"data":[{"value":"32","value_classification":"Fear",...}],...}
static bool fetch()
{
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(10000);
    if (!http.begin(client, FNG_URL))
        return false;

    int code = http.GET();
    if (code != HTTP_CODE_OK)
    {
        Serial.printf("[FNG] HTTP %d\n", code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body))
        return false;
    const char *text = doc["data"][0]["value"] | "";
    if (!text[0])
        return false;

    value = constrain(atoi(text), 0, 100);
    Serial.printf("[FNG] Fear & Greed %d (%s)\n", value, (const char *)(doc["data"][0]["value_classification"] | ""));
    return true;
}

void marketIndexPoll()
{
    if ((long)(millis() - nextFetch) < 0)
        return;
    nextFetch = millis() + (fetch() ? FNG_REFRESH_MS : FNG_RETRY_MS);
}

int marketIndexValue()
{
    return value;
}
