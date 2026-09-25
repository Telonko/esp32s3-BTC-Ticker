#include "WiFiProvHelper.h"
#include <WiFiProv.h>
#include <WiFi.h>

#include "ui.h"
#include "TimeHelper.h"
#include "BinanceWebSocket.h"  // Include the WebSocket header

#if __has_include("secrets.h")
    #include "secrets.h"
#endif

extern "C" {
    #include "esp_wifi.h"
}

extern const char *pop;
extern const char *service_name;

// Set from the Wi-Fi event task, consumed in loop(): LVGL must not be touched
// from the event task, and blocking there stalls the whole Wi-Fi stack.
static volatile bool pendingProvStart = false;
static volatile bool pendingGotIp = false;

void SysProvEvent(arduino_event_t *sys_event) {
    switch (sys_event->event_id) {
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[DEBUG] Wi-Fi connected successfully. IP: %s\n", WiFi.localIP().toString().c_str());
            pendingGotIp = true;
            break;

        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[DEBUG] Disconnected from Wi-Fi.");
            break;

        case ARDUINO_EVENT_PROV_START:
            Serial.println("[DEBUG] Provisioning started. Please provide Wi-Fi credentials.");
            pendingProvStart = true;
            break;

        case ARDUINO_EVENT_PROV_END:
            Serial.println("[DEBUG] Provisioning process completed.");
            break;

        default:
            break;
    }
}

void processProvEvents() {
    if (pendingProvStart) {
        pendingProvStart = false;
        lv_scr_load(ui_wifiProv);
        updateConnectionStatus("Use ESP SoftAP Prov App to provision", service_name, pop, "softap");
    }
    if (pendingGotIp) {
        pendingGotIp = false;
        onWiFiConnected();
    }
}

bool isProvisioned() {
    wifi_config_t conf;
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &conf);

    // Add debug information
    Serial.printf("[DEBUG] esp_wifi_get_config return value: %d\n", ret);
    Serial.printf("[DEBUG] Stored SSID: %s\n", (const char *)conf.sta.ssid);

    if (ret == ESP_OK && strlen((const char *)conf.sta.ssid) > 0) {
        return true;
    } else if (ret >= ESP_ERR_WIFI_BASE) {
        #if defined WIFI_SSID
        if (!connectToNetwork(WIFI_SSID, WIFI_PASSWORD)) {
            #if defined WIFI_SSID_OFFICE
                return connectToNetwork(WIFI_SSID_OFFICE, WIFI_PASSWORD_OFFICE);
            #else
                return false;
            #endif
        } else {
            return true;
        }
        #endif
    }
    return false;
}

bool connectToNetwork(const char *ssid, const char *pwd) {
    Serial.printf("Connecting to WiFi %s\n", ssid);

    WiFi.begin(ssid, pwd);
    // Wait for the connection for up to 10 seconds
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
        lv_task_handler();  // Handle LVGL tasks to update the screen
        delay(100);  // Wait 100ms between checks
    }

    return WiFi.status() == WL_CONNECTED;
}

void setupProvisioning(const char *pop, const char *service_name, const char *service_key, bool reset_provisioned) {
    WiFi.onEvent(SysProvEvent);

    // Start Wi-Fi provisioning using SoftAP
    WiFiProv.beginProvision(
        WIFI_PROV_SCHEME_SOFTAP, WIFI_PROV_SCHEME_HANDLER_NONE,
        WIFI_PROV_SECURITY_1, pop, service_name, service_key, nullptr, reset_provisioned
    );
}

void resetProvisioning() {
    Serial.println("Resetting Wi-Fi credentials...");
    esp_wifi_restore();  // Restore Wi-Fi to default factory settings
    ESP.restart();       // Restart device to begin provisioning again
}
