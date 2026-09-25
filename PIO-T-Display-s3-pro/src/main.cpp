#include <Arduino.h>
#include <lvgl.h>
#include <LV_Helper.h>
#include <LilyGo_AMOLED.h>
#include <ui.h>
#include <Preferences.h>
#include <esp_system.h>

#include "TimeHelper.h"
#include "WiFiProvHelper.h"
#include "ApScreen.h"
#include "BinanceWebSocket.h"
#include "pin_config.h"
#include "handleButtons.h"
#include "Battery.h"
#include "Settings.h"
#include "Alerts.h"
#include "WebConfig.h"
#include "ChartView.h"
#include "IconStore.h"

// Define display and touch hardware specifics
LilyGo_Class amoled;

const uint8_t default_brightness = 100;
const uint8_t low_battery_brightness = 20;
const int low_battery_percent = 10;

// Burn-in protection: every PIXEL_SHIFT_INTERVAL_MS the whole ticker screen
// moves to the next offset (the background has a black border, so the edge
// never shows).
#define PIXEL_SHIFT_INTERVAL_MS (3 * 60 * 1000UL)
#define PIXEL_SHIFT_PX 2

static int lastBatteryPercent = 100;

void toggleScreenRotation();
void startApp();
void syncScreen();
void updateBrightness();
void pixelShiftStep();

static const char *resetReasonName(uint8_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
    }
}

// Keeps the last reset reasons in NVS: resets on battery happen without a
// serial monitor attached, so they can only be inspected on a later boot.
static void logResetReason()
{
    const size_t historySize = 10;
    uint8_t history[historySize] = {0}; // reason + 1, 0 marks an empty slot

    Preferences prefs;
    prefs.begin("diag", false);
    if (prefs.isKey("resets"))
        prefs.getBytes("resets", history, historySize);
    memmove(history + 1, history, historySize - 1);
    history[0] = (uint8_t)esp_reset_reason() + 1;
    prefs.putBytes("resets", history, historySize);
    uint32_t boots = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", boots);
    prefs.end();

    Serial.printf("[DIAG] Boot #%u, reset reasons (newest first):", boots);
    for (size_t i = 0; i < historySize && history[i]; i++)
    {
        Serial.printf(" %s", resetReasonName(history[i] - 1));
    }
    Serial.println();
}

void setup()
{
    // Full speed while connecting; wifiLoop() drops to 80 MHz once connected
    setCpuFrequencyMhz(240);

    Serial.begin(115200);
    // Without a USB host, writes to USB-CDC would block for the TX timeout
    Serial.setTxTimeoutMs(0);
    delay(1000); // Give some time for the Serial Monitor to initialize
    logResetReason();

    // Initialize AMOLED Display
    if (!amoled.begin())
    {
        Serial.println("AMOLED init failed");
        while (1)
            delay(1000);
    }

    amoled.setRotation(2);     // Portrait mode. USB port on the right.
    amoled.setBrightness(100); // Set brightness adjustable range 0 ~ 255

    // Initialize LVGL
    beginLvglHelper(amoled);
    ui_init();

    apScreenInit();
    settingsLoad();
    iconStoreBegin();

    // Show the loading screen; it stays up while Wi-Fi is connecting
    lv_scr_load(ui_loading);
    lv_task_handler();

    // Known networks: wait for the first attempt (scan + connect) while
    // keeping the loading animation alive. Without any, wifiInit() starts the
    // setup access point. wifiLoop() keeps trying in the background.
    if (wifiInit())
    {
        while (wifiFirstAttemptPending())
        {
            wifiLoop();
            lv_task_handler();
            delay(20);
        }
    }
    startApp();

    // Set up the reset pin
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
}

