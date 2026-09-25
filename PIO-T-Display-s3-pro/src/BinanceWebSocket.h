// BinanceWebSocket.h

#ifndef BINANCEWEBSOCKET_H
#define BINANCEWEBSOCKET_H

#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// Index of the displayed pair in settings.tickers (UI thread only)
extern int currentTicker;

// Externally defined function to update the UI
void updatePriceUI(double lastRate, double highRate, double lowRate);

// Starts the network task (once). The WebSocket lives entirely in that task,
// so TLS handshakes and reconnects never block the LVGL loop.
void initBinanceWebSocket();

// Called from loop(): pushes fresh prices / connection state to LVGL.
void handleBinanceWebSocket();

// Refreshes ticker name, icon and price for currentTicker (UI thread only).
void setTickerInfo();

// Shows the next pair from settings.tickers
void selectNextTicker();

// Hands the ticker list to the network task, which subscribes to all pairs.
// Call after settings or currentTicker change.
void wsPublishStreams();

// Last price of settings.tickers[idx]; false if no data yet
bool wsGetPrice(int idx, double *last);

#endif // BINANCEWEBSOCKET_H
