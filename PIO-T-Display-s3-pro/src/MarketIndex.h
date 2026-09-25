#ifndef MARKET_INDEX_H
#define MARKET_INDEX_H

// Crypto Fear & Greed Index (api.alternative.me), updated once a day by the
// source; fetched every 6 hours from the history task (core 0).

// Background task: fetches when due; blocking (HTTPS), never call from loop()
void marketIndexPoll();

// 0..100, or -1 while unknown
int marketIndexValue();

#endif // MARKET_INDEX_H