void loop()
{
    // A long iteration freezes the screen: log it
    static unsigned long lastLoop = 0;
    unsigned long loopGap = millis() - lastLoop;
    if (lastLoop && loopGap > 300)
        Serial.printf("[UI] loop stalled for %lu ms\n", loopGap);
    lastLoop = millis();

    // Handle LVGL tasks
    lv_task_handler();
    delay(5);

    wifiLoop();
    syncScreen();
    webConfigLoop();
    alertLoop();

    // Update time, battery and Wi-Fi icon once per second; labels are redrawn only on change
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 1000)
    {
        updateTimeAndDate();
        lastTimeUpdate = millis();

        static int shownBattery = -1;
        static int8_t shownCharging = -1;
        int battery = batteryUpdate();
        int8_t charging = batteryCharging() ? 1 : 0;
        lastBatteryPercent = battery;
        if (battery != shownBattery || charging != shownCharging)
        {
            shownBattery = battery;
            shownCharging = charging;
            if (charging)
                lv_label_set_text_fmt(ui_Label_Battary, LV_SYMBOL_CHARGE " %d%%", battery);
            else
                lv_label_set_text_fmt(ui_Label_Battary, "%d%%", battery);
        }

        updateBrightness();

        static unsigned long lastBatteryLog = 0;
        if (lastBatteryLog == 0 || millis() - lastBatteryLog > 30000)
        {
            lastBatteryLog = millis();
            Serial.printf("[BAT] %u mV, %d%%%s\n", batteryMilliVolts(), battery, charging ? ", charging" : "");
        }

        static int8_t shownWifi = -1;
        int8_t wifi = WiFi.status() == WL_CONNECTED ? 1 : 0;
        if (wifi != shownWifi)
        {
            shownWifi = wifi;
            if (wifi)
                lv_obj_add_flag(ui_no_wifi, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_clear_flag(ui_no_wifi, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Push fresh prices / connection state from the network task to the UI
    handleBinanceWebSocket();
    chartViewLoop();

    static unsigned long lastPixelShift = 0;
    if (millis() - lastPixelShift > PIXEL_SHIFT_INTERVAL_MS)
    {
        lastPixelShift = millis();
        pixelShiftStep();
    }

    handleButton1();
    handleButton2();
}

// Function to update Bitcoin-related UI elements
void updatePriceUI(double btcRate, double highRate, double lowRate)
{
    if (btcRate <= 0)
    {
        // No data yet for this ticker
        lv_label_set_text(ui_Label_Price_Rate, "--");
        lv_label_set_text(ui_LabelPricehigh, "--");
        lv_label_set_text(ui_labelPriceLow, "--");
        return;
    }

    // Cheap coins (DOGE, SHIB...) need more decimals
    const char *format = btcRate >= 1 ? "%.2f" : btcRate >= 0.01 ? "%.4f" : "%.8f";
    lv_label_set_text_fmt(ui_Label_Price_Rate, format, btcRate);

    // Update High and Low Rates (no "High:" or "Low:" prefixes, just the price)
    lv_label_set_text_fmt(ui_LabelPricehigh, format, highRate);
    lv_label_set_text_fmt(ui_labelPriceLow, format, lowRate);
}

static bool isNightTime()
{
    int hour = localHour();
    uint8_t start = settings.nightStartHour, end = settings.nightEndHour;
    if (hour < 0 || start == end)
        return false;
    if (start < end)
        return hour >= start && hour < end;
    return hour >= start || hour < end; // e.g. 23..7 crosses midnight
}

// Brightness = the lowest of: location default, night mode, low battery
void updateBrightness()
{
    uint8_t brightness = default_brightness;
    if (WiFi.status() == WL_CONNECTED)
    {
        String ssid = WiFi.SSID();
        for (int i = 0; i < wifiList.count; i++)
        {
            if (ssid == wifiList.networks[i].ssid && wifiList.networks[i].brightness)
                brightness = wifiList.networks[i].brightness;
        }
    }
    if (isNightTime())
        brightness = min(brightness, settings.nightBrightness);
    if (lastBatteryPercent <= low_battery_percent && !batteryCharging())
        brightness = min(brightness, low_battery_brightness);

    if (brightness != amoled.getBrightness())
    {
        Serial.printf("[UI] Brightness %u\n", brightness);
        amoled.setBrightness(brightness);
    }
}

void pixelShiftStep()
{
    static const int8_t offsets[][2] = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1},
    };
    static uint8_t step = 0;
    step = (step + 1) % (sizeof(offsets) / sizeof(offsets[0]));

    lv_coord_t dx = offsets[step][0] * PIXEL_SHIFT_PX;
    lv_coord_t dy = offsets[step][1] * PIXEL_SHIFT_PX;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(ui_ticker); i++)
    {
        lv_obj_t *child = lv_obj_get_child(ui_ticker, i);
        lv_obj_set_style_translate_x(child, dx, 0);
        lv_obj_set_style_translate_y(child, dy, 0);
    }
}

// Shows the ticker screen and starts background services (once).
// Works without Wi-Fi too: the network task waits for a connection.
void startApp()
{
    static bool started = false;
    if (started)
        return;

    started = true;
    initiateNTPTimeSync();  // Non-blocking
    initBinanceWebSocket(); // Starts the network task
    chartViewInit();
    iconViewInit();
    setTickerInfo();
    updateBrightness();
    lv_scr_load(ui_ticker);
}

// Setup screen while the access point is up and there is no connection,
// the ticker otherwise
void syncScreen()
{
    lv_obj_t *wanted = wifiShowSetupScreen() ? apScreen() : ui_ticker;
    if (lv_scr_act() != wanted)
        lv_scr_load(wanted);
}

// Called on every Wi-Fi connection (boot or reconnect)
void onWiFiConnected()
{
    startApp();
    webConfigBegin(); // once
    showIpAddress(10000); // where to find the settings page
}

void toggleScreenRotation()
{
    // Rotation only changes the panel scan direction (MADCTL); the panel RAM
    // still holds the old frame, which then shows up flipped. Hide the panel,
    // redraw the whole screen right away, then restore brightness.
    uint8_t brightness = amoled.getBrightness();
    amoled.setBrightness(0);

    amoled.setRotation(amoled.getRotation() == 0 ? 2 : 0);

    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);

    amoled.setBrightness(brightness);
}