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

#endif // BATTERY_H
