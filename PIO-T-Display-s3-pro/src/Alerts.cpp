#include "Alerts.h"
#include <Arduino.h>
#include <ui.h>
#include "Settings.h"
#include "BinanceWebSocket.h"

#define ALERT_BLINK_DURATION_MS 60000
#define ALERT_BLINK_PERIOD_MS 400

static bool active = false;
static unsigned long startedAt = 0;
static unsigned long lastToggle = 0;
static bool hidden = false;

static void fire(int idx, const char *direction, double threshold, double price)
{
    Serial.printf("[ALERT] %s %s %.8g (price %.8g)\n", settings.tickers[idx], direction, threshold, price);
    settingsSave();

    currentTicker = idx;
    setTickerInfo(); // also re-publishes streams: this pair may not need streaming anymore

    active = true;
    startedAt = millis();
    lastToggle = 0;
}

void alertsCheck()
{
    for (int i = 0; i < settings.tickerCount; i++)
    {
        PriceAlert &alert = settings.alerts[i];
        if (alert.above <= 0 && alert.below <= 0)
            continue;

        double price;
        if (!wsGetPrice(i, &price))
            continue;

        if (alert.above > 0 && price >= alert.above)
        {
            double threshold = alert.above;
            alert.above = 0;
            fire(i, ">=", threshold, price);
            return;
        }
        if (alert.below > 0 && price <= alert.below)
        {
            double threshold = alert.below;
            alert.below = 0;
            fire(i, "<=", threshold, price);
            return;
        }
    }
}

void alertDismiss()
{
    if (!active)
        return;
    active = false;
    hidden = false;
    lv_obj_clear_flag(ui_Label_Price_Rate, LV_OBJ_FLAG_HIDDEN);
}

bool alertActive()
{
    return active;
}

void alertLoop()
{
    if (!active)
        return;

    if (millis() - startedAt > ALERT_BLINK_DURATION_MS)
    {
        alertDismiss();
        return;
    }

    if (millis() - lastToggle >= ALERT_BLINK_PERIOD_MS)
    {
        lastToggle = millis();
        hidden = !hidden;
        if (hidden)
            lv_obj_add_flag(ui_Label_Price_Rate, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(ui_Label_Price_Rate, LV_OBJ_FLAG_HIDDEN);
    }
}
