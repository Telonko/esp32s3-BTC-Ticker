#include <Arduino.h>
#include <lvgl.h>
#include <LV_Helper.h>
#include <LilyGo_AMOLED.h>
#include <ui.h>
#include <Preferences.h>
#include <esp_system.h>

#include "TimeHelper.h"
#include "WiFiProvHelper.h"
#include "WiFiProvScreen.h"
#include "BinanceWebSocket.h"
#include "pin_config.h"
#include "handleButtons.h"
#include "Battery.h"

#if __has_include("secrets.h")
    #include "secrets.h"
#endif

// Define display and touch hardware specifics
LilyGo_Class amoled;

// Example Wi-Fi provisioning QR code data (to be generated dynamically)
const char *pop = "12345678";               // Proof of possession
const char *service_name = "crypto_ticker"; // Name of your device
const uint8_t home_brighest = 50;

void SysProvEvent(arduino_event_t *sys_event);
void toggleScreenRotation();

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

    // Initialize the Wi-Fi provisioning screen UI
    ui_wifiProv_screen_init();

    // Show the QR code in the container after initializing the screen
    showQRCodeInContainer(service_name, pop);

    // Show the loading screen; it stays up while Wi-Fi is connecting
    lv_scr_load(ui_loading);
    lv_task_handler();

    // Check if Wi-Fi is already provisioned
    if (isProvisioned())
    {
        Serial.println("[DEBUG] Wi-Fi is already provisioned.");

        // Connect to Wi-Fi with saved credentials
        if (WiFi.status() != WL_CONNECTED)
        {
            WiFi.begin();
            WiFi.setTxPower(WIFI_TX_POWER);

            // Wait for the connection for up to 10 seconds
            unsigned long startAttemptTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
            {
                lv_task_handler(); // Handle LVGL tasks to update the screen
                delay(20);
            }
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("[DEBUG] Connected to Wi-Fi. Starting background tasks.");
            onWiFiConnected();
            lv_obj_del(ui_wifiProv); // Provisioning UI is not needed anymore
            ui_wifiProv = NULL;
        }
        else
        {
            Serial.println("[ERROR] Failed to connect to Wi-Fi.");
            setupProvisioning(pop, service_name, NULL, false);
            lv_scr_load(ui_wifiProv); // Load the Wi-Fi provisioning screen
        }
    }
    else
    {
        Serial.println("[DEBUG] Device is not provisioned. Starting provisioning...");
        setupProvisioning(pop, service_name, NULL, true);
    }

    // Set up the reset pin
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);
}

void loop()
{
    // Handle LVGL tasks
    lv_task_handler();
    delay(5);

    processProvEvents();

    // Update time, battery and Wi-Fi icon once per second; labels are redrawn only on change
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 1000)
    {
        updateTimeAndDate();
        lastTimeUpdate = millis();

        static int shownBattery = -1;
        int battery = batteryUpdate();
        if (battery != shownBattery)
        {
            shownBattery = battery;
            lv_label_set_text_fmt(ui_Label_Battary, "%d%%", battery);
        }

        static unsigned long lastBatteryLog = 0;
        if (lastBatteryLog == 0 || millis() - lastBatteryLog > 30000)
        {
            lastBatteryLog = millis();
            Serial.printf("[BAT] %u mV, %d%%\n", batteryMilliVolts(), battery);
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

    handleButton1();
    handleButton2();
}

// Function to update Bitcoin-related UI elements
void updatePriceUI(double btcRate, double highRate, double lowRate)
{
    // Update Bitcoin Rate with two decimal places (e.g., 12345.67)
    lv_label_set_text_fmt(ui_Label_Price_Rate, "%.2f", btcRate);

    // Update High and Low Rates (no "High:" or "Low:" prefixes, just the price)
    lv_label_set_text_fmt(ui_LabelPricehigh, "%.2f", highRate);
    lv_label_set_text_fmt(ui_labelPriceLow, "%.2f", lowRate);
}

// Called once Wi-Fi is up (at boot or after provisioning / reconnect)
void onWiFiConnected()
{
    static bool started = false;
    if (started)
        return; // Wi-Fi reconnects are handled by the network task

    started = true;
#ifdef WIFI_SSID
    if (WiFi.SSID() == WIFI_SSID)
    {
        amoled.setBrightness(home_brighest);
    }
#endif
    initiateNTPTimeSync();  // Non-blocking
    initBinanceWebSocket(); // Starts the network task
    setTickerInfo();
    lv_scr_load(ui_ticker);
}

void toggleScreenRotation()
{
    switch (amoled.getRotation())
    {
    case 0:
        amoled.setRotation(2);
        break;

    default:
        amoled.setRotation(0);
        break;
    }
}