#include "WiFiProvHelper.h"
#include <WiFi.h>
#include <DNSServer.h>

#include "ApScreen.h"
#include "Settings.h"
#include "WebConfig.h"

extern "C" {
    #include "esp_wifi.h"
}

#define MAX_KNOWN_NETWORKS (MAX_WIFI_NETWORKS + 1)
#define MAX_SCAN_RESULTS 10
// At 80 MHz joining a weak (-90 dBm) network kept failing, at 240 MHz it
// worked at once: connect at full speed, then save power
#define CPU_MHZ_CONNECTING 240
#define CPU_MHZ_CONNECTED 80
#define CONNECT_TIMEOUT_MS 15000
#define RETRY_DELAY_MS 30000
#define LOST_RESCAN_DELAY_MS 3000
// Own access point when no network could be joined for this long
#define AP_AFTER_MS (2 * 60 * 1000UL)
// Started by hand (button 1): stays up this long even when online
#define AP_MANUAL_KEEP_MS (5 * 60 * 1000UL)

struct KnownNetwork {
    char ssid[33];
    char pass[65];
};

enum WifiState { W_IDLE, W_SCANNING, W_CONNECTING, W_CONNECTED, W_WAITING };

// Network stored in the Wi-Fi driver config by the old provisioning, read
// once at init: after WiFi.begin() esp_wifi_get_config() returns the
// current (RAM) config instead.
static KnownNetwork storedNetwork = {{0}, {0}};

// SSIDs seen by the last scan, for the web page
static char lastScan[MAX_SCAN_RESULTS][33];
static int lastScanCount = 0;

// Next known network to try without seeing it in the scan (hidden SSID,
// missed beacon); reset after a full round or a successful connection
static int blindNext = 0;

static WifiState state = W_IDLE;
static unsigned long stateSince = 0;
static unsigned long waitMs = 0;
static bool firstAttemptPending = true;
static unsigned long offlineSince = 0;

// Setup access point
static DNSServer dnsServer;
static bool apActive = false;
static bool retryRequested = false; // scan even while a phone is on the AP
static unsigned long apKeepUntil = 0; // manual start: keep the AP until then
static char apSsid[20];
static char apPass[12];

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

// Networks from the web page plus the one stored in the driver config
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
    retryRequested = false;
    if (getCpuFrequencyMhz() != CPU_MHZ_CONNECTING)
        setCpuFrequencyMhz(CPU_MHZ_CONNECTING);
    Serial.println("[WIFI] Scanning...");
    WiFi.scanDelete();
    WiFi.scanNetworks(true /* async */, true /* show hidden */);
    setState(W_SCANNING);
}

static void attemptFailed(unsigned long retryMs) {
    firstAttemptPending = false;
    setState(W_WAITING, retryMs);
}

void wifiStartAp() {
    if (apActive)
        return;

    uint8_t mac[6];
    WiFi.macAddress(mac);
    snprintf(apSsid, sizeof(apSsid), "ticker-%02X%02X", mac[4], mac[5]);
    // New random password on every start: only someone who sees the screen
    // can join
    snprintf(apPass, sizeof(apPass), "%08x", esp_random());

    unsigned long t0 = millis();
    WiFi.mode(WIFI_AP_STA);
    unsigned long tMode = millis();
    WiFi.softAP(apSsid, apPass);
    WiFi.setTxPower(WIFI_TX_POWER);
    unsigned long tAp = millis();
    // Captive portal: every DNS name points to us, phones open the page
    dnsServer.start(53, "*", WiFi.softAPIP());
    apActive = true;

    String ip = WiFi.softAPIP().toString();
    apScreenShow(apSsid, apPass, ip.c_str());
    unsigned long tQr = millis();
    webConfigBegin();
    Serial.printf("[WIFI] Setup access point %s started, http://%s (mode %lu ms, AP %lu ms, QR %lu ms, web %lu ms)\n",
                  apSsid, ip.c_str(), tMode - t0, tAp - tMode, tQr - tAp, millis() - tQr);
}

static void stopAp() {
    dnsServer.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    apActive = false;
    Serial.println("[WIFI] Setup access point stopped");
}

bool wifiApActive() {
    return apActive;
}

bool wifiShowSetupScreen() {
    return apActive && (WiFi.status() != WL_CONNECTED || (long)(millis() - apKeepUntil) < 0);
}

