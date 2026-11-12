// LugaruEX launcher - Xbox 360 style controller visualizer (GDI)
#pragma once
#include "pad.h"

// Draws a controller outline into r (keeps its aspect, centred). Sticks move,
// pressed buttons light up, triggers fill. s may be null (idle outline).
// accent: colour of the outline / highlights (player colour).
void PadViz_Draw(HDC dc, const RECT& r, const PadState* s, COLORREF accent, bool showLabels);
