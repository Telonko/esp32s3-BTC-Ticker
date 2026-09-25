#ifndef CHART_VIEW_H
#define CHART_VIEW_H

// Bottom row modes: high/low, 24 h sparkline, 24 h volume (UI thread only)

// Creates the chart on the ticker screen; restores the last chosen mode
void chartViewInit();

// Next mode: high/low -> chart -> volume (remembered in NVS)
void chartViewToggle();

// Redraws when the data or the displayed pair changed; call from loop()
void chartViewLoop();

#endif // CHART_VIEW_H
