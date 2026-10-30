#include "weather_icons.h"
#include "EPD_1in54g.h"
#include "GUI_Paint.h"
#include "fonts.h"

// ---------- building blocks ----------

// Yellow disc with 8 rays. Rays run from r+3 out to r+len.
static void drawSun(int cx, int cy, int r, int len) {
  // Direction table scaled by 10, so diagonal rays are not longer than straight ones.
  static const int8_t D[8][2] = {{10, 0}, {7, 7}, {0, 10}, {-7, 7}, {-10, 0}, {-7, -7}, {0, -10}, {7, -7}};
  Paint_DrawCircle(cx, cy, r, EPD_1IN54G_YELLOW, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  for (int i = 0; i < 8; i++) {
    int x0 = cx + D[i][0] * (r + 3) / 10, y0 = cy + D[i][1] * (r + 3) / 10;
    int x1 = cx + D[i][0] * (r + len) / 10, y1 = cy + D[i][1] * (r + len) / 10;
    Paint_DrawLine(x0, y0, x1, y1, EPD_1IN54G_YELLOW, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  }
}

// Filled black cloud. Spans cx-18..cx+18 and cy-13..cy+8.
static void drawCloud(int cx, int cy) {
  Paint_DrawCircle(cx - 11, cy + 1, 7, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawCircle(cx + 1, cy - 3, 10, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawCircle(cx + 12, cy + 2, 6, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawRectangle(cx - 11, cy + 1, cx + 12, cy + 8, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

// ---------- icons ----------
// Each comment gives the icon's size (width x height in px) and its extent
// relative to the centre point (cx, cy).
// The sketch draws them at cx=26, cy=41 (top left, under the header). The condition
// text starts at x=54 and the tiles at y=69, so icons should stay within about
// cx-22..cx+25 and cy-15..cy+25.

// 34 x 34: x cx-17..cx+17, y cy-13..cy+21
void iconClear(int cx, int cy) {
  drawSun(cx, cy + 4, 10, 7);
}

// 46 x 28: x cx-21..cx+25, y cy-15..cy+13
void iconPartlyCloudy(int cx, int cy) {
  drawSun(cx - 8, cy - 2, 8, 5);
  drawCloud(cx + 7, cy + 5);
}

// 36 x 21: x cx-18..cx+18, y cy-9..cy+12 (cloud is shifted down 4 px)
void iconOvercast(int cx, int cy) {
  drawCloud(cx, cy + 4);
}

// 44 x 24: x cx-22..cx+22, y cy-8..cy+16
void iconFog(int cx, int cy) {
  for (int i = 0; i < 4; i++) {
    int half = (i % 2) ? 16 : 22;   // alternate line lengths
    int y = cy - 8 + i * 8;
    Paint_DrawLine(cx - half, y, cx + half, y, EPD_1IN54G_BLACK, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  }
}

// 36 x 30: x cx-18..cx+18, y cy-13..cy+17
void iconDrizzle(int cx, int cy) {
  drawCloud(cx, cy);
  for (int i = -1; i <= 1; i++) {
    Paint_DrawLine(cx + i * 9 + 1, cy + 13, cx + i * 9 - 1, cy + 17,
                   EPD_1IN54G_RED, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  }
}

// 36 x 33: x cx-18..cx+18, y cy-13..cy+20
void iconRain(int cx, int cy) {
  drawCloud(cx, cy);
  for (int i = -1; i <= 1; i++) {
    Paint_DrawLine(cx + i * 9 + 2, cy + 12, cx + i * 9 - 2, cy + 20,
                   EPD_1IN54G_RED, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  }
}

// 36 x 35: x cx-18..cx+18, y cy-13..cy+22
void iconSnow(int cx, int cy) {
  drawCloud(cx, cy);
  for (int i = -1; i <= 1; i++) {   // top row of flakes
    Paint_DrawPoint(cx + i * 10, cy + 13, EPD_1IN54G_BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
  }
  for (int i = 0; i < 2; i++) {     // staggered bottom row
    Paint_DrawPoint(cx - 5 + i * 10, cy + 21, EPD_1IN54G_BLACK, DOT_PIXEL_3X3, DOT_STYLE_DFT);
  }
}

// 36 x 39: x cx-18..cx+18, y cy-13..cy+25 (bolt is the lowest part)
void iconThunderstorm(int cx, int cy) {
  drawCloud(cx, cy);
  Paint_DrawLine(cx + 3, cy + 9, cx - 3, cy + 17, EPD_1IN54G_YELLOW, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(cx - 3, cy + 17, cx + 2, cy + 17, EPD_1IN54G_YELLOW, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
  Paint_DrawLine(cx + 2, cy + 17, cx - 4, cy + 25, EPD_1IN54G_YELLOW, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
}

// 17 x 24 (Font24 glyph): x cx-8..cx+9, y cy..cy+24
void iconUnknown(int cx, int cy) {
  Paint_DrawString_EN(cx - 8, cy, "?", &Font24, EPD_1IN54G_BLACK, EPD_1IN54G_WHITE);
}
