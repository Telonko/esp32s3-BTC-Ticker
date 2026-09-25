#ifndef COIN_NAMES_H
#define COIN_NAMES_H

#include <Arduino.h>

#define COIN_NAME_MAX 24

// Name shown on screen: set on the web page, else built-in, else "BTC"
String coinDisplayName(const char *ticker);

// Built-in name of a popular coin ("" if unknown)
const char *coinBuiltinName(const char *ticker);

// Name set on the web page ("" if none); stored in NVS
String coinCustomName(const char *ticker);
void coinSetCustomName(const char *ticker, String name); // empty = remove; ASCII only

#endif // COIN_NAMES_H
