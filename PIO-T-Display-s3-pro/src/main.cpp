#include <Arduino.h>
#include <lvgl.h>
#include <LV_Helper.h>
#include <LilyGo_AMOLED.h>
#include <ui.h>

#include "TimeHelper.h"
#include "WiFiProvHelper.h"
#include "WiFiProvScreen.h"
#include "BinanceWebSocket.h"
#include "pin_config.h"
#include "handleButtons.h"

// Define display and touch hardware specifics
LilyGo_Class amoled;

// Example Wi-Fi provisioning QR code data (to be generated dynamically)
const char *pop = "12345678";               // Proof of possession
const char *service_name = "crypto_ticker"; // Name of your device

void updatePriceUI(float btcRate, float highRate, float lowRate);
void SysProvEvent(arduino_event_t *sys_event);
void toggleScreenRotation();

void setup()
{
    Serial.begin(115200);
    delay(1000); // Give some time for the Serial Monitor to initialize

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

    // Show the loading screen
    lv_scr_load(ui_loading);

    // Non-blocking delay to show the loading screen for 5 seconds
    unsigned long loadingStart = millis();
    while (millis() - loadingStart < 5000)
    {
        lv_task_handler(); // Handle LVGL tasks to update the screen
        delay(5);          // Short delay to prevent overwhelming the CPU
    }

    // Check if Wi-Fi is already provisioned
    if (isProvisioned())
    {
        lv_obj_del(ui_wifiProv); // Clean up the provisioning UI if it was created
        Serial.println("[DEBUG] Wi-Fi is already provisioned.");

        // Connect to Wi-Fi with saved credentials
        if (WiFi.status() != WL_CONNECTED)
        {
            WiFi.begin();

            // Wait for the connection for up to 10 seconds
            unsigned long startAttemptTime = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
            {
                lv_task_handler(); // Handle LVGL tasks to update the screen
                delay(100);        // Wait 100ms between checks
            }
        }

        if (WiFi.status() == WL_CONNECTED)
        {
            Serial.println("[DEBUG] Connected to Wi-Fi. Starting background tasks.");
            initiateNTPTimeSync(); // Start NTP sync

            // Initialize Binance WebSocket to get real-time Bitcoin data
            initBinanceWebSocket();

            lv_scr_load(ui_ticker); // Load the main screen (ui_crypto)
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

    // Update time and date regularly from NTP (e.g., every second)
    static unsigned long lastTimeUpdate = 0;
    if (millis() - lastTimeUpdate > 1000)
    {
        updateTimeAndDate();
        lastTimeUpdate = millis();
    }

    // Handle WebSocket communication
    handleBinanceWebSocket();

    handleButton1();
    handleButton2();
}

// Function to update Bitcoin-related UI elements
void updatePriceUI(float btcRate, float highRate, float lowRate)
{
    // Update Bitcoin Rate with two decimal places (e.g., 12345.67)
    lv_label_set_text_fmt(ui_Label_Price_Rate, "%.2f", btcRate);

    // Update High and Low Rates (no "High:" or "Low:" prefixes, just the price)
    lv_label_set_text_fmt(ui_LabelPricehigh, "%.2f", highRate);
    lv_label_set_text_fmt(ui_labelPriceLow, "%.2f", lowRate);
}

// Updates time and date labels on the display
void updateTimeAndDate()
{
    // Get the current time from the synchronized NTP time
    time_t now = time(nullptr);
    struct tm *timeInfo = localtime(&now);

    // Format the time as 12-hour with AM/PM
    char timeStr[10];
    strftime(timeStr, sizeof(timeStr), "%H:%M", timeInfo);

    // Format the date as "04-09-24" (date-month-year with last 2 digits of the year)
    char dateStr[12];
    strftime(dateStr, sizeof(dateStr), "%d-%m-%y", timeInfo);

    // Update UI labels (assuming these labels are defined in the `ui.h`)
    lv_label_set_text(ui_Label_time, timeStr); // Update time label on eth screen
    lv_label_set_text(ui_Label_date, dateStr); // Update date label on eth screen
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