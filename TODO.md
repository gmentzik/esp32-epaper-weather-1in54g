# TODO

## Suggested order for the next releases
One feature per release (own branch, version bump, README update, PR into `main`):
1. Fetch less (below): `forecast_hours`, faster WiFi connect, clock, skip the fetch while fresh.
   It saves battery right away and adds the clock the moon needs.
2. Day length change on view 3.
3. Sky view, moon part.
4. Sky view, cloud tonight + UV.
5. Sky view, moonrise / moonset (hardest, so last).

## Fetch less
Each wake does one HTTPS request: WiFi connect (usually 1-3 s with DHCP), TLS handshake,
then the whole reply into a `String`. Most of a wake's energy goes to the panel refresh
(15-20 s) and the WiFi + TLS setup; the size of the reply matters less. So the biggest wins
are skipping the fetch when the data is still fresh and connecting faster. Trimming fields
is a smaller, easy win. New features must not add a second request: their fields go into
the same one.

What is wasted today:
- `hourly` with `forecast_days=3` returns 72 rows (plus 72 time strings), but only
  `HOURS_SHOWN` = 12 are used. That is most of the reply.
- `daily` (3-day forecast, sunrise/sunset) changes a few times a day, but comes every wake.
- A button press right after a refresh fetches again, though Open-Meteo's `current` values
  only change every 15 min.

- [ ] **Ask only for the hours shown:** `&forecast_hours=12` instead of 3 days of hourly data.
      The reply then starts at the current hour, so the index check in `fetchWeather()` gets
      simpler. Cuts ~2 KB of download and parsing. Check the reply in a browser first.
- [ ] **Faster WiFi connect:** after a good connect, keep the channel and BSSID in RTC memory
      and pass them to `WiFi.begin(ssid, pass, channel, bssid)` next time. If that fails,
      connect the normal way. Connecting drops from ~1-3 s to well under 1 s.
      Optional: static IP (skips DHCP), as a portal setting.
- [ ] **Clock:** after a good fetch, set the ESP32 clock from `current.time` and
      `utc_offset_seconds` (`settimeofday`). It keeps counting in deep sleep (drifts a little,
      fine for this). Also needed by the 3-minute rule (push button) and the moon.
- [ ] **Skip the fetch while the data is fresh:** keep the time of the last good fetch in RTC.
      If a wake comes less than 15 min after it (button press, or refresh period < 15 min),
      don't start WiFi. Redraw from `lastW` with fresh indoor values and the next bottom view.
- [ ] **Slow data less often:** request `&daily=` only when the date changed or the last
      daily data is over ~3 h old; keep the last values in `lastW` in between. Small saving,
      do it last or skip it.
- [ ] Optional: parse straight from the stream with an ArduinoJson filter instead of
      `getString()`. Saves RAM, not battery.
- [ ] Optional: try plain `http://` (the data is public and the certificate is not checked
      anyway). Skips the TLS handshake. Only if Open-Meteo answers on http.

## Sky tonight view and day length change
A new bottom view with the moon and the night sky, plus the day length change on view 3.
Everything is computed on the ESP32 or comes from extra fields in the existing request, so
there is no extra network cost. Each step below is its own release.

1. [ ] **Day length change on view 3:** `DAY +2m` in Font12 under the sun line, only the
       change, not the day length. Tomorrow's `daylight_duration` minus today's, rounded to
       minutes, sign always shown (`+2m`, `-3m`; `0m` near the solstices and in polar
       day/night; `DAY --` if missing). Only `int16_t dayDeltaSec` in RTC. The rows above
       move up ~5 px to make room. If that looks too cramped on the panel, put `+2m` in the
       sun line instead of the moon icon.
2. [ ] **Sky view, moon part:** new `VIEW_SKY` before the picture (picture becomes view 5),
       with `FLAG_SKY_VIEW` (default on), a portal checkbox and a line in `viewEnabled()`.
       Shows phase name, % lit, age, next full / new moon. Computed from the date (Meeus
       ch. 49), no network; needs the clock from "Fetch less".
       Icon `iconMoonPhase()`: empty ring at new moon, black fills in as more of it is lit.
       At full moon a thick 3 px yellow ring (the only yellow, as yellow is hard to see on
       this panel).
3. [ ] **Cloud tonight + UV:** add `cloud_cover` to `hourly` (average of 21:00-02:00, so
       `forecast_hours` must reach 02:00 of the next night) and `uv_index_max` to `daily`.
       UV word: LOW 0-2, MOD 3-5, HIGH 6-7, V.HIGH 8-10, EXTREME 11+. One byte each in RTC.
4. [ ] **Moonrise / moonset:** low-precision moon position (Meeus ch. 47, ~10 terms), step
       through the local day every 20 min, find where it crosses the horizon
       (h0 = +0.125°), then refine. About ±5-10 min. A day with no rise or set shows `--:--`.
       Computed at draw time, nothing in RTC.

