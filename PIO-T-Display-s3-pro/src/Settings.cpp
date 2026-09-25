#include "Settings.h"
#include <Arduino.h>
#include <Preferences.h>

#define SETTINGS_NAMESPACE "ticker"
#define SETTINGS_KEY "settings"
#define SETTINGS_VERSION 1
#define WIFI_KEY "wifi"

Settings settings;
WifiList wifiList;

// Stored as one blob; the version guards against layout changes
struct StoredSettings
{
    uint32_t version;
    Settings data;
};

static void setDefaults()
{
    memset(&settings, 0, sizeof(settings));
    const char *defaults[] = {"ltc", "eth", "btc"};
    settings.tickerCount = 3;
    for (int i = 0; i < 3; i++)
        strlcpy(settings.tickers[i], defaults[i], TICKER_LEN);
    settings.nightStartHour = 23;
    settings.nightEndHour = 7;
    settings.nightBrightness = 15;
}

void settingsLoad()
{
    setDefaults();

    Preferences prefs;
    if (!prefs.begin(SETTINGS_NAMESPACE, true))
        return; // nothing stored yet

    StoredSettings stored;
    if (prefs.getBytesLength(SETTINGS_KEY) == sizeof(stored) &&
        prefs.getBytes(SETTINGS_KEY, &stored, sizeof(stored)) == sizeof(stored) &&
        stored.version == SETTINGS_VERSION &&
        stored.data.tickerCount > 0 && stored.data.tickerCount <= MAX_TICKERS)
    {
        settings = stored.data;
    }

    memset(&wifiList, 0, sizeof(wifiList));
    if (prefs.getBytesLength(WIFI_KEY) == sizeof(wifiList))
    {
        prefs.getBytes(WIFI_KEY, &wifiList, sizeof(wifiList));
        if (wifiList.count > MAX_WIFI_NETWORKS)
            memset(&wifiList, 0, sizeof(wifiList));
    }
    prefs.end();
}

void wifiListSave()
{
    Preferences prefs;
    prefs.begin(SETTINGS_NAMESPACE, false);
    prefs.putBytes(WIFI_KEY, &wifiList, sizeof(wifiList));
    prefs.end();
}

void settingsSave()
{
    StoredSettings stored;
    stored.version = SETTINGS_VERSION;
    stored.data = settings;

    Preferences prefs;
    prefs.begin(SETTINGS_NAMESPACE, false);
    prefs.putBytes(SETTINGS_KEY, &stored, sizeof(stored));
    prefs.end();
}

bool settingsNormalizeTicker(const char *in, char *out)
{
    size_t len = 0;
    for (; in[len]; len++)
    {
        char c = tolower((unsigned char)in[len]);
        if (len >= TICKER_LEN - 1 || !(isdigit((unsigned char)c) || (c >= 'a' && c <= 'z')))
            return false;
        out[len] = c;
    }
    out[len] = 0;
    return len >= 2;
}
