#ifndef ICON_STORE_H
#define ICON_STORE_H

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

// Custom pair icons uploaded on the web page, stored as PNG in LittleFS
// (/icons/<pair>.png). They take priority over the built-in BTC/ETH icons.

#define ICON_MAX_BYTES 16384
#define ICON_MAX_SIZE 64 // px, width and height

// Mounts LittleFS (formats it on first use)
bool iconStoreBegin();

bool iconExists(const char *name);
String iconPath(const char *name);

// Checks PNG signature and dimensions; returns an error text or nullptr
const char *iconValidate(const uint8_t *data, size_t size);

void iconDelete(const char *name);

// UI thread: creates the image object on the ticker screen
void iconViewInit();

// UI thread: shows the custom icon of the pair; false if there is none
bool iconViewShow(const char *name);
void iconViewHide();

#endif // ICON_STORE_H