Layout of the full view (200 x 94, y 106..199):
```
|( )  WAXING GIBBOUS          |  phase name
|( 20 ) 78%     age 9d        |  % lit, age
|( r )  FULL 04/12 NEW 19/12  |  next full / new moon
|-----------------------------|
|RISE 15:42     SET 03:10     |  step 4
|CLOUD 20%      UV 6 HIGH     |  step 3
```
Until steps 3 and 4 land, leave those rows out or draw the moon part larger.
Tides were considered and dropped: they need a second request, and Mediterranean tides are
only ±10-20 cm.

## Bitmap bottom screen: fetch from local network
The bitmap view can already be turned on/off in the setup portal. Next: fetch a prepared
bitmap from the local network with custom information, similar to other e-ink solutions.
When the view is enabled, the user enters the URL to fetch the image from at setup.

- [ ] URL field for fetching the image from the local network.

## Push button: refresh now / open setup
One button on the case, GPIO33 to GND (README "Push button").
- **Press: refresh now** (done in 0.11): wakes from deep sleep, forced refresh, next bottom view.
- **Long press (hold 2-4 s):** open the setup portal, same as after a reset. Not planned for now.
  To be discussed: is this worth it, or is a reset button / power switch better? See "Options" below.

### Hardware
- Done: button between **GPIO33** and **GND**, internal pull-up, so no resistor needed.
  Optional 100 kΩ pull-up to 3V3 + 100 nF across the button for noisy setups (in README).
- Optional feedback LED: many dev boards have one on GPIO2. Blinking it once the long press
  counts lets the user know to let go, since the e-paper takes 15-20 s to show anything.

### Firmware
- [x] **Wake on the button:** `goToSleep()` arms ext0 on GPIO33 (wake on LOW) with the RTC
      pull-up, as the last step before sleep. Not armed if the button is held at that moment.
- [x] **Setup check:** button wakes (`ESP_SLEEP_WAKEUP_EXT0`) don't open the portal.
- [x] **Forced refresh:** a button wake always redraws, even when the
      "still failing, indoor unchanged" check would skip it.
- [x] **Next timer:** after a button refresh it sleeps a full period (normal flow).
- [x] Presses while the ESP32 is awake are ignored (the wake source is only armed for deep sleep).
- [x] Docs: wiring comment in the `.ino`, README "Push button" and "Pins to keep free".
- [ ] **3-minute rule (later, if needed):** Waveshare asks for >= 3 min between refreshes; now a
      press right after a refresh starts another one. Fix: keep the time of the last refresh in
      RTC memory (`time(nullptr)` keeps counting in deep sleep). On a button wake less than
      `REFRESH_MIN_MINUTES` after it, set an RTC "pending" flag and sleep until the 3 min mark
      without reading sensors or WiFi. On that wake do the forced refresh.
- [ ] **Press length (only with long press):** on `ESP_SLEEP_WAKEUP_EXT0`, read the pin in a loop.
      Released before 2 s = short press, still low at 2 s = long press. Measure from `millis()`
      (time since boot, ~0.2-0.3 s boot), ignore presses under ~50 ms (bounce).
- [ ] **Long press = portal without reset:** unlike EN, RTC memory is kept, so the last weather,
      failure count and bottom view survive. After the portal, carry on with a normal refresh.

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
The .ino (~830 lines) mixes fetching, drawing and the wake/sleep flow. Move the first
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
- Pass `cfg` (city, lat, lon, flags) and `SETUP_PORTAL_SECONDS` to the screen functions as parameters. `setup()`
  uses `img` directly: expose it, or add a `showImage()` that does init + display
  (this also fits the "Panel calls" wrapper above).
- `DAY_NAMES` is used by the forecast log in `setup()`: expose it or move that log.
- Public functions drop `static`; default arguments go in the header declaration only.
- The "define `BottomView` early for the IDE prototypes" note can go, since .cpp files
  get no auto-generated prototypes.

## Look up the city automatically (geocoding)
The portal already takes the name, LAT and LON by hand (plus preset buttons). Add an
automatic lookup as an extra option; manual entry stays as the fallback when the API
can't be reached or finds nothing.
The forecast API only takes coordinates, but Open-Meteo's free Geocoding API (no key)
turns a name into them:
`https://geocoding-api.open-meteo.com/v1/search?name=Athens&count=1&language=en&format=json`
(`results[0]` has `name`, `latitude`, `longitude`, `country`; sorted by population,
optional `&countryCode=GR`).

- [ ] **Geocode once:** the portal runs in AP mode without internet, so look the city up
      on the next normal wake (already on WiFi). Save `lat`, `lon` and the returned name
      in NVS; later wakes just read them. Only look up again when the city changes.
- [ ] **Fallback:** if the lookup fails or finds nothing, keep the saved coordinates
      (entered by hand or from a preset) and retry on a later wake.

Things to handle:
- Geocoder names are mixed case with accents: uppercase them and turn accented letters
  into plain ones (e.g. "São Paulo" -> "SAO PAULO"). `cleanCityName()` only replaces
  them with `?` today.
- Same-name towns: log the matched country; maybe an optional country field in the portal.
