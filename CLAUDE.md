# ESP32 e-Paper Weather Station

Arduino sketch for an ESP32 Dev Module driving a Waveshare 1.54" (G) 4-colour e-paper
(200×200, black/white/yellow/red), with a DHT22 (indoor T/H) and a BMP280 (pressure).
Weather comes from Open-Meteo (no API key). Each wake: read sensors, fetch, redraw,
sleep the panel, deep-sleep the ESP32.

`README.md` is the full user documentation (layout, wiring, setup portal, version history).
`TODO.md` holds planned work with design notes. Read the relevant part before changing behaviour.

## Building

- The user compiles and uploads in the **Arduino IDE**. Do not run `arduino-cli compile`
  or any other build; check changes by reading the code carefully instead.
- Board: ESP32 Dev Module. Libraries: ArduinoJson v7, Adafruit DHT sensor library,
  Adafruit BMP280 Library.
- The sketch folder must stay named `ESP32_WEATHER_EPAPER/` to match the `.ino`.

## Layout

All firmware is in `ESP32_WEATHER_EPAPER/`:

| File | Role |
|---|---|
| `ESP32_WEATHER_EPAPER.ino` | Settings block, RTC state, fetch (`fetchWeather`), all drawing, bottom-area views, sleep/retry flow in `setup()` (`loop()` is empty) |
| `local_sensor.*` | DHT22 + BMP280 reading, sea-level conversion |
| `wifi_setup.*` | Setup portal (AP `Weather-Setup`, web page) |
| `settings.*` | `StationSettings` in NVS, `LOCATION_PRESETS`, `flags` bits, `REFRESH_*` limits |
| `panel.h` | The only place that names the panel: size, `COL_*` colours, `panelInit/Display/Sleep` |
| `weather_icons.*` | Icons per WMO code |
| `bottom_image.*` | **Generated** by `tools/img2epd.ps1` — never edit by hand |
| `EPD_1in54g.*`, `DEV_Config.*`, `GUI_Paint.*`, `font*.cpp`, `fonts.h`, `Debug.h`, `ImageData.*` | Waveshare vendor driver/library — leave alone unless asked |
| `secrets.h` | Optional, gitignored; template is `secrets.example.h` |

Files in the repo root ending in `.bak*` or `.org` are untracked local backups (some contain
WiFi passwords). Ignore them and never commit them.

## Code conventions

- Drawing code uses `EPD_WIDTH`/`EPD_HEIGHT` and `COL_*` from `panel.h`, never the
  `EPD_1IN54G_*` names directly.
- Layout positions are `#define`s near the drawing code (`ICON_X`, `DIVIDER_Y`, `BOTTOM_*`, ...),
  with a comment giving the resulting pixel value.
- Values that must survive deep sleep are `RTC_DATA_ATTR static`. RTC memory is small, so
  keep these compact (e.g. `int8_t`/`uint8_t` arrays, minutes as `uint16_t`, not strings).
- User settings live in NVS via `settings.*`. New on/off options get a `FLAG_*` bit in
  `settings.h`, a portal checkbox, and (for views) a line in `viewEnabled()`.
- New bottom view: add to `BottomView`, write `drawBottom...()`, add a `case` in
  `drawBottom()`, list it in `BOTTOM_ROTATION`.
- Types used in function signatures in the `.ino` must be defined before the first function,
  because the Arduino IDE auto-generates prototypes at the top.
- Logging only through `LOG(...)` (controlled by `DEBUG_LOG` in the `.ino`). It is a `#define`
  in the `.ino`, so other `.cpp` files can't see it.
- Missing values are drawn as `--`; numbers in black, units in red.
- Respect Waveshare's ≥ 3 min between refreshes (`REFRESH_MIN_MINUTES`) and avoid
  needless refreshes (each takes 15-20 s and costs battery).
- Pins: GPIO12 stays free (strapping pin). GPIO33 is the push button (`BUTTON_PIN`, ext0 wake from deep sleep).
- Comments are short, plain English, and explain *why*. File headers describe the module's job.

## Docs and versions

- Keep `README.md` in sync with any change to behaviour, settings, wiring or screen layout.
  Pin changes must be updated in the Mermaid diagram, the README tables and the wiring
  comment at the top of the `.ino`.
- A release bumps the version in the `.ino` header comment (`// Version X.Y`), in README
  ("Current version") and adds a "Version history" entry. Commit messages follow the pattern
  `Update version to X.Y; <summary>`.
- Mark finished items in `TODO.md` (or remove the section) when a feature lands.
- Work happens on feature branches merged into `main` via pull requests.
