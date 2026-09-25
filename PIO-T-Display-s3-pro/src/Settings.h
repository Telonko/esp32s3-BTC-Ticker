#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

#define MAX_TICKERS 8
#define TICKER_LEN 12 // "doge" etc., without the "usdt" quote asset

struct PriceAlert
{
    double above; // 0 = off
    double below; // 0 = off
};

struct Settings
{
    uint8_t tickerCount;
    char tickers[MAX_TICKERS][TICKER_LEN];
    PriceAlert alerts[MAX_TICKERS];
    uint8_t nightStartHour;  // night mode is off when start == end
    uint8_t nightEndHour;
    uint8_t nightBrightness; // 1..255
};

#define MAX_WIFI_NETWORKS 5

struct WifiNetwork
{
    char ssid[33];
    char pass[65];
    uint8_t brightness; // screen brightness on this network, 0 = default
};

// Networks edited on the web page (plus the one stored by provisioning)
struct WifiList
{
    uint8_t count;
    WifiNetwork networks[MAX_WIFI_NETWORKS];
};

// Owned by the UI thread (loop()); the network task gets its own copy of
// the ticker list via wsPublishStreams().
extern Settings settings;
extern WifiList wifiList;

// Loads both settings and wifiList
void settingsLoad();
void settingsSave();
void wifiListSave();

// Lowercases and checks a ticker name ([a-z0-9], 2..TICKER_LEN-1 chars)
bool settingsNormalizeTicker(const char *in, char *out);

#endif // SETTINGS_H
