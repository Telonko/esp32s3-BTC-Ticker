#ifndef ALERTS_H
#define ALERTS_H

// All functions run on the UI thread (loop()).

// Checks all streamed prices against settings.alerts; a triggered alert is
// one-shot: it is removed, the screen switches to that pair and blinks.
void alertsCheck();

// Blinks the price while an alert is active; call from loop()
void alertLoop();

bool alertActive();
void alertDismiss();

#endif // ALERTS_H
