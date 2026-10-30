// Weather on a 1.54" 4-color e-Paper (G) with ESP32
// Version 0.3 (see "Version history" in README.md)
//
// Wakes up, connects to WiFi, fetches current weather from Open-Meteo
// (no API key needed), redraws the screen, puts the panel to sleep, then
// deep-sleeps the ESP32 until the next refresh.
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

// ---------- settings ----------

const char *CITY = "ATHENS";      // header text (ASCII only)
const float LAT = 37.98;
const float LON = 23.73;

// Full refresh takes ~15-20 s and Waveshare recommends >= 3 min between
// refreshes. Keep this at 3 or more.
const uint32_t REFRESH_MINUTES = 15;
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

struct Weather {
  float temp, feels, wind;
  int hum, code;
  char hhmm[6];
  char ddmm[6];
};

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
static int beaufort(float kmh) {
  static const float LIMITS[12] = {1, 6, 12, 20, 29, 39, 50, 62, 75, 89, 103, 118};
  int b = 0;
  while (b < 12 && kmh >= LIMITS[b]) b++;
  return b;
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

  char url[300];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
           "wind_speed_10m,weather_code&timezone=auto",
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
  return true;
}

// Screen layout (200 x 200). Weather uses the top part down to the divider;
// everything below it (y 106..199) is free for other information.
//   y  0..20  black header: city left, update time right
//   y 21..23  yellow accent line
//   y 26..66  icon left (cx 26), description right of it, temp + humidity below that
//   y 69..102 tiles: FEELS (x 2), WIND (x 54, under the temp), INDOOR (x 126)
//   y 105     divider
// Note: Paint_DrawString_EN takes the BACKGROUND colour first, then the TEXT colour.
#define ICON_X    26
#define ICON_Y    41
#define TEXT_X    54    // description, temperature and the WIND tile start here
#define TILE_Y    69    // tile titles; values go 13 px lower
#define DIVIDER_Y 105

// One reading: the number in big black Font20 and its unit in small red Font12,
// bottoms lined up. Returns the x just after the unit, so the next value can follow.
// Font20 is 14 px and Font12 7 px wide; a reading wraps if it runs past x 199.
static int drawReading(int x, int y, const char *num, const char *unit, sFONT *unitFont = &Font12) {
  Paint_DrawString_EN(x, y, num, &Font20, EPD_1IN54G_WHITE, EPD_1IN54G_BLACK);
  x += Font20.Width * strlen(num);
  Paint_DrawString_EN(x, y + Font20.Height - unitFont->Height, unit, unitFont, EPD_1IN54G_WHITE, EPD_1IN54G_RED);
  return x + unitFont->Width * strlen(unit);
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
}

// Used when the very first fetch fails and there is no old data to show.
static void drawNoData(const LocalReading &loc, bool locOk) {
  startImage();
  drawHeader("Update failed", true);
  iconUnknown(ICON_X, ICON_Y);
  Paint_DrawString_EN(TEXT_X, 27, "No data", &Font16, EPD_1IN54G_BLACK, EPD_1IN54G_WHITE);
  drawLocal(loc, locOk);
  drawDivider();
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

  // 3. sleep until next refresh (retry sooner if this attempt failed)
  goToSleep(ok ? REFRESH_MINUTES * 60 : retrySeconds());
}

void loop() {}
