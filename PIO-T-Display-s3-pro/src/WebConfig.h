#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#define WEB_CONFIG_HOSTNAME "ticker" // http://ticker.local

// Starts the settings web page in its own task (once, after Wi-Fi is up)
void webConfigBegin();

// Removes the page password (recovery via button 1)
void webConfigResetPassword();

#endif // WEB_CONFIG_H
