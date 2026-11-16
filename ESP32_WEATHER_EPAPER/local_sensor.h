// Indoor (local) sensors:
//  - temperature and humidity from a DHT22 on GPIO32
//  - air pressure from a BMP280 on I2C (SDA GPIO21, SCL GPIO22)
// Needs the "DHT sensor library" and the "Adafruit BMP280 Library" by Adafruit (Library Manager).

#ifndef LOCAL_SENSOR_H
#define LOCAL_SENSOR_H

struct LocalReading {
  float temp;       // degrees C (DHT22)
  float hum;        // % relative humidity (DHT22)
  float pressure;           // hPa at the sensor ("station pressure", BMP280), NAN = not read. Never converted.
  float seaLevelPressure;   // pressure converted to sea level, set by setup(); NAN until the
                            // location's height is known (first good fetch) or if not read
};

// DHT22: fills r.temp / r.hum and returns true on success, false if it could not be read.
bool readLocalSensor(LocalReading &r);

// BMP280: fills r.pressure (hPa at the sensor, "station pressure") and returns true on
// success. On failure r.pressure is set to NAN. r.seaLevelPressure is not touched.
bool readPressureSensor(LocalReading &r);

// Station pressure (hPa) -> sea-level pressure (hPa), as weather reports show it.
// elevationM = height of the sensor above sea level in metres.
float seaLevelPressure(float hPa, float elevationM);

// The other way round: height of the sensor above sea level in metres, from its station
// pressure and the real sea-level pressure at the place right now (e.g. Open-Meteo's
// pressure_msl). About 1 hPa per 8 m, so the BMP280's +-1 hPa gives roughly +-10 m.
float altitudeFromPressure(float stationHPa, float seaHPa);

#endif
