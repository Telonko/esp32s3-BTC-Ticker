#ifndef WIFI_PROV_HELPER_H
#define WIFI_PROV_HELPER_H

#include <WiFi.h>

// Lower TX power = lower current peaks. Full power (19.5 dBm) causes brownout
// resets when the board switches between USB and battery power.
#define WIFI_TX_POWER WIFI_POWER_11dBm

// Puts Wi-Fi into STA mode; true if any network is known (from the web page
// or stored in the driver config). With none, the setup access point starts.
bool wifiInit();

// Non-blocking connection manager: scans, joins the strongest known network
// (hidden ones too), rescans when the connection is lost (also handles moving
// between networks) and starts the setup access point after 2 minutes
// offline. Call from loop(); calls onWiFiConnected() on every connection.
void wifiLoop();

// Setup access point "ticker-XXXX" with a random password shown on screen;
// the settings page is served there (captive portal)
void wifiStartAp();
bool wifiApActive();

// True while the setup screen (QR code) should be shown: access point up and
// offline, or started by hand less than 5 minutes ago
bool wifiShowSetupScreen();

// Closes a hand-started access point early (once no phone is connected)
void wifiCloseSetup();

// Button 1 hold: resets the page password and starts the access point for 5 min
void wifiSetupMode();

// For the web page: network stored in the driver config ("" if none) and
// SSIDs seen by the last scan
const char *wifiProvisionedSsid();
int wifiScanResults(const char *out[], int max);

// Scans right away (e.g. after the network list changed), also while a phone
// is connected to the setup access point
void wifiRetryNow();

// True while the first connection attempt after boot is still running
bool wifiFirstAttemptPending();

// Defined in main.cpp: switches to the ticker screen and starts network services
void onWiFiConnected();

#endif // WIFI_PROV_HELPER_H