void wifiCloseSetup() {
    apKeepUntil = millis(); // closes once no phone is connected
}

bool wifiInit() {
    // Connecting to networks from the web page must not overwrite the
    // network stored in the driver config
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false); // wifiLoop() reconnects (and can switch networks)
    offlineSince = millis();

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

    if (count > 0) {
        startScan();
    } else {
        firstAttemptPending = false;
        wifiStartAp();
    }
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
    retryRequested = true;
    if (state == W_WAITING || state == W_IDLE)
        startScan();
}

bool wifiFirstAttemptPending() {
    return firstAttemptPending;
}

void wifiLoop() {
    if (apActive) {
        dnsServer.processNextRequest();

        // Online again and nobody is using the setup network: close it
        if (state == W_CONNECTED && WiFi.softAPgetStationNum() == 0 && (long)(millis() - apKeepUntil) >= 0)
            stopAp();
    } else if (state != W_CONNECTED && millis() - offlineSince > AP_AFTER_MS) {
        Serial.printf("[WIFI] Offline for %lu s\n", AP_AFTER_MS / 1000);
        wifiStartAp();
    }

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

        for (int i = 0; i < found; i++) {
            Serial.printf("[WIFI]   seen \"%s\" %d dBm ch %d\n", WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i));
        }

        // Strongest hidden AP (empty SSID): joining it by BSSID and channel
        // works where a probe for the hidden SSID gets no answer
        int hidden = -1;
        for (int i = 0; i < found; i++) {
            if (WiFi.SSID(i).length() == 0 && (hidden < 0 || WiFi.RSSI(i) > WiFi.RSSI(hidden)))
                hidden = i;
        }
        uint8_t hiddenBssid[6];
        int32_t hiddenChannel = 0;
        if (hidden >= 0) {
            memcpy(hiddenBssid, WiFi.BSSID(hidden), 6);
            hiddenChannel = WiFi.channel(hidden);
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
            // Not in the scan: still try each known network, as a hidden one
            // (via the strongest hidden AP) or directly (missed beacon)
            if (blindNext < count) {
                best = blindNext++;
                if (hidden >= 0) {
                    Serial.printf("[WIFI] %s not seen in scan, trying via hidden AP %02X:%02X:%02X:%02X:%02X:%02X ch %d\n",
                                  known[best].ssid, hiddenBssid[0], hiddenBssid[1], hiddenBssid[2], hiddenBssid[3],
                                  hiddenBssid[4], hiddenBssid[5], hiddenChannel);
                    WiFi.begin(known[best].ssid, known[best].pass, hiddenChannel, hiddenBssid);
                } else {
                    Serial.printf("[WIFI] %s not seen in scan, trying anyway\n", known[best].ssid);
                    WiFi.begin(known[best].ssid, known[best].pass);
                }
                WiFi.setTxPower(WIFI_TX_POWER);
                setState(W_CONNECTING);
                break;
            }
            blindNext = 0;
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
            blindNext = 0;
            setState(W_CONNECTED);
            setCpuFrequencyMhz(CPU_MHZ_CONNECTED);
            onWiFiConnected();
        } else if (millis() - stateSince > CONNECT_TIMEOUT_MS) {
            Serial.printf("[WIFI] Connection timeout (status %d)\n", WiFi.status());
            WiFi.disconnect();
            // Try the next network right away while a blind round is running
            attemptFailed(blindNext ? 500 : 5000);
        }
        break;

    case W_CONNECTED:
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WIFI] Connection lost");
            offlineSince = millis();
            setState(W_WAITING, LOST_RESCAN_DELAY_MS);
        }
        break;

    case W_WAITING:
        // Joining a network moves the AP to that network's channel and drops
        // the phone: wait while someone is on the setup page, unless the
        // page asked to retry (network list saved)
        if (apActive && WiFi.softAPgetStationNum() > 0 && !retryRequested)
            break;
        if (millis() - stateSince >= waitMs || retryRequested)
            startScan();
        break;
    }
}

void wifiSetupMode() {
    unsigned long t0 = millis();
    webConfigResetPassword();
    Serial.printf("[WIFI] Setup mode: page password reset (%lu ms), access point on\n", millis() - t0);
    apKeepUntil = millis() + AP_MANUAL_KEEP_MS;
    wifiStartAp();
}
