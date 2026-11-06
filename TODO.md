# TODO

## Make bitmap bottom screen optional.
Since not practical use at the moment make it so it can be disabled from setup screen.
Later it will be used to fetch a prepared bitmap from local network with custom information similar to other e-ink solutions out there.
When enabled user may also put the url path to fetch the image at setup.

- [x] Checkbox in the setup portal, stored as bit 0 of the `flags` byte (default off).
- [ ] URL field for fetching the image from the local network.

## Push button: refresh now / open setup
One button on the case:
- **Short press:** refresh the screen now (same as a timer wake: sensor, fetch, redraw).
- **Long press (hold 2-4 s):** open the setup portal, same as after a reset.
  To be discussed: is this worth it, or is a reset button / power switch better? See "Options" below.

### Hardware
- Push button (normally open) between **GPIO33** and **GND**.
- GPIO33 is a free RTC pin, so it can wake the ESP32 from deep sleep, and it has an internal
  pull-up. It is not a strapping pin and is not on the I2C pins (21/22) kept for the pressure sensor.
- Optional: an external 100 kΩ pull-up to 3V3 and a 100 nF capacitor across the button,
  for hardware debounce and a pull-up that doesn't depend on the RTC settings.
- Avoid GPIO0/2/12/15 (strapping pins). GPIO34-39 only work with an external pull-up
  (they have no internal one).
- Optional feedback LED: many dev boards have one on GPIO2. Blinking it once the long press
  counts lets the user know to let go, since the e-paper takes 15-20 s to show anything.

### Firmware
- [ ] **Wake on the button:** in `goToSleep()`, also `esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0)`
      (wake when the pin is low) plus `rtc_gpio_pullup_en()` / `rtc_gpio_pulldown_dis()` on
      GPIO33. With ext0 the RTC peripherals stay powered, which costs a few µA.
- [ ] **Press length:** on `ESP_SLEEP_WAKEUP_EXT0`, read the pin in a loop. Released before
      2 s = short press, still low at 2 s = long press (no need to wait for 4 s). The ESP32 takes
      ~0.2-0.3 s to boot, so measure from `millis()` (time since boot), not from `setup()` start.
      Ignore presses under ~50 ms (bounce).
- [ ] **Setup check must change:** `setup()` now opens the portal whenever the wake cause is
      not `ESP_SLEEP_WAKEUP_TIMER`, so a button wake would always open it. Open it only on
      power-up / reset (`ESP_SLEEP_WAKEUP_UNDEFINED`) or on a long press.
- [ ] **Short press = forced refresh:** always redraw the panel, even when the
      "still failing, indoor unchanged" check would skip it.
- [ ] **Long press = portal without reset:** unlike EN, RTC memory is kept, so the last weather,
      failure count and bottom view survive. After the portal, carry on with a normal refresh.
- [ ] **Next timer:** after a button refresh, sleep a full `REFRESH_MINUTES` again.
- [ ] **Too many refreshes:** Waveshare asks for >= 3 min between refreshes. Either accept that
      a button press is rare, or ignore short presses within ~1 min of the last refresh
      (keep the last refresh time in RTC memory; no clock, so track it with sleep time).
- [ ] Presses while the ESP32 is awake (during the 15-20 s refresh) are ignored. OK, or poll
      the pin at the end of `setup()` and refresh once more.
- [ ] Docs: wiring comment in the `.ino`, README wiring table and "Pins to keep free".

### Options (to discuss)
| Option | Refresh now | Setup | Cost / notes |
|---|---|---|---|
| A. One button on GPIO33 (above) | short press | long press, keeps RTC data | 1 button, most firmware work |
| B. Button on GPIO33 + button on **EN** | GPIO33 | EN = reset, no firmware needed | 2 buttons. Same as the board's EN, but reachable from outside the case; clears RTC data |
| C. Button on GPIO33 + power switch | GPIO33 | switch off/on | Switch is useful anyway on battery (no drain when stored); setup needs a power cycle |

Suggestion: A, plus a power switch if it runs on battery. The switch is for storage, not
for setup: a long press does setup without losing the last weather. B is the fallback if
press-length detection proves unreliable.

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
(Done: name / LAT / LON fields in the portal, "Fill in ATHENS" preset button, presets in `settings.cpp`.)
As 2nd option: Auto search will be an additional option. That way user can cover cases of API failure to connect / or fail to find.

Things to handle:
- Header room: Font16 is 11 px wide and the date/time starts near x 119, so about
  10 characters fit. Cut longer names.
- Header is ASCII only: uppercase the name and replace or drop non-ASCII characters
  (e.g. "Sao Paulo", "Zurich").
- Same-name towns: log the matched country; maybe an optional country field in the portal.
- Optional: let the portal take lat/lon directly for places geocoding doesn't know.
