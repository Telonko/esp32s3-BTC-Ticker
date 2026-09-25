#include "ChartView.h"
#include <Arduino.h>
#include <Preferences.h>
#include <ui.h>
#include "History.h"
#include "BinanceWebSocket.h"

#define CHART_REDRAW_MS 5000  // live price moves the last point every second
#define VOLUME_REDRAW_MS 2000
#define CHART_RANGE 1000
#define COLOR_UP 0x3FCF6E
#define COLOR_DOWN 0xE5484D
#define COLOR_VOLUME 0xFFCA41
#define PREFS_NAMESPACE "ticker"
#define PREFS_MODE_KEY "bottom"
#define PREFS_OLD_CHART_KEY "chart" // bool of the two-mode version

// Bottom row: high/low (from the SquareLine UI), 24 h chart or 24 h volume
enum BottomMode : uint8_t
{
    MODE_HIGH_LOW,
    MODE_CHART,
    MODE_VOLUME,
    MODE_COUNT
};

static lv_obj_t *chart = nullptr;
static lv_chart_series_t *line = nullptr;
static lv_obj_t *volumeBox = nullptr;
static lv_obj_t *volumeLabel = nullptr;
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

    if (mode == MODE_VOLUME)
        lv_obj_clear_flag(volumeBox, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(volumeBox, LV_OBJ_FLAG_HIDDEN);

    shownTicker = -1; // redraw on next loop
}

void chartViewInit()
{
    chart = createRowBox(lv_chart_create(ui_ticker));
    lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR); // no point markers

    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_chart_set_point_count(chart, HISTORY_POINTS);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, CHART_RANGE);
    line = lv_chart_add_series(chart, lv_color_hex(COLOR_UP), LV_CHART_AXIS_PRIMARY_Y);

    // Font38 is monospaced, ~23 px per char: "24h Vol 1.23B" = ~300 px
    volumeBox = createRowBox(lv_obj_create(ui_ticker));
    volumeLabel = lv_label_create(volumeBox);
    lv_obj_center(volumeLabel);
    lv_obj_set_style_text_font(volumeLabel, &ui_font_Font38, 0);
    lv_obj_set_style_text_color(volumeLabel, lv_color_hex(COLOR_VOLUME), 0);
    lv_label_set_text(volumeLabel, "");

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    if (prefs.isKey(PREFS_MODE_KEY))
        mode = prefs.getUChar(PREFS_MODE_KEY) % MODE_COUNT;
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

static void redrawVolume()
{
    double volume;
    if (!wsGetVolume(currentTicker, &volume))
    {
        lv_label_set_text(volumeLabel, "24h Vol --");
        return;
    }

    const char *suffix = "";
    if (volume >= 1e9)
    {
        volume /= 1e9;
        suffix = "B";
    }
    else if (volume >= 1e6)
    {
        volume /= 1e6;
        suffix = "M";
    }
    else if (volume >= 1e3)
    {
        volume /= 1e3;
        suffix = "K";
    }
    lv_label_set_text_fmt(volumeLabel, "24h Vol %.2f%s", volume, suffix);
}

void chartViewLoop()
{
    if (!chart || mode == MODE_HIGH_LOW)
        return;

    bool tickerChanged = currentTicker != shownTicker;
    if (mode == MODE_VOLUME)
    {
        if (!tickerChanged && millis() - lastRedraw < VOLUME_REDRAW_MS)
            return;
        shownTicker = currentTicker;
        lastRedraw = millis();
        redrawVolume();
        return;
    }

    uint32_t version = historyVersion();
    if (!tickerChanged && (version == shownVersion || millis() - lastRedraw < CHART_REDRAW_MS))
        return;

    shownTicker = currentTicker;
    shownVersion = version;
    lastRedraw = millis();
    redrawChart();
}
