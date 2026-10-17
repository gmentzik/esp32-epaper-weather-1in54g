# ESP32 e-Paper Weather Station

A low-power weather station built on an ESP32 and a Waveshare 1.54" (G) 4-colour e-paper display.
It shows the current outdoor weather from an online forecast service together with indoor
temperature and humidity from a local sensor. Between updates the ESP32 deep-sleeps and the
panel is powered down, so the image stays on screen while drawing almost no power.

## What it shows

- City name in the header
- Outdoor temperature, weather condition text and icon
- "Feels like" temperature, humidity and wind speed
- Indoor temperature and humidity (`--` if the sensor can't be read)
- Time and date of the last successful update

## How it works

Each time the ESP32 wakes up it:

1. Reads the indoor sensor (DHT22).
2. Connects to WiFi and fetches current weather from [Open-Meteo](https://open-meteo.com/) (no API key needed).
3. Redraws the display, puts the panel to sleep, then deep-sleeps until the next update.

When the update fails (no WiFi or the API is unreachable):

- **After at least one good update:** the last known weather stays on screen and the
  "Updated ..." line turns white on red.
- **If no data was ever fetched:** a "No data / Update failed" page is shown.
- **Retries back off:** each failure in a row waits one step longer (1, 2, 3 ... minutes,
  up to 15 steps), so a long outage doesn't drain the battery.
- **No needless refreshes:** while it keeps failing, the panel is only refreshed again
  if the indoor reading on screen would change. Each full refresh takes 15-20 s.

Values that must survive deep sleep (last weather, failure count, what is on screen) are kept in RTC memory.
A reset or power loss clears them.

## Hardware

| Part | Notes |
|---|---|
| ESP32 dev board | "ESP32 Dev Module" (ESP32-WROOM-32) |
| Waveshare 1.54" e-Paper (G) | 200×200, black / white / yellow / red |
| DHT22 (AM2302) module | temperature and humidity |
| Jumper wires, USB cable | |

## Wiring

GPIO numbers match the `Dxx` labels on most ESP32 dev boards.

### e-Paper display

| e-Paper pin | ESP32 pin | Function |
|---|---|---|
| VCC  | 3V3 | 3.3 V |
| GND  | GND | |
| DIN  | GPIO14 | SPI MOSI |
| CLK  | GPIO13 | SPI clock |
| CS   | GPIO15 | chip select |
| DC   | GPIO27 | data / command |
| RST  | GPIO26 | reset |
| BUSY | GPIO25 | busy flag |

Set the **BS** jumper on the display board to **0** (4-line SPI). The pins are defined in `DEV_Config.h`.

### DHT22 sensor

| DHT22 pin | ESP32 pin | Note |
|---|---|---|
| + / VCC | 3V3 | Use 3.3 V, not 5 V, so the data line stays at a safe level |
| OUT / DATA | GPIO32 | Set by `DHT22_PIN` in `local_sensor.cpp` |
| - / GND | GND | |

Most DHT22 modules already have a pull-up resistor on the data line.
A bare 4-pin sensor needs about 10 kΩ between DATA and 3V3.

### Pins to keep free

- **GPIO21 (SDA) / GPIO22 (SCL):** reserved for a future I2C sensor (e.g. pressure).
- **GPIO12:** leave unconnected. It is a strapping pin and can block boot or flashing.

## Software setup

1. **Arduino IDE** with the ESP32 board package (Espressif) installed.
2. **Libraries** (Library Manager):
   - `ArduinoJson` (v7)
   - `DHT sensor library` by Adafruit (also install `Adafruit Unified Sensor` when asked)
3. **WiFi credentials:** copy `secrets.example.h` to `secrets.h` in the same folder and fill in
   your WiFi name and password. `secrets.h` is in `.gitignore`, so it is never committed.
4. **Settings** at the top of `ESP32_WEATHER_EPAPER.ino`:

   | Setting | Default | Meaning |
   |---|---|---|
   | `CITY` | `"ATHENS"` | Header text (ASCII only) |
   | `LAT`, `LON` | `37.98`, `23.73` | Location for the forecast |
   | `REFRESH_MINUTES` | `15` | Time between updates. Keep it at 3 or more (Waveshare recommendation) |
   | `RETRY_MINUTES` | `1` | Base retry wait after a failure |
   | `MAX_FAIL_COUNT` | `15` | Maximum backoff steps |
   | `DEBUG_LOG` | `1` | `1` = progress on the Serial Monitor (115200 baud), `0` = silent for battery use |

5. **Board:** Tools → Board → *ESP32 Dev Module*, select the COM port, then Upload.

The folder must be named `ESP32_WEATHER_EPAPER`, the same as the `.ino` file, for the Arduino IDE to open it.

## First start

1. Open the Serial Monitor at 115200 baud.
2. Press EN (reset) on the ESP32.
3. You should see the indoor reading, `WiFi connected`, `Fetch OK`, and then the panel
   refresh, which takes about 15-20 s.
4. The ESP32 then deep-sleeps for `REFRESH_MINUTES` and repeats.

**Troubleshooting**

| Symptom | Check |
|---|---|
| Upload fails with "Wrong boot mode" | Hold BOOT, tap EN, release BOOT, then upload again |
| `Indoor sensor read failed`, `--` on screen | DHT22 data wire on GPIO32, 3V3 power, pull-up resistor |
| `WiFi failed` | SSID and password in `secrets.h`; the ESP32 only supports 2.4 GHz WiFi |
| Display stays blank | Wiring, BS jumper at 0, BUSY pin connected |

## Project files

| File | Purpose |
|---|---|
| `ESP32_WEATHER_EPAPER.ino` | Main program: WiFi, fetch, layout, sleep and retry logic |
| `local_sensor.cpp/.h` | Indoor sensor (DHT22) reading |
| `weather_icons.cpp/.h` | Weather icons drawn for each condition |
| `secrets.example.h` | Template for `secrets.h` (WiFi credentials) |
| `EPD_1in54g.*`, `DEV_Config.*`, `GUI_Paint.*`, `font*.cpp`, `fonts.h` | Waveshare display driver and drawing library |
| `TODO.md` | Planned improvements |

## Other displays

The drawing code is written for the 1.54" (G) panel: 200×200 pixels, 4 colours and
Waveshare's `EPD_1IN54G_*` driver calls. Other Waveshare panels can be supported by swapping
the driver files and adjusting colours and layout. See `TODO.md` for the planned clean-up
that will make this easier.
