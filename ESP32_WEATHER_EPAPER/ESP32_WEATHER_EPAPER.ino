// Weather on a 1.54" 4-color e-Paper (G) with ESP32
// Version 0.3 (see "Version history" in README.md)
//
// Wakes up, connects to WiFi, fetches current weather from Open-Meteo
// (no API key needed), redraws the screen, puts the panel to sleep, then
// deep-sleeps the ESP32 until the next refresh.
// The bottom half of the screen changes on every refresh between views:
// a 3-day forecast, a 12-hour temperature/humidity graph and a picture (see "bottom area").
//
// SETUP
//  1. Library Manager: install "ArduinoJson" (v7) and "DHT sensor library" (Adafruit,
//     also installs "Adafruit Unified Sensor").
//  2. Open this .ino from its folder "ESP32_WEATHER_EPAPER" (the Arduino IDE needs the
//     folder name to match). The Waveshare driver files are already in this folder.
//  3. WiFi: after power-up / reset, join the "Weather-Setup" network with a phone and
//     enter your WiFi name and password (open for SETUP_PORTAL_SECONDS).
//     Optional default: copy secrets.example.h to secrets.h and fill it in.
//  4. Board: ESP32 Dev Module.

/*
 * Wiring: Waveshare 1.54" e-Paper (G), 4-color  ->  ESP32 Dev Module
 * (pin numbers match the silkscreen Dxx labels = GPIO numbers; defined in DEV_Config.h)
 *
 *   e-Paper pin | ESP32 pin | Note
 *   ------------+-----------+-----------------------------------
 *   VCC         | 3V3       | 3.3 V
 *   GND         | GND       |
 *   DIN         | D14       | SPI MOSI  (EPD_MOSI_PIN)
 *   CLK         | D13       | SPI SCK   (EPD_SCK_PIN)
 *   CS          | D15       | chip select (EPD_CS_PIN)
 *   DC          | D27       | data/command (EPD_DC_PIN)
 *   RST         | D26       | reset (EPD_RST_PIN)
 *   BUSY        | D25       | busy flag (EPD_BUSY_PIN)
 *
 *
 *   DHT22 module | ESP32 pin | Note
 *   -------------+-----------+-----------------------------------
 *   + / VCC      | 3V3       | keeps power in deep sleep, so no warm-up wait
 *   OUT / DATA   | D32       | DHT22_PIN in local_sensor.cpp
 *   - / GND      | GND       |
 *
 * Keep D21 (SDA) / D22 (SCL) free for a future I2C pressure sensor.
 *
 * BS jumper on the display: 0 (4-line SPI).
 * Leave D12 unconnected (strapping pin, can block boot/flashing).
 * Upload tip: if "Wrong boot mode" appears, hold BOOT, tap EN, release BOOT.
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>
#include "DEV_Config.h"
#include "EPD_1in54g.h"
#include "GUI_Paint.h"
#include "fonts.h"
#include "weather_icons.h"
#include "local_sensor.h"
#include "wifi_setup.h"
#include "bottom_image.h"

// ---------- settings ----------

const char *CITY = "ATHENS";      // header text (ASCII only)
const float LAT = 37.98;
const float LON = 23.73;

// Full refresh takes ~15-20 s and Waveshare recommends >= 3 min between
// refreshes. Keep this at 3 or more.
const uint32_t REFRESH_MINUTES = 10;
const uint32_t RETRY_MINUTES = 1;    // used when WiFi/fetch fails
// Each failure in a row adds one more RETRY_MINUTES to the wait (1, 2, 3 ... min),
// up to this many times, so a long outage doesn't drain the battery with retries.
const uint8_t MAX_FAIL_COUNT = 15;

// WiFi setup portal: open this long after power-up / reset, never after deep sleep.
const uint32_t SETUP_PORTAL_SECONDS = 120;

#define DEBUG_LOG 1   // 1 = print progress to the Serial Monitor, 0 = silent (battery runs)
// ------------------------------

#if DEBUG_LOG
  #define LOG(...) Serial.printf(__VA_ARGS__)
#else
  #define LOG(...) ((void)0)
#endif

static UBYTE img[((EPD_1IN54G_WIDTH + 3) / 4) * EPD_1IN54G_HEIGHT];

// Points in the hourly graph: the current hour and the 11 after it (12 fit 200 px well).
#define HOURS_SHOWN 12

// One forecast day for the bottom area.
struct DayForecast {
  int code;            // WMO weather code
  float tmax, tmin;
  int rain;            // max. chance of rain, %
  uint8_t wday;        // 0 = Sunday
};

struct Weather {
  float temp, feels, wind;
  int hum, code;
  char hhmm[6];
  char ddmm[6];
  DayForecast days[3];   // tomorrow and the 2 days after
  bool haveDays;         // false if the forecast part of the reply was missing
  int8_t htemp[HOURS_SHOWN];   // hourly temperature, rounded (small, as this lives in RTC memory)
  uint8_t hhum[HOURS_SHOWN];   // hourly humidity, %
  uint8_t hour0;               // hour of day (0..23) of the first point
  bool haveHours;              // false if the hourly part of the reply was missing
};

// Views for the bottom area (see "bottom area" below). Defined up here, before any
// function, so the prototypes the Arduino IDE generates can use the type.
enum BottomView { VIEW_FORECAST, VIEW_HOURLY, VIEW_IMAGE };

// Kept in RTC memory so they survive deep sleep.
RTC_DATA_ATTR static Weather lastW;
RTC_DATA_ATTR static bool haveLast = false;
RTC_DATA_ATTR static bool errShown = false;
RTC_DATA_ATTR static uint8_t failCount = 0;   // failed fetches in a row, 0..MAX_FAIL_COUNT
// Indoor values as currently drawn on the panel (rounded, like on screen).
// Saved only after a real panel refresh, so they always match what is shown.
RTC_DATA_ATTR static bool shownLocOk = false;   // false = screen shows "--"
RTC_DATA_ATTR static int shownTemp = 0;
RTC_DATA_ATTR static int shownHum = 0;

// Retry wait grows with failCount. At least 1x, so a failure with failCount = 0
// (e.g. the display init fails after a good fetch) never sleeps for 0 s.
static uint32_t retrySeconds() {
  return RETRY_MINUTES * 60 * (failCount > 0 ? failCount : 1);
}

static const char *wmoText(int c) {
  if (c == 0) return "Clear";
  if (c == 1) return "Mainly clear";
  if (c == 2) return "Partly cloudy";
  if (c == 3) return "Overcast";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Showers";
  if (c == 85 || c == 86) return "Snow showers";
  if (c >= 95) return "Thunderstorm";
  return "Unknown";
}

// Beaufort force 0..12 from km/h. LIMITS[i] is the lowest speed of force i+1.
// Note: static makes function visible only from this module
static int beaufort(float kmh) {
  //Note: static is avoid reinit constant var at every function call. (memory optimization)
  static const float LIMITS[12] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
  int b = 0;
  while (b < 12 && kmh >= LIMITS[b]) b++;
  return b;
}

// Day of the week, 0 = Sunday (Sakamoto's method).
static uint8_t dayOfWeek(int y, int m, int d) {
  static const int T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + T[m - 1] + d) % 7;
}

// Picks the icon for a WMO weather code (same ranges as wmoText). Icons live in weather_icons.cpp.
static void drawWeatherIcon(int c, int cx, int cy) {
  LOG("Icon for code %d: %s\n", c, wmoText(c));
  if (c == 0 || c == 1) iconClear(cx, cy);
  else if (c == 2) iconPartlyCloudy(cx, cy);
  else if (c == 3) iconOvercast(cx, cy);
  else if (c == 45 || c == 48) iconFog(cx, cy);
  else if (c >= 51 && c <= 57) iconDrizzle(cx, cy);
  else if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) iconRain(cx, cy);
  else if ((c >= 71 && c <= 77) || c == 85 || c == 86) iconSnow(cx, cy);
  else if (c >= 95) iconThunderstorm(cx, cy);
  else iconUnknown(cx, cy);
}

static bool fetchWeather(Weather &w) {
  WiFiClientSecure client;
  client.setInsecure();  // skips certificate check; fine for public weather data

  // "daily" with forecast_days=4 gives today (index 0) plus the 3 days shown in the bottom area.
  // "hourly" starts at 00:00 today (local time), so the current hour is also its index.
  char url[480];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
           "wind_speed_10m,weather_code"
           "&hourly=temperature_2m,relative_humidity_2m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
           "&forecast_days=4&timezone=auto",
           LAT, LON);

  HTTPClient http;
  if (!http.begin(client, url)) {
    LOG("http.begin failed\n");
    return false;
  }
  int status = http.GET();
  LOG("HTTP status %d\n", status);
  if (status != 200) {
    http.end();
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    LOG("JSON error: %s\n", err.c_str());
    return false;
  }

  JsonObject cur = doc["current"];
  if (cur.isNull()) {
    LOG("JSON has no 'current' object\n");
    return false;
  }

  w.temp = cur["temperature_2m"] | 0.0f;
  w.feels = cur["apparent_temperature"] | 0.0f;
  w.hum = cur["relative_humidity_2m"] | 0;
  w.wind = cur["wind_speed_10m"] | 0.0f;
  w.code = cur["weather_code"] | -1;

  strcpy(w.hhmm, "--:--");
  const char *ts = cur["time"] | "";  // e.g. "2026-10-07T21:45" (local time)
  strcpy(w.ddmm, "--/--");
  if (strlen(ts) >= 16) {
    memcpy(w.hhmm, ts + 11, 5);
    w.hhmm[5] = 0;
    // "2026-10-07T..." -> "07/10"
    memcpy(w.ddmm, ts + 8, 2);
    w.ddmm[2] = '/';
    memcpy(w.ddmm + 3, ts + 5, 2);
    w.ddmm[5] = 0;
  }

  // Forecast for the bottom area. Missing data only hides the forecast, the fetch still counts as OK.
  w.haveDays = false;
  JsonObject daily = doc["daily"];
  if (!daily.isNull() && daily["time"].size() >= 4) {
    w.haveDays = true;
    for (int i = 0; i < 3; i++) {
      int k = i + 1;   // skip today
      DayForecast &d = w.days[i];
      d.code = daily["weather_code"][k] | -1;
      d.tmax = daily["temperature_2m_max"][k] | 0.0f;
      d.tmin = daily["temperature_2m_min"][k] | 0.0f;
      d.rain = daily["precipitation_probability_max"][k] | 0;   // can be null; then 0
      int yy = 0, mm = 0, dd = 0;
      const char *date = daily["time"][k] | "";   // e.g. "2026-10-31"
      if (sscanf(date, "%d-%d-%d", &yy, &mm, &dd) == 3 && mm >= 1 && mm <= 12) {
        d.wday = dayOfWeek(yy, mm, dd);
      } else {
        w.haveDays = false;   // without a date the day names would be wrong
      }
    }
  }

  // Hourly graph: HOURS_SHOWN points from the current hour. Missing data only hides the graph.
  w.haveHours = false;
  JsonObject hourly = doc["hourly"];
  int hour = strlen(ts) >= 16 ? atoi(ts + 11) : -1;   // "2026-10-07T21:45" -> 21
  if (!hourly.isNull() && hour >= 0 && hour <= 23 &&
      hourly["temperature_2m"].size() >= (size_t)(hour + HOURS_SHOWN) &&
      hourly["relative_humidity_2m"].size() >= (size_t)(hour + HOURS_SHOWN)) {
    // Make sure index "hour" really is this hour, e.g. "2026-10-07T21:00" vs "2026-10-07T21:45".
    const char *t0 = hourly["time"][hour] | "";
    if (strncmp(t0, ts, 13) == 0) {
      w.haveHours = true;
      w.hour0 = hour;
      for (int i = 0; i < HOURS_SHOWN; i++) {
        w.htemp[i] = (int8_t)lroundf(hourly["temperature_2m"][hour + i] | 0.0f);
        w.hhum[i] = hourly["relative_humidity_2m"][hour + i] | 0;
      }
    }
  }
  return true;
}

// Screen layout (200 x 200). Weather uses the top part down to the divider;
// everything below it (y 106..199) is free for other information.
//   y  0..20  black header: city left, update time right
//   y 21..23  yellow accent line
//   y 26..66  icon left (cx 26), description right of it, temp + humidity below that
//   y 69..102 tiles: FEELS (x 2), WIND (x 54, under the temp), INDOOR (x 126)
//   y 105     divider
// Note: Paint_DrawString_EN takes the BACKGROUND colour first, then the TEXT colour
// (GUI_Paint.h names them the other way round, but the code swaps them):
//   Paint_DrawString_EN(x, y, "Hello", &Font16, EPD_1IN54G_WHITE, EPD_1IN54G_RED);
//                       pos   text     size     BACKGROUND        TEXT colour
// Paint_DrawChar and Paint_DrawNum take the TEXT colour first.
#define ICON_X    26
#define ICON_Y    41
#define TEXT_X    54    // description, temperature and the WIND tile start here
#define TILE_Y    69    // tile titles; values go 13 px lower
#define DIVIDER_Y 105

// One reading: the number in black (big Font20 unless told otherwise) and its unit in
// small red Font12, bottoms lined up. Returns the x just after the unit, so the next
// value can follow. Font20 is 14 px and Font12 7 px wide; a reading wraps if it runs past x 199.
static int drawReading(int x, int y, const char *num, const char *unit,
                       sFONT *numFont = &Font20, sFONT *unitFont = &Font12) {
  Paint_DrawString_EN(x, y, num, numFont, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  x += numFont->Width * strlen(num);
  Paint_DrawString_EN(x, y + numFont->Height - unitFont->Height, unit, unitFont, EPD_1IN54G_WHITE, EPD_1IN54G_RED);
  return x + unitFont->Width * strlen(unit);
}

// Width in px of a reading drawn by drawReading, used to centre it.
static int readingWidth(const char *num, const char *unit, sFONT *numFont, sFONT *unitFont = &Font12) {
  return numFont->Width * strlen(num) + unitFont->Width * strlen(unit);
}

// Small black Font12 title above a tile.
static void drawTitle(int x, const char *title) {
  Paint_DrawString_EN(x, TILE_Y, title, &Font12, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
}

// Black band with the city on the left and a short text (time or status) on the right,
// then the yellow accent line. red = true draws the right text white on red (stale data).
static void drawHeader(const char *right, bool red) {
  Paint_DrawRectangle(0, 0, EPD_1IN54G_WIDTH, 20, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawRectangle(0, 21, EPD_1IN54G_WIDTH, 24, EPD_1IN54G_YELLOW, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(4, 2, CITY, &Font16, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  int x = EPD_1IN54G_WIDTH - 4 - Font12.Width * strlen(right);   // right-aligned
  if (red) Paint_DrawString_EN(x, 4, right, &Font12, EPD_1IN54G_RED, EPD_1IN54G_WHITE);
  else     Paint_DrawString_EN(x, 4, right, &Font12, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
}

// Indoor tile at x 126: temperature and humidity side by side (126..198).
// ok = false (sensor read failed) shows "--" instead of numbers.
// A future pressure reading fits on a second line at y 104 ("1013" + Font8 "hPa" = 71 px);
// move DIVIDER_Y down then.
static void drawLocal(const LocalReading &loc, bool ok) {
  char buf[8];
  drawTitle(126, "INDOOR");

  // lroundf here and in setup()'s "indoor changed?" check, so both round the same way
  if (ok) snprintf(buf, sizeof(buf), "%d", (int)lroundf(loc.temp));
  else    snprintf(buf, sizeof(buf), "--");
  int x = drawReading(126, TILE_Y + 13, buf, "C");

  if (ok) snprintf(buf, sizeof(buf), "%d", (int)lroundf(loc.hum));
  else    snprintf(buf, sizeof(buf), "--");
  drawReading(x + 3, TILE_Y + 13, buf, "%");
}

static void drawDivider() {
  Paint_DrawLine(0, DIVIDER_Y, EPD_1IN54G_WIDTH - 1, DIVIDER_Y, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
}

static void startImage() {
  Paint_NewImage(img, EPD_1IN54G_WIDTH, EPD_1IN54G_HEIGHT, 0, EPD_1IN54G_WHITE);
  Paint_SetScale(4);
  Paint_SelectImage(img);
  Paint_Clear(EPD_1IN54G_WHITE);
}

// ---------- bottom area ----------
// Everything below the divider: 200 x 94 px. It shows one "view" per panel refresh and
// moves on to the next view in BOTTOM_ROTATION after each refresh.
// To add a view: add it to the BottomView enum (near struct Weather), write a
// drawBottom...() function for it, add a case in drawBottom(), and list it in BOTTOM_ROTATION.
#define BOTTOM_Y (DIVIDER_Y + 1)                  // 106
#define BOTTOM_W EPD_1IN54G_WIDTH                 // 200
#define BOTTOM_H (EPD_1IN54G_HEIGHT - BOTTOM_Y)   // 94

static const BottomView BOTTOM_ROTATION[] = { VIEW_FORECAST, VIEW_HOURLY, VIEW_IMAGE };
static const uint8_t BOTTOM_VIEWS = sizeof(BOTTOM_ROTATION) / sizeof(BOTTOM_ROTATION[0]);
RTC_DATA_ATTR static uint8_t bottomIndex = 0;   // which entry of BOTTOM_ROTATION is next

static const char *DAY_NAMES[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// Font12 text centred horizontally on cx.
static void drawCentered(int cx, int y, const char *s) {
  Paint_DrawString_EN(cx - Font12.Width * strlen(s) / 2, y, s, &Font12, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
}

// View 1: the next 3 days in three 66 px columns: day name, icon, max/min, chance of rain.
// w = nullptr (no weather ever fetched) or no forecast data shows "No forecast".
static void drawBottomForecast(const Weather *w) {
  if (w == nullptr || !w->haveDays) {
    drawCentered(BOTTOM_W / 2, BOTTOM_Y + (BOTTOM_H - Font12.Height) / 2, "No forecast");
    return;
  }
  char buf[16];
  for (int i = 0; i < 3; i++) {
    const DayForecast &d = w->days[i];
    int x0 = i * 67;          // columns at x 0, 67, 134
    int cx = x0 + 33;         // column centre
    if (i > 0) {              // thin line between columns, at x 66 and 133
      Paint_DrawLine(x0 - 1, BOTTOM_Y + 2, x0 - 1, EPD_1IN54G_HEIGHT - 2, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
    }

    drawCentered(cx, BOTTOM_Y + 2, DAY_NAMES[d.wday % 7]);    // y 108..119
    drawWeatherIcon(d.code, cx, BOTTOM_Y + 31);                // icons span cy-15..cy+25: y 122..162

    // "24/15" + "C" in Font16 is 62 px; longer text such as "-12/-15" drops to Font12 to stay in the column
    snprintf(buf, sizeof(buf), "%d/%d", (int)lroundf(d.tmax), (int)lroundf(d.tmin));
    sFONT *f = strlen(buf) <= 5 ? &Font16 : &Font12;
    drawReading(cx - readingWidth(buf, "C", f) / 2, BOTTOM_Y + 59, buf, "C", f);   // y 165..181

    snprintf(buf, sizeof(buf), "%d", d.rain);
    drawReading(cx - readingWidth(buf, "%", &Font12) / 2, BOTTOM_Y + 80, buf, "%", &Font12);   // y 186..198
  }
}

// Smallest and largest of n values.
static void minMax(const int *v, int n, int &lo, int &hi) {
  lo = hi = v[0];
  for (int i = 1; i < n; i++) {
    if (v[i] < lo) lo = v[i];
    if (v[i] > hi) hi = v[i];
  }
}

// View 2: the next 12 hours as a graph. Temperature is a thick red line, humidity a thin
// black one, each scaled to its own min..max (shown at the top). Hour labels every 3 hours.
//   y 108..119  ranges: "14..19C" red on the left, "45..80%" black on the right
//   y 124..180  plot, y 182 time axis
//   y 186..197  hour labels
// Note: Paint_DrawLine draws 1 px left/up of the given point (2X2 lines: 3 px wide, ending there).
static void drawBottomHourly(const Weather *w) {
  if (w == nullptr || !w->haveHours) {
    drawCentered(BOTTOM_W / 2, BOTTOM_Y + (BOTTOM_H - Font12.Height) / 2, "No hourly data");
    return;
  }
  int t[HOURS_SHOWN], h[HOURS_SHOWN];
  for (int i = 0; i < HOURS_SHOWN; i++) {
    t[i] = w->htemp[i];
    h[i] = w->hhum[i];
  }
  int tLo, tHi, hLo, hHi;
  minMax(t, HOURS_SHOWN, tLo, tHi);
  minMax(h, HOURS_SHOWN, hLo, hHi);

  // Each range is drawn in its line's colour, so the ranges double as the legend.
  char buf[16];
  snprintf(buf, sizeof(buf), "%d..%dC", tLo, tHi);
  Paint_DrawString_EN(4, BOTTOM_Y + 2, buf, &Font12, EPD_1IN54G_WHITE, EPD_1IN54G_RED);
  snprintf(buf, sizeof(buf), "%d..%d%%", hLo, hHi);
  Paint_DrawString_EN(BOTTOM_W - 4 - Font12.Width * strlen(buf), BOTTOM_Y + 2, buf, &Font12,
                      EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);

  // Minimum spans, so a nearly flat day stays a nearly flat line instead of a zig-zag.
  int tSpan = tHi - tLo < 4 ? 4 : tHi - tLo;
  int hSpan = hHi - hLo < 10 ? 10 : hHi - hLo;
  const int X0 = 9, X1 = 192;                         // first and last point
  const int Y0 = BOTTOM_Y + 20, Y1 = BOTTOM_Y + 74;   // top (126) and bottom (180) of the plot

  int px = 0, pt = 0, ph = 0;   // previous point
  for (int i = 0; i < HOURS_SHOWN; i++) {
    int x = X0 + (X1 - X0) * i / (HOURS_SHOWN - 1);
    int ty = Y1 - (Y1 - Y0) * (t[i] - tLo) / tSpan;
    int hy = Y1 - (Y1 - Y0) * (h[i] - hLo) / hSpan;
    if (i > 0) {
      // humidity first, so the red temperature line stays on top where they cross
      Paint_DrawLine(px, ph, x, hy, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
      Paint_DrawLine(px, pt, x, ty, EPD_1IN54G_RED, DOT_PIXEL_2X2, LINE_STYLE_SOLID);
    }
    px = x; pt = ty; ph = hy;
    if (i % 3 == 0) {   // e.g. "21", "00", "03", "06"
      snprintf(buf, sizeof(buf), "%02d", (w->hour0 + i) % 24);
      drawCentered(x, BOTTOM_Y + 80, buf);
    }
  }
  Paint_DrawLine(X0, Y1 + 3, X1, Y1 + 3, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, LINE_STYLE_SOLID);   // time axis, y 182
}

// View 3: a picture from bottom_image.cpp (made with tools/img2epd.ps1), centred in the area.
// The data uses the panel's own 2-bit colour codes, 4 pixels per byte, leftmost pixel in the top bits.
static void drawBottomImage() {
  static_assert(BOTTOM_IMAGE_W <= BOTTOM_W && BOTTOM_IMAGE_H <= BOTTOM_H,
                "bottom_image is larger than the bottom area (200 x 94)");
  const int x0 = (BOTTOM_W - BOTTOM_IMAGE_W) / 2;
  const int y0 = BOTTOM_Y + (BOTTOM_H - BOTTOM_IMAGE_H) / 2;
  const int rowBytes = (BOTTOM_IMAGE_W + 3) / 4;
  for (int y = 0; y < BOTTOM_IMAGE_H; y++) {
    for (int x = 0; x < BOTTOM_IMAGE_W; x++) {
      uint8_t b = bottomImage[y * rowBytes + x / 4];
      uint8_t color = (b >> (6 - 2 * (x % 4))) & 0x3;
      Paint_SetPixel(x0 + x, y0 + y, color);
    }
  }
}

static const char *viewName(BottomView v) {
  switch (v) {
    case VIEW_FORECAST: return "forecast";
    case VIEW_HOURLY:   return "hourly";
    case VIEW_IMAGE:    return "image";
  }
  return "?";
}

// Draws the bottom area with the view that is due now. w may be nullptr (no data yet).
static void drawBottom(const Weather *w) {
  BottomView v = BOTTOM_ROTATION[bottomIndex % BOTTOM_VIEWS];
  LOG("Bottom view: %s\n", viewName(v));
  switch (v) {
    case VIEW_FORECAST: drawBottomForecast(w); break;
    case VIEW_HOURLY:   drawBottomHourly(w);   break;
    case VIEW_IMAGE:    drawBottomImage();     break;
  }
}

// Call after each real panel refresh, so the next refresh shows the next view.
static void nextBottomView() {
  bottomIndex = (bottomIndex + 1) % BOTTOM_VIEWS;
}
// ---------- end of bottom area ----------

// stale = true when the data is old because the last update failed:
// the update time in the header is then drawn white on red.
static void drawWeather(const Weather &w, const LocalReading &loc, bool locOk, bool stale = false) {
  char buf[16];
  startImage();

  snprintf(buf, sizeof(buf), "%s %s", w.ddmm, w.hhmm);
  drawHeader(buf, stale);

  drawWeatherIcon(w.code, ICON_X, ICON_Y);
  // "Partly cloudy" (13 chars x 11 px) is the longest and ends at x 197
  Paint_DrawString_EN(TEXT_X, 27, wmoText(w.code), &Font16, EPD_1IN54G_BLACK, EPD_1IN54G_WHITE);

  // Temperature and humidity: no titles, the units say what they are.
  // "-12.5C" ends at x 131, before the humidity at x 140.
  snprintf(buf, sizeof(buf), "%.1f", w.temp);
  drawReading(TEXT_X, 46, buf, "C");
  snprintf(buf, sizeof(buf), "%d", w.hum);
  drawReading(140, 46, buf, "%");

  // Feels is rounded like the indoor values, so "-12C" (49 px) stays left of the WIND tile.
  drawTitle(2, "FEELS");
  snprintf(buf, sizeof(buf), "%d", (int)lroundf(w.feels));
  drawReading(2, TILE_Y + 13, buf, "C");

  drawTitle(TEXT_X, "WIND");
  snprintf(buf, sizeof(buf), "%d", beaufort(w.wind));
  drawReading(TEXT_X, TILE_Y + 13, buf, "Bft");

  drawLocal(loc, locOk);
  drawDivider();
  drawBottom(&w);
}

// Used when the very first fetch fails and there is no old data to show.
static void drawNoData(const LocalReading &loc, bool locOk) {
  startImage();
  drawHeader("Update failed", true);
  iconUnknown(ICON_X, ICON_Y);
  Paint_DrawString_EN(TEXT_X, 27, "No data", &Font16, EPD_1IN54G_BLACK, EPD_1IN54G_WHITE);
  drawLocal(loc, locOk);
  drawDivider();
  drawBottom(nullptr);
}

// Shown while the WiFi setup portal is open, so the user knows what to join.
static void drawSetupScreen() {
  char buf[24];
  startImage();
  Paint_DrawRectangle(0, 0, EPD_1IN54G_WIDTH, 30, EPD_1IN54G_BLACK, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawRectangle(0, 31, EPD_1IN54G_WIDTH, 35, EPD_1IN54G_YELLOW, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(8, 3, "WIFI SETUP", &Font24, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  Paint_DrawString_EN(8, 48, "Join WiFi:", &Font16, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  Paint_DrawString_EN(8, 68, SETUP_AP_NAME, &Font16, EPD_1IN54G_RED, EPD_1IN54G_WHITE);
  Paint_DrawString_EN(8, 88, "Pass " SETUP_AP_PASS, &Font16, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  Paint_DrawString_EN(8, 114, "Then open:", &Font16, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  Paint_DrawString_EN(8, 134, SETUP_AP_IP, &Font16, EPD_1IN54G_RED, EPD_1IN54G_WHITE);
  snprintf(buf, sizeof(buf), "Open for %u min", (unsigned)(SETUP_PORTAL_SECONDS / 60));
  Paint_DrawString_EN(8, 162, buf, &Font12, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  Paint_DrawString_EN(8, 176, "then weather starts", &Font12, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
}

// Same shutdown steps every time the panel has been woken, so no path leaves it powered.
static void panelOff() {
  EPD_1IN54G_Sleep();          // important: never leave the panel powered
  DEV_Delay_ms(2000);          // Waveshare: at least 2 s after sleep
  DEV_Module_Exit();
}

static void goToSleep(uint32_t seconds) {
  LOG("Deep sleep for %u s\n", (unsigned)seconds);
#if DEBUG_LOG
  Serial.flush();   // let the last line finish printing before the chip sleeps
#endif
  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  esp_deep_sleep_start();
}

void setup() {
#if DEBUG_LOG
  Serial.begin(115200);
  delay(200);
#endif
  LOG("\n=== Wake: cause=%d haveLast=%d errShown=%d failCount=%u ===\n",
      (int)esp_sleep_get_wakeup_cause(), haveLast, errShown, (unsigned)failCount);

  // 0a. WiFi setup portal, only at power-up / reset. A timer wake from deep sleep skips it.
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    LOG("Power-up: showing setup screen, then portal '%s' for %u s\n",
        SETUP_AP_NAME, (unsigned)SETUP_PORTAL_SECONDS);
    if (DEV_Module_Init() == 0) {
      drawSetupScreen();
      EPD_1IN54G_Init();
      EPD_1IN54G_Display(img);
      panelOff();
    } else {
      LOG("DEV_Module_Init failed, portal runs without setup screen\n");
      DEV_Module_Exit();
    }
    bool saved = runWifiSetupPortal(SETUP_PORTAL_SECONDS);
    LOG(saved ? "New WiFi credentials saved\n" : "Setup portal closed, keeping current WiFi\n");
  }

  // 0b. indoor sensor (independent of WiFi, so it is available even if the fetch fails)
  LocalReading loc = {0, 0};
  bool locOk = readLocalSensor(loc);
  if (locOk) LOG("Indoor: %.1f C, %.0f %%\n", loc.temp, loc.hum);
  else       LOG("Indoor sensor read failed\n");

  // 1. WiFi + fetch
  Weather w;
  bool ok = false;
  String ssid, pass;
  getWifiCredentials(ssid, pass);   // saved by the setup portal, or secrets.h
  LOG("Connecting to WiFi '%s'...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) delay(250);

  if (WiFi.status() == WL_CONNECTED) {
    LOG("WiFi connected in %u ms, IP %s, RSSI %d dBm\n",
        (unsigned)(millis() - t0), WiFi.localIP().toString().c_str(), WiFi.RSSI());
    ok = fetchWeather(w);
    LOG("Fetch %s\n", ok ? "OK" : "FAILED");
    if (ok) {
      LOG("Weather: %.1f C (feels %.1f), hum %d%%, wind %.0f km/h (Bft %d), code %d (%s), %s %s\n",
          w.temp, w.feels, w.hum, w.wind, beaufort(w.wind), w.code, wmoText(w.code), w.ddmm, w.hhmm);
      if (!w.haveDays) LOG("No forecast in the reply\n");
      for (int i = 0; i < 3 && w.haveDays; i++) {
        LOG("Day %d: %s code %d, %.0f/%.0f C, rain %d%%\n", i + 1, DAY_NAMES[w.days[i].wday % 7],
            w.days[i].code, w.days[i].tmax, w.days[i].tmin, w.days[i].rain);
      }
      if (!w.haveHours) LOG("No hourly data in the reply\n");
      for (int i = 0; i < HOURS_SHOWN && w.haveHours; i++) {
        LOG("Hour %02d: %d C, %u%%\n", (w.hour0 + i) % 24, w.htemp[i], (unsigned)w.hhum[i]);
      }
    }
  } else {
    LOG("WiFi failed after %u ms (status %d)\n", (unsigned)(millis() - t0), (int)WiFi.status());
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Count failures in a row; a good fetch starts the count again.
  if (ok) failCount = 0;
  else if (failCount < MAX_FAIL_COUNT) failCount++;
  if (!ok) LOG("Failure %u in a row, next retry in %u min\n", (unsigned)failCount, (unsigned)(retrySeconds() / 60));

  // Would the indoor box look different from what is on the screen now?
  int newTemp = (int)lroundf(loc.temp);
  int newHum = (int)lroundf(loc.hum);
  bool indoorChanged = (locOk != shownLocOk) ||
                       (locOk && (newTemp != shownTemp || newHum != shownHum));

  if (ok) {
    lastW = w;       // remember for later failed attempts (survives deep sleep)
    haveLast = true;
  } else if (errShown && !indoorChanged) {
    LOG("Still failing, indoor unchanged, red 'Updated' line already on screen: skipping panel refresh\n");
    // Nothing on the screen would change; don't redo a 15-20 s refresh every retry.
    // If the indoor reading changed, fall through and redraw the old weather with the new indoor values.
    // Wake the panel by reset (image stays) so it can be put to sleep the same way.
    // Not needed while the panel is already asleep from the last run:
    // DEV_Module_Init();
    // EPD_1IN54G_Init();
    // panelOff();
    goToSleep(retrySeconds());
  }

  // 2. draw + refresh
  if (DEV_Module_Init() != 0) {
    LOG("DEV_Module_Init failed\n");
    DEV_Module_Exit();           // panel not initialised, so skip its sleep command (it would wait on BUSY)
    goToSleep(retrySeconds());
  }
  if (ok) {
    LOG("Drawing fresh weather\n");
    drawWeather(w, loc, locOk);
  } else {
    // redraw the last known weather (or a "No data" page) with the red "Updated" line
    LOG("Drawing %s with red status line\n", haveLast ? "last known weather" : "'No data' page");
    if (haveLast) drawWeather(lastW, loc, locOk, true); else drawNoData(loc, locOk);
  }
  errShown = !ok;
#if DEBUG_LOG
  uint32_t tr = millis();   // only used for the refresh timing log
#endif
  LOG("Refreshing panel (takes ~15-20 s)...\n");
  EPD_1IN54G_Init();
  EPD_1IN54G_Display(img);
  LOG("Refresh done in %u ms\n", (unsigned)(millis() - tr));
  panelOff();
  LOG("Panel asleep\n");

  // The panel now shows this indoor reading; remember it for the next "changed?" check.
  shownLocOk = locOk;
  shownTemp = newTemp;
  shownHum = newHum;
  // The bottom area alternates: the next refresh shows the next view.
  nextBottomView();

  // 3. sleep until next refresh (retry sooner if this attempt failed)
  goToSleep(ok ? REFRESH_MINUTES * 60 : retrySeconds());
}

void loop() {}
