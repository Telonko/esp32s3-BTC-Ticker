#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

// Call about once per second. Returns the smoothed charge level (0..100).
int batteryUpdate();

// Last smoothed battery voltage in millivolts
uint32_t batteryMilliVolts();

#endif // BATTERY_H
