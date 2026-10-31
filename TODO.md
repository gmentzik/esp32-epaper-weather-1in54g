# TODO

## Make bitmap bottom screen optional.
Since not practical use at the moment make it so it can be disabled from setup screen.
Later it will be used to fetch a prepared bitmap from local network with custom information similar to other e-ink solutions out there.
When enabled user may also put the url path to fetch the image at setup.

## Make the code easier to port to other e-paper displays
The drawing code is currently tied to the Waveshare 1.54" (G) 4-color panel
(about 47 uses of `EPD_1IN54G_*` in the .ino and weather_icons.cpp).

- [ ] **Colour names:** define `COL_BG`, `COL_TEXT` and `COL_ACCENT` once, instead of
      using `EPD_1IN54G_*` everywhere. A black/white panel then only needs these remapped.
- [ ] **Panel calls:** put the init, display and sleep calls (`EPD_1IN54G_Init`,
      `EPD_1IN54G_Display`, `EPD_1IN54G_Sleep`) in one small wrapper, so a new panel
      only changes that wrapper.
- [ ] **Layout:** express positions in terms of `EPD_WIDTH` and `EPD_HEIGHT` where
      possible, instead of hard-coded 200x200 coordinates.

## Split the .ino into modules
The .ino (~720 lines) mixes fetching, drawing and the wake/sleep flow. Move the first
two into their own files, like `local_sensor` and `wifi_setup`. Only moves code, no
behaviour change.

- [ ] **`weather_fetch.h/.cpp`:** `HOURS_SHOWN`, `DayForecast`, `Weather`, `fetchWeather()`
      (take `lat, lon` as parameters) and its helpers `wmoText()`, `beaufort()`, `dayOfWeek()`.
- [ ] **`screen.h/.cpp`:** the `img` buffer, layout `#define`s, `startImage()`, the drawing
      helpers (`drawReading`, `drawHeader`, `drawLocal`, `drawWeatherIcon`, ...) and the
      whole bottom area (views, rotation, `bottomIndex`). Public: `drawWeather()`,
      `drawNoData()`, `drawSetupScreen()`, `nextBottomView()`.
- [ ] **Keep in the .ino:** wiring comment, settings, RTC state, `retrySeconds()`,
      `panelOff()`, `goToSleep()`, `setup()`.

Things to handle:
- `LOG` / `DEBUG_LOG` must move to a small `log.h`, since a `#define` in the .ino is not
  seen by other .cpp files (so the switch leaves the settings block).
- Pass `CITY` and `SETUP_PORTAL_SECONDS` to the screen functions as parameters. `setup()`
  uses `img` directly: expose it, or add a `showImage()` that does init + display
  (this also fits the "Panel calls" wrapper above).
- `DAY_NAMES` is used by the forecast log in `setup()`: expose it or move that log.
- Public functions drop `static`; default arguments go in the header declaration only.
- The "define `BottomView` early for the IDE prototypes" note can go, since .cpp files
  get no auto-generated prototypes.

## Set the city in the setup portal
`CITY`, `LAT` and `LON` are hard-coded. The forecast API only takes coordinates, but
Open-Meteo's free Geocoding API (no key) turns a name into them:
`https://geocoding-api.open-meteo.com/v1/search?name=Athens&count=1&language=en&format=json`
(`results[0]` has `name`, `latitude`, `longitude`, `country`; sorted by population,
optional `&countryCode=GR`).

- [ ] **Portal:** add a "City" field next to the WiFi name and password, saved in NVS.
- [ ] **Geocode once:** the portal runs in AP mode without internet, so look the city up
      on the next normal wake (already on WiFi). Save `lat`, `lon` and the returned name
      in NVS; later wakes just read them. Only look up again when the city changes.
- [ ] **Fallback:** if the lookup fails or finds nothing, keep the old coordinates (or the
      `LAT`/`LON` defaults) and retry on a later wake.

As 1st option: User can search the Lat Lon and in config form set the name , Lat and Lon.
As 2nd option: Auto search will be an additional option. That way user can cover cases of API failure to connect / or fail to find.

Things to handle:
- Header room: Font16 is 11 px wide and the date/time starts near x 119, so about
  10 characters fit. Cut longer names.
- Header is ASCII only: uppercase the name and replace or drop non-ASCII characters
  (e.g. "Sao Paulo", "Zurich").
- Same-name towns: log the matched country; maybe an optional country field in the portal.
- Optional: let the portal take lat/lon directly for places geocoding doesn't know.
