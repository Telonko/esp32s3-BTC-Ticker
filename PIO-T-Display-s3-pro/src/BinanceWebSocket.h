// BinanceWebSocket.h

#ifndef BINANCEWEBSOCKET_H
#define BINANCEWEBSOCKET_H

#include <WebSocketsClient.h>
#include <ArduinoJson.h>

extern char *screenTickers[2];
extern char *currentTicker;

// Externally defined function to update the UI
void updatePriceUI(float btcRate, float highRate, float lowRate);

// Function to initialize and connect the WebSocket client
void initBinanceWebSocket();

// Function to handle the WebSocket loop
void handleBinanceWebSocket();

void setTickerInfo();

#endif // BINANCEWEBSOCKET_H
