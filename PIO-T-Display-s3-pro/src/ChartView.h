#ifndef CHART_VIEW_H
#define CHART_VIEW_H

// Bottom row modes: high/low or 24 h candles (UI thread only)

// Creates the chart on the ticker screen; restores the last chosen mode
void chartViewInit();

// Switches high/low <-> candles (remembered in NVS)
void chartViewToggle();

// Redraws when the data or the displayed pair changed; call from loop()
void chartViewLoop();

#endif // CHART_VIEW_H
