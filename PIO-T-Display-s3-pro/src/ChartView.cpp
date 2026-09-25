#include "ChartView.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ui.h>
#include "History.h"
#include "BinanceWebSocket.h"

#define CHART_REDRAW_MS 5000  // the live price moves the last candle every second
#define CANDLE_SLOT 5 // px per candle: 3 px body + 2 px gap, 96 candles = 480 px
#define CANDLE_BODY 3
#define COLOR_UP 0x3FCF6E
#define COLOR_DOWN 0xE5484D
#define PREFS_NAMESPACE "ticker"
#define PREFS_MODE_KEY "bottom"
#define PREFS_OLD_CHART_KEY "chart" // bool of the two-mode version

// Bottom row: high/low (from the SquareLine UI) or 24 h candles; the 24 h
// volume is in the top row
enum BottomMode : uint8_t
{
    MODE_HIGH_LOW,
    MODE_CHART,
    MODE_COUNT
};

static lv_obj_t *chart = nullptr;
static Candle candles[HISTORY_POINTS]; // copy drawn by drawCandles()
static int candleCount = 0;
static uint8_t mode = MODE_HIGH_LOW;
static uint32_t shownVersion = UINT32_MAX;
static int shownTicker = -1;
static unsigned long lastRedraw = 0;

// Opaque black box over the row: covers the up/down arrows of the background
static lv_obj_t *createRowBox(lv_obj_t *box)
{
    lv_obj_set_pos(box, 32, 182);
    lv_obj_set_size(box, 496, 52);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(box, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_pad_all(box, 3, 0);
    return box;
}

static void applyMode()
{
    bool highLow = mode == MODE_HIGH_LOW;
    if (highLow)
    {
        lv_obj_clear_flag(ui_LabelPricehigh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_labelPriceLow, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ui_LabelPricehigh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_labelPriceLow, LV_OBJ_FLAG_HIDDEN);
    }

    if (mode == MODE_CHART)
        lv_obj_clear_flag(chart, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(chart, LV_OBJ_FLAG_HIDDEN);

    shownTicker = -1; // redraw on next loop
}

// lv_chart has no candlesticks: draw them directly on a plain object
static void drawCandles(lv_event_t *e)
{
    if (candleCount == 0)
        return;

    lv_obj_t *obj = lv_event_get_target(e);
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t area;
    lv_obj_get_content_coords(obj, &area);
    lv_coord_t height = lv_area_get_height(&area);

    float lo = candles[0].low, hi = candles[0].high;
    for (int i = 1; i < candleCount; i++)
    {
        lo = min(lo, candles[i].low);
        hi = max(hi, candles[i].high);
    }
    float span = hi - lo;
    auto toY = [&](float price) -> lv_coord_t
    {
        float t = span > 0 ? (price - lo) / span : 0.5f;
        return area.y2 - (lv_coord_t)(t * (height - 1));
    };

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;

    // Right-aligned: newest candle at the right edge
    lv_coord_t x = area.x2 - candleCount * CANDLE_SLOT + 1;
    for (int i = 0; i < candleCount; i++, x += CANDLE_SLOT)
    {
        const Candle &c = candles[i];
        dsc.bg_color = lv_color_hex(c.close >= c.open ? COLOR_UP : COLOR_DOWN);

        lv_area_t wick = {(lv_coord_t)(x + CANDLE_BODY / 2), toY(c.high), (lv_coord_t)(x + CANDLE_BODY / 2), toY(c.low)};
        lv_draw_rect(ctx, &dsc, &wick);

        lv_coord_t top = toY(max(c.open, c.close));
        lv_coord_t bottom = toY(min(c.open, c.close));
        lv_area_t body = {x, top, (lv_coord_t)(x + CANDLE_BODY - 1), bottom}; // flat candle = 1 px line
        lv_draw_rect(ctx, &dsc, &body);
    }
}

void chartViewInit()
{
    chart = createRowBox(lv_obj_create(ui_ticker));
    lv_obj_add_event_cb(chart, drawCandles, LV_EVENT_DRAW_MAIN_END, nullptr);

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    if (prefs.isKey(PREFS_MODE_KEY))
        mode = prefs.getUChar(PREFS_MODE_KEY) % MODE_COUNT; // old "volume" = 2 -> high/low
    else if (prefs.isKey(PREFS_OLD_CHART_KEY) && prefs.getBool(PREFS_OLD_CHART_KEY))
        mode = MODE_CHART;
    prefs.end();
    applyMode();
}

void chartViewToggle()
{
    mode = (mode + 1) % MODE_COUNT;
    applyMode();

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putUChar(PREFS_MODE_KEY, mode);
    prefs.end();
}

static void redrawChart()
{
    // While loading or after a list change there are fewer candles: the
    // left part stays empty
    candleCount = historyGet(currentTicker, candles);
    lv_obj_invalidate(chart);
}

void chartViewLoop()
{
    if (!chart || mode == MODE_HIGH_LOW)
        return;

    bool tickerChanged = currentTicker != shownTicker;
    uint32_t version = historyVersion();
    if (!tickerChanged && (version == shownVersion || millis() - lastRedraw < CHART_REDRAW_MS))
        return;

    shownTicker = currentTicker;
    shownVersion = version;
    lastRedraw = millis();
    redrawChart();
}
