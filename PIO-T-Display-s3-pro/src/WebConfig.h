#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#define WEB_CONFIG_HOSTNAME "ticker" // http://ticker.local

// Starts the settings web page (once, after Wi-Fi is up)
void webConfigBegin();

// Serves pending requests; call from loop() (settings belong to the UI thread)
void webConfigLoop();

// Removes the page password (recovery via button 1)
void webConfigResetPassword();

#endif // WEB_CONFIG_H
