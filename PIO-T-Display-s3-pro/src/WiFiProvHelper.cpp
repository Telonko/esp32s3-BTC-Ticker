#include "WiFiProvHelper.h"
#include <WiFiProv.h>
#include <WiFi.h>

#include "ui.h"
#include "WiFiProvScreen.h"
#include "Settings.h"
#include "WebConfig.h"

extern "C" {
    #include "esp_wifi.h"
}

extern const char *pop;
extern const char *service_name;

#define MAX_KNOWN_NETWORKS (MAX_WIFI_NETWORKS + 1)
#define MAX_SCAN_RESULTS 10
#define CONNECT_TIMEOUT_MS 15000
#define RETRY_DELAY_MS 30000
#define LOST_RESCAN_DELAY_MS 3000

struct KnownNetwork {
    char ssid[33];
    char pass[65];
};

enum WifiState { W_IDLE, W_SCANNING, W_CONNECTING, W_CONNECTED, W_WAITING };

// Network stored by provisioning, read once at init: after WiFi.begin()
// esp_wifi_get_config() returns the current (RAM) config instead.
static KnownNetwork storedNetwork = {{0}, {0}};

// SSIDs seen by the last scan, for the web page
static char lastScan[MAX_SCAN_RESULTS][33];
static int lastScanCount = 0;

static WifiState state = W_IDLE;
static unsigned long stateSince = 0;
static unsigned long waitMs = 0;
static bool firstAttemptPending = true;

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

static void addKnown(KnownNetwork *list, int &count, const char *ssid, const char *pass) {
    if (!ssid || !ssid[0] || count >= MAX_KNOWN_NETWORKS)
        return;
    for (int i = 0; i < count; i++) {
        if (strcmp(list[i].ssid, ssid) == 0)
            return;
    }
    strlcpy(list[count].ssid, ssid, sizeof(list[count].ssid));
    strlcpy(list[count].pass, pass ? pass : "", sizeof(list[count].pass));
    count++;
}

// Networks from the web page plus the one stored by provisioning
static int loadKnownNetworks(KnownNetwork *list) {
    int count = 0;
    for (int i = 0; i < wifiList.count; i++)
        addKnown(list, count, wifiList.networks[i].ssid, wifiList.networks[i].pass);

    addKnown(list, count, storedNetwork.ssid, storedNetwork.pass);
    return count;
}

static void setState(WifiState next, unsigned long wait = 0) {
    state = next;
    stateSince = millis();
    waitMs = wait;
}

static void startScan() {
    Serial.println("[WIFI] Scanning...");
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */);
    setState(W_SCANNING);
}

static void attemptFailed(unsigned long retryMs) {
    firstAttemptPending = false;
    setState(W_WAITING, retryMs);
}

bool wifiInit() {
    // Connecting to networks from the web page must not overwrite the network
    // stored by provisioning; only the provisioning manager writes to flash.
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // wifiLoop() reconnects (and can switch networks)

    wifi_config_t conf;
    if (esp_wifi_get_config(WIFI_IF_STA, &conf) == ESP_OK) {
        memcpy(storedNetwork.ssid, conf.sta.ssid, sizeof(conf.sta.ssid));
        memcpy(storedNetwork.pass, conf.sta.password, sizeof(conf.sta.password));
    }

    KnownNetwork known[MAX_KNOWN_NETWORKS];
    int count = loadKnownNetworks(known);
    Serial.printf("[WIFI] %d known network(s)\n", count);
    for (int i = 0; i < count; i++)
        Serial.printf("[WIFI]   %s\n", known[i].ssid);

    if (count > 0)
        startScan();
    else
        firstAttemptPending = false;
    return count > 0;
}

const char *wifiProvisionedSsid() {
    return storedNetwork.ssid;
}

int wifiScanResults(const char *out[], int max) {
    int count = 0;
    for (; count < lastScanCount && count < max; count++)
        out[count] = lastScan[count];
    return count;
}

void wifiRetryNow() {
    if (state == W_WAITING)
        startScan();
}

bool wifiFirstAttemptPending() {
    return firstAttemptPending;
}

void wifiLoop() {
    switch (state) {
    case W_IDLE:
        break;

    case W_SCANNING: {
        int found = WiFi.scanComplete();
        if (found == WIFI_SCAN_RUNNING)
            break;
        if (found < 0) {
            Serial.println("[WIFI] Scan failed");
            attemptFailed(5000);
            break;
        }

        KnownNetwork known[MAX_KNOWN_NETWORKS];
        int count = loadKnownNetworks(known);
        lastScanCount = 0;
        for (int i = 0; i < found && lastScanCount < MAX_SCAN_RESULTS; i++) {
            String ssid = WiFi.SSID(i);
            bool seen = ssid.length() == 0;
            for (int j = 0; j < lastScanCount && !seen; j++)
                seen = ssid == lastScan[j];
            if (!seen)
                strlcpy(lastScan[lastScanCount++], ssid.c_str(), sizeof(lastScan[0]));
        }

        int best = -1;
        int bestRssi = -1000;
        for (int i = 0; i < found; i++) {
            for (int k = 0; k < count; k++) {
                if (WiFi.SSID(i) == known[k].ssid && WiFi.RSSI(i) > bestRssi) {
                    best = k;
                    bestRssi = WiFi.RSSI(i);
                }
            }
        }
        WiFi.scanDelete();

        if (best < 0) {
            Serial.printf("[WIFI] No known network in range (%d found), retry in %d s\n", found, RETRY_DELAY_MS / 1000);
            attemptFailed(RETRY_DELAY_MS);
            break;
        }

        Serial.printf("[WIFI] Connecting to %s (%d dBm)\n", known[best].ssid, bestRssi);
        WiFi.begin(known[best].ssid, known[best].pass);
        WiFi.setTxPower(WIFI_TX_POWER);
        setState(W_CONNECTING);
        break;
    }

    case W_CONNECTING:
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("[WIFI] Connected to %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
            firstAttemptPending = false;
            setState(W_CONNECTED);
            onWiFiConnected();
        } else if (millis() - stateSince > CONNECT_TIMEOUT_MS) {
            Serial.println("[WIFI] Connection timeout");
            WiFi.disconnect();
            attemptFailed(5000);
        }
        break;

    case W_CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WIFI] Connection lost");
            setState(W_WAITING, LOST_RESCAN_DELAY_MS);
        }
        break;

    case W_WAITING:
        if (millis() - stateSince >= waitMs)
            startScan();
        break;
    }
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
    Serial.println("Resetting Wi-Fi credentials and web page password...");
    webConfigResetPassword();
    esp_wifi_restore();  // Restore Wi-Fi to default factory settings
    ESP.restart();       // Restart device to begin provisioning again
}
