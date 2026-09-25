#ifndef HISTORY_H
#define HISTORY_H

#include <stdint.h>
#include "Settings.h"

// 24 h of 15-minute closes per pair: loaded from Binance REST (klines) by a
// background task, then kept up to date from the WebSocket stream.
#define HISTORY_POINTS 96
#define HISTORY_BUCKET_MS (15 * 60 * 1000UL)

// Starts the loader task (once)
void historyBegin();

// UI thread: ticker list changed. Keeps data of pairs that stay in the list,
// new pairs get loaded.
void historySetTickers(const char names[][TICKER_LEN], int count);

// Network task: a new price for a pair
void historyOnPrice(const char *name, double price);

// UI thread: copies the series of settings.tickers[idx]; returns the number
// of points (0 while not loaded yet)
int historyGet(int idx, float *out);

// Changes whenever any series changes
uint32_t historyVersion();

#endif // HISTORY_H
