// BinanceWebSocket.h

#ifndef BINANCEWEBSOCKET_H
#define BINANCEWEBSOCKET_H

#include <WebSocketsClient.h>
#include <ArduinoJson.h>

#define TICKERS_COUNT 3

extern const char *screenTickers[TICKERS_COUNT];
extern const char *currentTicker;

// Externally defined function to update the UI
void updatePriceUI(double lastRate, double highRate, double lowRate);

// Starts the network task (once). The WebSocket lives entirely in that task,
// so TLS handshakes and reconnects never block the LVGL loop.
void initBinanceWebSocket();

// Called from loop(): pushes fresh prices / connection state to LVGL.
void handleBinanceWebSocket();

// Refreshes ticker name, icon and cached price for currentTicker (UI thread only).
void setTickerInfo();

#endif // BINANCEWEBSOCKET_H
