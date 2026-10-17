// Indoor (local) temperature and humidity, read from a DHT22 on GPIO32.
// Needs the "DHT sensor library" by Adafruit (Library Manager).

#ifndef LOCAL_SENSOR_H
#define LOCAL_SENSOR_H

struct LocalReading {
  float temp;   // degrees C
  float hum;    // % relative humidity
};

// Fills r and returns true on success, false if the sensor could not be read.
bool readLocalSensor(LocalReading &r);

#endif
