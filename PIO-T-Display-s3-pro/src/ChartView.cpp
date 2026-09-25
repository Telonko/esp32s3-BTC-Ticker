#include "ChartView.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ui.h>
#include "History.h"
#include "BinanceWebSocket.h"

#define CHART_REDRAW_MS 5000 // live price moves the last point every second
#define CHART_RANGE 1000
#define COLOR_UP 0x3FCF6E
#define COLOR_DOWN 0xE5484D
#define PREFS_NAMESPACE "ticker"
#define PREFS_CHART_KEY "chart"

static lv_obj_t *chart = nullptr;
static lv_chart_series_t *line = nullptr;
static bool visible = false;
static uint32_t shownVersion = UINT32_MAX;
static int shownTicker = -1;
static unsigned long lastRedraw = 0;

static void applyVisibility()
{
    if (visible)
    {
        lv_obj_add_flag(ui_LabelPricehigh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_labelPriceLow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(chart, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(ui_LabelPricehigh, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_labelPriceLow, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(chart, LV_OBJ_FLAG_HIDDEN);
    }
    shownTicker = -1; // redraw on next loop
}

void chartViewInit()
{
    // Bottom row, right of the corner decoration; the opaque black background
    // covers the up/down arrows of the background image
    chart = lv_chart_create(ui_ticker);
    lv_obj_set_pos(chart, 32, 182);
    lv_obj_set_size(chart, 496, 52);
    lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(chart, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 3, 0);
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR); // no point markers

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_point_count(chart, HISTORY_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_RANGE);
    line = lv_chart_add_series(chart, lv_color_hex(COLOR_UP), LV_CHART_AXIS_PRIMARY_Y);

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);
    visible = prefs.isKey(PREFS_CHART_KEY) && prefs.getBool(PREFS_CHART_KEY);
    prefs.end();
    applyVisibility();
}

void chartViewToggle()
{
    visible = !visible;
    applyVisibility();

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putBool(PREFS_CHART_KEY, visible);
    prefs.end();
}

static void redraw()
{
    static float points[HISTORY_POINTS];
    int count = historyGet(currentTicker, points);

    // Right-align: while loading or after a list change the left part stays empty
    int offset = HISTORY_POINTS - count;
    for (int i = 0; i < offset; i++)
        lv_chart_set_value_by_id(chart, line, i, LV_CHART_POINT_NONE);

    if (count > 0)
    {
        float lo = points[0], hi = points[0];
        for (int i = 1; i < count; i++)
        {
            lo = min(lo, points[i]);
            hi = max(hi, points[i]);
        }
        float span = hi - lo;
        for (int i = 0; i < count; i++)
        {
            lv_coord_t y = span > 0 ? (lv_coord_t)((points[i] - lo) / span * CHART_RANGE) : CHART_RANGE / 2;
            lv_chart_set_value_by_id(chart, line, offset + i, y);
        }
        lv_chart_set_series_color(chart, line, lv_color_hex(points[count - 1] >= points[0] ? COLOR_UP : COLOR_DOWN));
    }
    lv_chart_refresh(chart);
}

void chartViewLoop()
{
    if (!chart || !visible)
        return;

    uint32_t version = historyVersion();
    bool tickerChanged = currentTicker != shownTicker;
    if (!tickerChanged && (version == shownVersion || millis() - lastRedraw < CHART_REDRAW_MS))
        return;

    shownTicker = currentTicker;
    shownVersion = version;
    lastRedraw = millis();
    redraw();
}
