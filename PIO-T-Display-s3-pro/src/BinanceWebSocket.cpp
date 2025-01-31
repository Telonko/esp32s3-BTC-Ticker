// BinanceWebSocket.cpp

#include "BinanceWebSocket.h"
#include <WiFi.h>
#include <ui.h>

// Global WebSocket client instance
WebSocketsClient webSocket;

float lastRate = 0.0;
float highRate = 0.0;
float lowRate = 0.0;

char *screenTickers[2] = {(char *)"eth", (char *)"btc"};
char *currentTicker = (char *)screenTickers[0];

// Function to handle incoming WebSocket messages
void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_TEXT:
    {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);

        if (error)
        {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
        }

        // Handle different types of messages
        if (doc.containsKey("e") && doc["e"] == "24hrTicker")
        {
            lastRate = doc["c"].as<float>();  // Last price
            highRate = doc["h"].as<float>(); // High price
            lowRate = doc["l"].as<float>();  // Low price

            // Update the UI with the latest data
            updatePriceUI(lastRate, highRate, lowRate);
        } else {
            Serial.printf("%s\n", payload);
        }

        break;
    }
    case WStype_DISCONNECTED:
        Serial.println("WebSocket Disconnected");
        break;
    case WStype_CONNECTED:
        Serial.println("WebSocket Connected");
        // Subscribe to the 24hr ticker stream for BTCUSDT
        webSocket.sendTXT("{\"method\": \"SUBSCRIBE\", \"params\": [\"" + String((char *)currentTicker) + "usdt@ticker\"], \"id\": 1}");
        break;
    case WStype_ERROR:
        Serial.println("WebSocket Error");
        break;
    default:
        break;
    }
}

// Function to initialize and connect the WebSocket client
void initBinanceWebSocket()
{
    webSocket.disconnect();
    delay(100);

    Serial.printf("[DEBUG] Connecting to websocket %susdt", currentTicker);
    // Initialize WebSocket connection to Binance
    webSocket.beginSSL("stream.binance.com", 9443, "/ws");
    webSocket.onEvent(onWebSocketEvent);

    setTickerInfo();
}

// Function to handle the WebSocket loop
void handleBinanceWebSocket()
{
    webSocket.loop();
}

// Function to switch between tickers
void setTickerInfo()
{
    updatePriceUI(0, 0, 0);
    lv_label_set_text(ui_Label_text, "");
    lv_obj_clear_flag(ui_Label_text, LV_OBJ_FLAG_HIDDEN);
    if (currentTicker == "eth")
    {
        lv_label_set_text(ui_Label_text, "Ethereum");
        lv_obj_add_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
    }
    else if (currentTicker == "btc")
    {
        lv_label_set_text(ui_Label_text, "Bitcoin");

        lv_obj_add_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
    }
}
