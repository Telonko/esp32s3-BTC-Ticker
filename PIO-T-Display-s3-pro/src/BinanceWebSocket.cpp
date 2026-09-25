// BinanceWebSocket.cpp
//
// Threading model:
//  - networkTask (core 0) owns the WebSocket: connect, TLS, parsing,
//    SUBSCRIBE/UNSUBSCRIBE to match the streams published by the UI.
//  - loop() (core 1) owns LVGL and settings; it publishes the wanted streams
//    and reads the shared quotes.
// LVGL is not thread-safe, so nothing in the network task touches UI objects.

#include "BinanceWebSocket.h"
#include <WiFi.h>
#include <ui.h>
#include "Settings.h"
#include "Alerts.h"
#include "History.h"
#include "IconStore.h"
#include "CoinNames.h"

#define WS_HOST "stream.binance.com"
#define WS_PORT 9443
#define WS_RECONNECT_INTERVAL_MS 5000
#define NETWORK_TASK_STACK 12288
// No ticker message for this long = dead connection (TCP can stay "open"
// for a long time after the peer is gone); reconnect right away
#define WS_STALE_MS 20000

int currentTicker = 0;

struct Quote
{
    double last;
    double high;
    double low;
};

// Shared between the UI (writer of names/wanted) and the network task
// (writer of quotes); everything below is guarded by stateMux.
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
static char pubNames[MAX_TICKERS][TICKER_LEN];
static uint8_t pubCount = 0;
static uint32_t pubWanted = 0; // bit i: stream settings.tickers[i]
static uint32_t pubVersion = 0;
static Quote quotes[MAX_TICKERS];
static volatile uint32_t quotesVersion = 0;
static volatile bool wsConnected = false;
static volatile unsigned long lastMessageAt = 0;

// Network task only
static char subNames[MAX_TICKERS][TICKER_LEN]; // currently subscribed
static uint8_t subCount = 0;
static uint32_t syncedVersion = UINT32_MAX;
static uint32_t requestId = 0;

// UI-side state
static uint32_t shownVersion = 0;
static int8_t shownWsState = -1; // -1: not drawn yet
static Quote shownQuote = {-1, -1, -1};
// Top-right corner shows the pair name, or the IP address until this time
static unsigned long ipShownUntil = 0;
static void showTickerName();

static WebSocketsClient webSocket;
static TaskHandle_t networkTaskHandle = nullptr;

static bool isSubscribed(const char *name)
{
    for (int i = 0; i < subCount; i++)
    {
        if (strcmp(subNames[i], name) == 0)
            return true;
    }
    return false;
}

// Maps "BTCUSDT" to a subscribed name ("btc"); nullptr if not subscribed
// (e.g. messages still in flight after UNSUBSCRIBE).
static const char *subscribedName(const char *symbol)
{
    for (int i = 0; i < subCount; i++)
    {
        size_t len = strlen(subNames[i]);
        if (strncasecmp(symbol, subNames[i], len) == 0 && strcasecmp(symbol + len, "usdt") == 0)
            return subNames[i];
    }
    return nullptr;
}

// Runs in the network task
static void onWebSocketEvent(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_TEXT:
    {
        // Short stalls (below WS_STALE_MS) show up as a lagging price
        unsigned long gap = millis() - lastMessageAt;
        if (lastMessageAt && gap > 3000)
            Serial.printf("[WS] No data for %.1f s (RSSI %d dBm)\n", gap / 1000.0, WiFi.RSSI());
        lastMessageAt = millis();

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

        // Subscription replies ({"result":null,"id":N}) and errors have no symbol
        const char *name = subscribedName(doc["s"] | "");
        if (!name)
        {
            if (!doc["s"].is<const char *>())
                Serial.printf("[WS] <- %.*s\n", (int)length, (const char *)payload);
            return;
        }

        Quote q;
        q.last = strtod(doc["c"] | "0", nullptr);
        q.high = strtod(doc["h"] | "0", nullptr);
        q.low = strtod(doc["l"] | "0", nullptr);

        bool first = false;
        portENTER_CRITICAL(&stateMux);
        for (int i = 0; i < pubCount; i++)
        {
            if (strcmp(pubNames[i], name) == 0)
            {
                first = quotes[i].last == 0;
                quotes[i] = q;
                quotesVersion++;
                break;
            }
        }
        portEXIT_CRITICAL(&stateMux);

        historyOnPrice(name, q.last);

        if (first)
            Serial.printf("[WS] First %s price: %.2f\n", name, q.last);
        break;
    }
    case WStype_DISCONNECTED:
        // Also fired on every failed connection attempt (every WS_RECONNECT_INTERVAL_MS)
        Serial.printf("[WS] %s, free heap %u, largest block %u\n",
                      wsConnected ? "Disconnected" : "Connection failed",
                      ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        wsConnected = false;
        subCount = 0;
        syncedVersion = UINT32_MAX;
        break;
    case WStype_CONNECTED:
        Serial.println("[WS] Connected");
        lastMessageAt = millis();
        wsConnected = true;
        break;
    case WStype_ERROR:
        Serial.println("[WS] Error");
        break;
    default:
        break;
    }
}

