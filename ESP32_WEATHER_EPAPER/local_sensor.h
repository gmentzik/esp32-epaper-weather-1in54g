// Indoor (local) sensors:
//  - temperature and humidity from a DHT22 on GPIO32
//  - air pressure from a BMP280 on I2C (SDA GPIO21, SCL GPIO22)
// Needs the "DHT sensor library" and the "Adafruit BMP280 Library" by Adafruit (Library Manager).

#ifndef LOCAL_SENSOR_H
#define LOCAL_SENSOR_H

struct LocalReading {
  float temp;       // degrees C (DHT22)
  float hum;        // % relative humidity (DHT22)
  float pressure;   // hPa (BMP280), NAN = not read
};

// DHT22: fills r.temp / r.hum and returns true on success, false if it could not be read.
bool readLocalSensor(LocalReading &r);

// BMP280: fills r.pressure (hPa at the sensor, "station pressure") and returns true on
// success. On failure r.pressure is set to NAN.
bool readPressureSensor(LocalReading &r);

// Station pressure (hPa) -> sea-level pressure (hPa), as weather reports show it.
// elevationM = height of the sensor above sea level in metres.
float seaLevelPressure(float hPa, float elevationM);

#endif
