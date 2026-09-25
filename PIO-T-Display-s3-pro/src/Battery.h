#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

// Call about once per second. Returns the smoothed charge level (0..100).
int batteryUpdate();

// Last smoothed battery voltage in millivolts
uint32_t batteryMilliVolts();

// Heuristic: the board has no charger status pin, so "charging" means the
// voltage keeps rising. A full battery on USB (voltage flat) reads as false.
bool batteryCharging();

// Heuristic as well: charging, or nearly full and not dropping (full battery
// on USB). Right after unplugging it may stay true for up to ~2 minutes.
bool batteryExternalPower();

#endif // BATTERY_H