static void sendSubscription(const char *method, char names[][TICKER_LEN], int count)
{
    if (count == 0)
        return;

    String msg = "{\"method\":\"";
    msg += method;
    msg += "\",\"params\":[";
    for (int i = 0; i < count; i++)
    {
        if (i)
            msg += ',';
        msg += '"';
        msg += names[i];
        msg += "usdt@miniTicker\"";
    }
    msg += "],\"id\":";
    msg += ++requestId;
    msg += '}';

    Serial.printf("[WS] %s\n", msg.c_str());
    webSocket.sendTXT(msg);
}

// Brings the subscriptions in line with the published wanted set, over the
// existing connection (no reconnect, no new TLS handshake).
static void syncSubscription()
{
    if (!wsConnected || syncedVersion == pubVersion)
        return;

    char wanted[MAX_TICKERS][TICKER_LEN];
    int wantedCount = 0;
    uint32_t version;

    portENTER_CRITICAL(&stateMux);
    version = pubVersion;
    for (int i = 0; i < pubCount; i++)
    {
        if (pubWanted & (1u << i))
            memcpy(wanted[wantedCount++], pubNames[i], TICKER_LEN);
    }
    portEXIT_CRITICAL(&stateMux);

    char toUnsub[MAX_TICKERS][TICKER_LEN];
    int unsubCount = 0;
    for (int i = 0; i < subCount; i++)
    {
        bool keep = false;
        for (int j = 0; j < wantedCount && !keep; j++)
            keep = strcmp(subNames[i], wanted[j]) == 0;
        if (!keep)
            memcpy(toUnsub[unsubCount++], subNames[i], TICKER_LEN);
    }

    char toSub[MAX_TICKERS][TICKER_LEN];
    int newCount = 0;
    for (int j = 0; j < wantedCount; j++)
    {
        if (!isSubscribed(wanted[j]))
            memcpy(toSub[newCount++], wanted[j], TICKER_LEN);
    }

    sendSubscription("UNSUBSCRIBE", toUnsub, unsubCount);
    sendSubscription("SUBSCRIBE", toSub, newCount);

    memcpy(subNames, wanted, sizeof(wanted[0]) * wantedCount);
    subCount = wantedCount;

    // Drop prices of pairs that are no longer streamed: they would go stale
    portENTER_CRITICAL(&stateMux);
    for (int i = 0; i < pubCount; i++)
    {
        if (!isSubscribed(pubNames[i]))
            quotes[i] = {0, 0, 0};
    }
    portEXIT_CRITICAL(&stateMux);

    syncedVersion = version;
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

        if (wsConnected && millis() - lastMessageAt > WS_STALE_MS)
        {
            Serial.printf("[WS] No data for %d s, reconnecting\n", WS_STALE_MS / 1000);
            webSocket.disconnect();
            wsConnected = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void initBinanceWebSocket()
{
    if (networkTaskHandle)
        return;

    wsPublishStreams();
    historyBegin();
    // Core 0 hosts the Wi-Fi stack; the Arduino loop (LVGL) stays on core 1
    xTaskCreatePinnedToCore(networkTask, "binance_ws", NETWORK_TASK_STACK, nullptr, 1, &networkTaskHandle, 0);
}

void wsPublishStreams()
{
    if (currentTicker >= settings.tickerCount)
        currentTicker = 0;

    // All pairs are streamed: switching is instant with a fresh price, and
    // alerts work for pairs that are not on screen (~0.3 KB/s per pair)
    uint32_t wanted = (1u << settings.tickerCount) - 1;

    portENTER_CRITICAL(&stateMux);
    bool listChanged = pubCount != settings.tickerCount ||
                       memcmp(pubNames, settings.tickers, sizeof(pubNames[0]) * pubCount) != 0;
    if (listChanged)
    {
        memcpy(pubNames, settings.tickers, sizeof(pubNames));
        pubCount = settings.tickerCount;
        memset(quotes, 0, sizeof(quotes));
    }
    pubWanted = wanted;
    pubVersion++;
    portEXIT_CRITICAL(&stateMux);

    if (listChanged)
        historySetTickers(settings.tickers, settings.tickerCount);
}

bool wsGetPrice(int idx, double *last)
{
    if (idx < 0 || idx >= MAX_TICKERS)
        return false;

    portENTER_CRITICAL(&stateMux);
    *last = quotes[idx].last;
    portEXIT_CRITICAL(&stateMux);
    return *last > 0;
}

static void showCurrentQuote()
{
    portENTER_CRITICAL(&stateMux);
    Quote q = quotes[currentTicker];
    portEXIT_CRITICAL(&stateMux);

    // Skip redraw when another pair was updated
    if (q.last == shownQuote.last && q.high == shownQuote.high && q.low == shownQuote.low)
        return;

    shownQuote = q;
    updatePriceUI(q.last, q.high, q.low);
}

void handleBinanceWebSocket()
{
    if (ipShownUntil && (long)(millis() - ipShownUntil) >= 0)
        showTickerName();

    // Also flag a connection that is open but silent. Read the timestamp
    // before millis(): it is updated on the other core.
    unsigned long last = lastMessageAt;
    int8_t ws = wsConnected && millis() - last < WS_STALE_MS / 2 ? 1 : 0;
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
        alertsCheck();
    }
}

static void showTickerName()
{
    ipShownUntil = 0;
    const char *ticker = settings.tickers[currentTicker];

    // Font25 is monospaced, 15 px per char: 8 chars fill the 120 px label.
    // Longer names use the smaller default font (~13 chars), then "..."
    String name = coinDisplayName(ticker);
    bool fits = name.length() <= 8;
    lv_obj_set_style_text_font(ui_Label_text, fits ? &ui_font_Font25 : &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(ui_Label_text, fits ? 0 : 5, 0);
    lv_label_set_long_mode(ui_Label_text, LV_LABEL_LONG_DOT);
    lv_label_set_text(ui_Label_text, name.c_str());
    lv_obj_clear_flag(ui_Label_text, LV_OBJ_FLAG_HIDDEN);

    // An uploaded icon replaces the built-in one
    bool customIcon = iconViewShow(ticker);
    if (strcmp(ticker, "eth") == 0)
    {
        lv_obj_add_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
        if (customIcon)
            lv_obj_add_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
    }
    else if (strcmp(ticker, "btc") == 0)
    {
        lv_obj_add_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
        if (customIcon)
            lv_obj_add_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
    }
}

void showIpAddress(unsigned long durationMs)
{
    if (WiFi.status() != WL_CONNECTED)
        return;

    // The yellow corner is ~140 px wide: Font25 would not fit an IP address
    lv_obj_set_style_text_font(ui_Label_text, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(ui_Label_text, 5, 0);
    lv_label_set_text(ui_Label_text, WiFi.localIP().toString().c_str());
    lv_obj_clear_flag(ui_Label_text, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_img_symbol, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_img_symbol_btc, LV_OBJ_FLAG_HIDDEN);
    iconViewHide();
    ipShownUntil = millis() + durationMs;
    if (ipShownUntil == 0)
        ipShownUntil = 1;
}

void setTickerInfo()
{
    if (currentTicker >= settings.tickerCount)
        currentTicker = 0;
    showTickerName();

    wsPublishStreams();

    // All pairs are streamed, so the price is usually there already;
    // "--" only right after boot or a list change.
    shownQuote = {-1, -1, -1};
    showCurrentQuote();
}

void selectNextTicker()
{
    currentTicker = (currentTicker + 1) % settings.tickerCount;
    setTickerInfo();
}
