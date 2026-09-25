// BinanceWebSocket.cpp
//
// Threading model:
//  - networkTask (core 0) owns the WebSocket: connect, TLS, parsing,
//    SUBSCRIBE/UNSUBSCRIBE when the UI switches currentTicker.
//  - loop() (core 1) owns LVGL and only reads the shared quotes.
// LVGL is not thread-safe, so nothing in the network task touches UI objects.

#include "BinanceWebSocket.h"
#include <WiFi.h>
#include <ui.h>

#define WS_HOST "stream.binance.com"
#define WS_PORT 9443
#define WS_RECONNECT_INTERVAL_MS 5000
#define NETWORK_TASK_STACK 12288

const char *screenTickers[TICKERS_COUNT] = {"ltc", "eth", "btc"};
const char *currentTicker = screenTickers[0];

struct Quote
{
    double last;
    double high;
    double low;
};

// Written by the network task, read by the UI loop
static Quote quotes[TICKERS_COUNT];
static portMUX_TYPE quotesMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t quotesVersion = 0;
static volatile bool wsConnected = false;

// Network task only
static int subscribedTicker = -1;
static uint32_t requestId = 0;

// UI-side state
static uint32_t shownVersion = 0;
static int8_t shownWsState = -1; // -1: not drawn yet
static Quote shownQuote = {-1, -1, -1};

static WebSocketsClient webSocket;
static TaskHandle_t networkTaskHandle = nullptr;

static int tickerIndex(const char *ticker)
{
    for (int i = 0; i < TICKERS_COUNT; i++)
    {
        if (strcmp(ticker, screenTickers[i]) == 0)
            return i;
    }
    return -1;
}

// Maps "BTCUSDT" to its index in screenTickers
static int symbolIndex(const char *symbol)
{
    for (int i = 0; i < TICKERS_COUNT; i++)
    {
        size_t len = strlen(screenTickers[i]);
        if (strncasecmp(symbol, screenTickers[i], len) == 0 && strcasecmp(symbol + len, "usdt") == 0)
            return i;
    }
    return -1;
}

// Runs in the network task
static void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_TEXT:
    {
        // Keep only the fields we need: much less RAM and parsing time
        JsonDocument filter;
        filter["s"] = true;
        filter["c"] = true;
        filter["h"] = true;
        filter["l"] = true;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, payload, length, DeserializationOption::Filter(filter));
        if (error)
        {
            Serial.printf("[WS] deserializeJson() failed: %s\n", error.c_str());
            return;
        }

        // Subscription replies ({"result":null,"id":N}) have no symbol
        JsonObject data = doc.as<JsonObject>();
        int idx = symbolIndex(data["s"] | "");
        if (idx < 0)
            return;

        Quote q;
        q.last = strtod(data["c"] | "0", nullptr);
        q.high = strtod(data["h"] | "0", nullptr);
        q.low = strtod(data["l"] | "0", nullptr);

        portENTER_CRITICAL(&quotesMux);
        quotes[idx] = q;
        quotesVersion++;
        portEXIT_CRITICAL(&quotesMux);
        break;
    }
    case WStype_DISCONNECTED:
        // Also fired on every failed connection attempt (every WS_RECONNECT_INTERVAL_MS)
        Serial.printf("[WS] %s, free heap %u, largest block %u\n",
                      wsConnected ? "Disconnected" : "Connection failed",
                      ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        wsConnected = false;
        subscribedTicker = -1;
        break;
    case WStype_CONNECTED:
        Serial.println("[WS] Connected");
        wsConnected = true;
        break;
    case WStype_ERROR:
        Serial.println("[WS] Error");
        break;
    default:
        break;
    }
}

static void sendSubscription(const char *method, int ticker)
{
    char msg[96];
    snprintf(msg, sizeof(msg), "{\"method\":\"%s\",\"params\":[\"%susdt@miniTicker\"],\"id\":%u}",
             method, screenTickers[ticker], ++requestId);
    Serial.printf("[WS] %s\n", msg);
    webSocket.sendTXT(msg);
}

// Only the displayed ticker is subscribed. Switching sends UNSUBSCRIBE/SUBSCRIBE
// over the same connection, so no reconnect and no new TLS handshake.
static void syncSubscription()
{
    int wanted = tickerIndex(currentTicker);
    if (!wsConnected || wanted < 0 || wanted == subscribedTicker)
        return;

    if (subscribedTicker >= 0)
        sendSubscription("UNSUBSCRIBE", subscribedTicker);
    sendSubscription("SUBSCRIBE", wanted);
    subscribedTicker = wanted;
}

static void networkTask(void *)
{
    webSocket.beginSSL(WS_HOST, WS_PORT, "/ws");
    webSocket.onEvent(onWebSocketEvent);
    webSocket.setReconnectInterval(WS_RECONNECT_INTERVAL_MS);
    // Detect half-open connections (Wi-Fi AP alive, internet gone)
    webSocket.enableHeartbeat(15000, 3000, 2);

    for (;;)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            if (webSocket.isConnected())
                webSocket.disconnect();
            wsConnected = false;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        webSocket.loop();
        syncSubscription();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void initBinanceWebSocket()
{
    if (networkTaskHandle)
        return;

    // Core 0 hosts the Wi-Fi stack; the Arduino loop (LVGL) stays on core 1
    xTaskCreatePinnedToCore(networkTask, "binance_ws", NETWORK_TASK_STACK, nullptr, 1, &networkTaskHandle, 0);
}

static void showCurrentQuote()
{
    int idx = tickerIndex(currentTicker);
    if (idx < 0)
        return;

    portENTER_CRITICAL(&quotesMux);
    Quote q = quotes[idx];
    portEXIT_CRITICAL(&quotesMux);

    // Skip redraw when another ticker was updated
    if (q.last == shownQuote.last && q.high == shownQuote.high && q.low == shownQuote.low)
        return;

    shownQuote = q;
    updatePriceUI(q.last, q.high, q.low);
}

void handleBinanceWebSocket()
{
    int8_t ws = wsConnected ? 1 : 0;
    if (ws != shownWsState)
    {
        shownWsState = ws;
        if (ws)
            lv_obj_add_flag(ui_no_websocket, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(ui_no_websocket, LV_OBJ_FLAG_HIDDEN);
    }

    uint32_t version = quotesVersion;
    if (version != shownVersion)
    {
        shownVersion = version;
        showCurrentQuote();
    }
}

void setTickerInfo()
{
    lv_obj_clear_flag(ui_Label_text, LV_OBJ_FLAG_HIDDEN);
    if (strcmp(currentTicker, "eth") == 0)
    {
        lv_label_set_text(ui_Label_text, "Ethereum");
        lv_obj_add_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
    }
    else if (strcmp(currentTicker, "btc") == 0)
    {
        lv_label_set_text(ui_Label_text, "Bitcoin");
        lv_obj_add_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(ui_Label_text, currentTicker);
        lv_obj_add_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
    }

    // The new ticker was not streamed while unsubscribed: drop its stale price,
    // labels show "--" until the first update arrives (~1 s)
    int idx = tickerIndex(currentTicker);
    if (idx >= 0)
    {
        portENTER_CRITICAL(&quotesMux);
        quotes[idx] = {0, 0, 0};
        portEXIT_CRITICAL(&quotesMux);
    }
    shownQuote = {-1, -1, -1};
    showCurrentQuote();
}
