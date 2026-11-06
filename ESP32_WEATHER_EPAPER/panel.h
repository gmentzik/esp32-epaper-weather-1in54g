// The one place that names the actual e-paper panel. The drawing code only uses the
// names below, so moving to another panel means swapping the driver files and changing
// this file. A panel with fewer colours maps the missing ones to a colour it has
// (black/white: COL_RED and COL_YELLOW become black).
// Also regenerate bottom_image.cpp: it holds this panel's raw colour codes.

#ifndef PANEL_H
#define PANEL_H

#include "EPD_1in54g.h"

// Size in pixels
#define EPD_WIDTH  EPD_1IN54G_WIDTH
#define EPD_HEIGHT EPD_1IN54G_HEIGHT

// Colours
#define COL_WHITE  EPD_1IN54G_WHITE
#define COL_BLACK  EPD_1IN54G_BLACK
#define COL_RED    EPD_1IN54G_RED
#define COL_YELLOW EPD_1IN54G_YELLOW

// Image buffer: 4 colours take 2 bits, so 4 pixels fit in a byte.
#define PANEL_SCALE 4                                         // for Paint_SetScale()
#define PANEL_IMAGE_BYTES (((EPD_WIDTH + 3) / 4) * EPD_HEIGHT)

// Panel calls
inline void panelInit() { EPD_1IN54G_Init(); }
inline void panelDisplay(const UBYTE *image) { EPD_1IN54G_Display(image); }
inline void panelSleep() { EPD_1IN54G_Sleep(); }

#endif
