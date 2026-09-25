#include "Battery.h"
#include <Arduino.h>
#include "pin_config.h"

// The board has a 1:2 divider in front of the ADC pin
#define BAT_DIVIDER 2
#define BAT_SAMPLES 16
// Smoothing factor per update (~1 s): time constant of about 20 s
#define BAT_EMA_ALPHA 0.05f
// Displayed level changes only when it moves this far from the shown value
#define BAT_HYSTERESIS 3

// Typical Li-ion open-circuit voltage curve (mV -> %), under light load
static const struct
{
    uint16_t mv;
    uint8_t percent;
} curve[] = {
    {3300, 0}, {3500, 5}, {3600, 10}, {3700, 20}, {3750, 30}, {3790, 40},
    {3830, 50}, {3870, 60}, {3920, 70}, {3980, 80}, {4060, 90}, {4150, 100},
};

// Charging detection: voltage trend over CHARGE_WINDOW samples
#define CHARGE_SAMPLE_MS 30000
#define CHARGE_WINDOW 5         // 5 samples = 2 minutes
#define CHARGE_RISE_MV 8        // rising at least this much -> charging
#define CHARGE_FALL_MV -3       // falling at least this much -> not charging

static float filteredMv = 0;
static int shownPercent = -1;
static uint16_t trend[CHARGE_WINDOW];
static uint8_t trendCount = 0;
static unsigned long lastTrendSample = 0;
static bool charging = false;

static void updateChargingTrend()
{
    if (lastTrendSample != 0 && millis() - lastTrendSample < CHARGE_SAMPLE_MS)
        return;
    lastTrendSample = millis();

    if (trendCount == CHARGE_WINDOW)
    {
        memmove(trend, trend + 1, sizeof(trend[0]) * (CHARGE_WINDOW - 1));
        trendCount--;
    }
    trend[trendCount++] = (uint16_t)filteredMv;
    if (trendCount < CHARGE_WINDOW)
        return;

    int delta = (int)trend[CHARGE_WINDOW - 1] - (int)trend[0];
    if (delta >= CHARGE_RISE_MV)
        charging = true;
    else if (delta <= CHARGE_FALL_MV)
        charging = false;
}

// Median-ish reading: drop the extremes caused by Wi-Fi TX current spikes
static uint32_t readMilliVolts()
{
    uint32_t samples[BAT_SAMPLES];
    for (int i = 0; i < BAT_SAMPLES; i++)
    {
        samples[i] = analogReadMilliVolts(PIN_BAT_VOLT); // eFuse-calibrated
    }

    // Insertion sort, 16 elements
    for (int i = 1; i < BAT_SAMPLES; i++)
    {
        uint32_t v = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > v)
        {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = v;
    }

    uint32_t sum = 0;
    for (int i = BAT_SAMPLES / 4; i < BAT_SAMPLES * 3 / 4; i++)
    {
        sum += samples[i];
    }
    return sum / (BAT_SAMPLES / 2) * BAT_DIVIDER;
}

static int percentFromMilliVolts(float mv)
{
    const int n = sizeof(curve) / sizeof(curve[0]);
    if (mv <= curve[0].mv)
        return 0;
    if (mv >= curve[n - 1].mv)
        return 100;

    for (int i = 1; i < n; i++)
    {
        if (mv < curve[i].mv)
        {
            float t = (mv - curve[i - 1].mv) / (curve[i].mv - curve[i - 1].mv);
            return curve[i - 1].percent + (int)(t * (curve[i].percent - curve[i - 1].percent) + 0.5f);
        }
    }
    return 100;
}

int batteryUpdate()
{
    uint32_t mv = readMilliVolts();

    if (filteredMv == 0)
        filteredMv = mv; // first reading
    else
        filteredMv += BAT_EMA_ALPHA * ((float)mv - filteredMv);

    updateChargingTrend();

    int percent = percentFromMilliVolts(filteredMv);
    if (shownPercent < 0 || abs(percent - shownPercent) >= BAT_HYSTERESIS || percent == 0 || percent == 100)
    {
        shownPercent = percent;
    }
    return shownPercent;
}

uint32_t batteryMilliVolts()
{
    return (uint32_t)filteredMv;
}

bool batteryCharging()
{
    return charging;
}
