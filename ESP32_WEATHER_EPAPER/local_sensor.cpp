#include <DHT.h>
#include <Wire.h>
#include <Adafruit_BMP280.h>
#include "local_sensor.h"

#define DHT22_PIN 32   // ESP32 GPIO32 connected to the DHT22 data pin
#define BMP_SDA   21   // BMP280 SDA (default ESP32 I2C pins)
#define BMP_SCL   22   // BMP280 SCL

static DHT dht22(DHT22_PIN, DHT22);
static Adafruit_BMP280 bmp;   // uses Wire

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

// Finds the sensor: module boards use address 0x76 or 0x77 (set by the SDO pin).
// Chip ID 0x58 = BMP280; 0x60 = BME280, often sold as "BMP280" and read the same way
// for pressure. Returns false if nothing answers.
static bool beginBmp() {
  static const uint8_t ADDRS[2] = {0x76, 0x77};
  static const uint8_t IDS[2] = {0x58, 0x60};
  for (uint8_t a : ADDRS) {
    for (uint8_t id : IDS) {
      if (bmp.begin(a, id)) return true;
    }
  }
  return false;
}

bool readPressureSensor(LocalReading &r) {
  r.pressure = NAN;
  Wire.begin(BMP_SDA, BMP_SCL);
  if (!beginBmp()) return false;

  // Forced mode: one measurement on request, then the chip goes back to sleep by itself,
  // so it draws almost nothing while the ESP32 deep-sleeps.
  bmp.setSampling(Adafruit_BMP280::MODE_FORCED,
                  Adafruit_BMP280::SAMPLING_X1,    // temperature (only used to correct pressure)
                  Adafruit_BMP280::SAMPLING_X4,    // pressure: 4x oversampling, less noise
                  Adafruit_BMP280::FILTER_OFF);
  if (!bmp.takeForcedMeasurement()) return false;

  float hPa = bmp.readPressure() / 100.0f;   // library returns Pa
  if (isnan(hPa) || hPa < 300 || hPa > 1100) return false;   // outside the sensor's range = bad read
  r.pressure = hPa;
  return true;
}

float seaLevelPressure(float hPa, float elevationM) {
  // Barometric formula (standard atmosphere), the same one the Adafruit library uses.
  return hPa / powf(1.0f - elevationM / 44330.0f, 5.255f);
}
