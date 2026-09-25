#ifndef CHART_VIEW_H
#define CHART_VIEW_H

// 24 h sparkline in place of the high/low row (UI thread only)

// Creates the chart on the ticker screen; restores the last chosen mode
void chartViewInit();

// Switches the bottom row between high/low and the chart (remembered in NVS)
void chartViewToggle();

// Redraws when the data or the displayed pair changed; call from loop()
void chartViewLoop();

#endif // CHART_VIEW_H
