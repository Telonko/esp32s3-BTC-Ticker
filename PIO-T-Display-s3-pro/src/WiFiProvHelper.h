#ifndef WIFI_PROV_HELPER_H
#define WIFI_PROV_HELPER_H

#include <WiFi.h>

// Function declarations for provisioning and event handling
void SysProvEvent(arduino_event_t *sys_event);
void setupProvisioning(const char *pop, const char *service_name, const char *service_key, bool reset_provisioned);
void updateConnectionStatus(const char *status, const char *service_name, const char *pop, const char *transport);
void resetProvisioning();  // Resets Wi-Fi provisioning by erasing stored credentials

// Lower TX power = lower current peaks. Full power (19.5 dBm) causes brownout
// resets when the board switches between USB and battery power.
#define WIFI_TX_POWER WIFI_POWER_11dBm

// Puts Wi-Fi into STA mode; true if any network is known
// (from the web page or stored by provisioning)
bool wifiInit();

// Non-blocking connection manager: scans, joins the strongest known network,
// rescans when the connection is lost (also handles moving between networks).
// Call from loop(); calls onWiFiConnected() on every successful connection.
void wifiLoop();

// For the web page: network stored by provisioning ("" if none) and SSIDs
// seen by the last scan
const char *wifiProvisionedSsid();
int wifiScanResults(const char *out[], int max);

// Skips the retry delay when not connected (e.g. after the network list changed)
void wifiRetryNow();

// True while the first connection attempt after boot is still running
bool wifiFirstAttemptPending();

// Applies provisioning events to the UI; call from loop() (LVGL thread)
void processProvEvents();

// Defined in main.cpp: switches to the ticker screen and starts network services
void onWiFiConnected();

#endif // WIFI_PROV_HELPER_H
