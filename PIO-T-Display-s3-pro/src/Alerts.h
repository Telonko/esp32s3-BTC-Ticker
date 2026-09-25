#ifndef ALERTS_H
#define ALERTS_H

#include <stdint.h>

// All functions run on the UI thread (loop()).

// Checks all streamed prices against settings.alerts; a triggered alert is
// one-shot: it is removed, the screen switches to that pair and blinks.
void alertsCheck();

// Blinks the price while an alert is active; call from loop()
void alertLoop();

bool alertActive();
void alertDismiss();

// Screen brightness while an alert blinks: the whole screen flashes between
// this level and nearly dark, ignoring night mode, to be noticed in the dark.
// Stored in NVS, default 200.
uint8_t alertBrightnessGet();
void alertBrightnessSet(uint8_t level);

// Brightness the screen should have right now while an alert is active
uint8_t alertScreenBrightness();

#endif // ALERTS_H
