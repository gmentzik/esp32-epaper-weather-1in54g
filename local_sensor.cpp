#include <DHT.h>
#include "local_sensor.h"

#define DHT22_PIN 32   // ESP32 GPIO32 connected to the DHT22 data pin (21/22 kept free for I2C)

static DHT dht22(DHT22_PIN, DHT22);

bool readLocalSensor(LocalReading &r) {
  dht22.begin();   // initialize the DHT22 sensor (needed after every wake, RAM is lost in deep sleep)

  // Up to 2 tries: right after power-on the DHT22 can fail the first read.
  // It needs at least 2 s between reads, so wait before the retry.
  for (int attempt = 0; attempt < 2; attempt++) {
    if (attempt > 0) delay(2100);
    float hum = dht22.readHumidity(true);    // true = force a new read, not a cached one
    float tempC = dht22.readTemperature();    // reuses the read above (same 2 s window)
    if (!isnan(hum) && !isnan(tempC)) {
      r.temp = tempC;
      r.hum = hum;
      return true;
    }
  }
  return false;
}
