// Weather icons drawn with the Waveshare Paint primitives (no bitmaps).
//
// Every function draws into the currently selected Paint image, centred on
// (cx, cy). An icon fits in a box of about 56 x 40 px around that point.

#ifndef WEATHER_ICONS_H
#define WEATHER_ICONS_H

void iconClear(int cx, int cy);          // sun
void iconPartlyCloudy(int cx, int cy);   // sun behind a cloud
void iconOvercast(int cx, int cy);       // cloud
void iconFog(int cx, int cy);            // horizontal lines
void iconDrizzle(int cx, int cy);        // cloud + short red strokes
void iconRain(int cx, int cy);           // cloud + red strokes
void iconSnow(int cx, int cy);           // cloud + dots
void iconThunderstorm(int cx, int cy);   // cloud + yellow bolt
void iconUnknown(int cx, int cy);        // "?"

#endif
