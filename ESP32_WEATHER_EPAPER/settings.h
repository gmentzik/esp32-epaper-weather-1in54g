// Station settings chosen in the setup portal: location (name, LAT, LON), refresh period
// and on/off options. Saved in flash (NVS), so they survive deep sleep, resets and power loss.

#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>

// Longest location name. Font16 is 11 px wide, so 10 characters still leave room
// for the date/time in the header.
#define CITY_MAX_LEN 10

// Time between screen updates, in minutes. The portal accepts MIN..MAX; DEFAULT is used
// until it saves a value. A full refresh takes ~15-20 s and Waveshare recommends >= 3 min.
#define REFRESH_DEFAULT_MINUTES 15
#define REFRESH_MIN_MINUTES     3
#define REFRESH_MAX_MINUTES     1440   // one day

// On/off options share one byte ("flags"), one bit each, to save space.
#define FLAG_SHOW_IMAGE (1 << 0)   // bit 0: picture view in the bottom area (default off)

struct StationSettings {
  char city[CITY_MAX_LEN + 1];   // header text, ASCII
  float lat, lon;
  uint16_t refreshMin;           // minutes between screen updates (REFRESH_MIN..MAX_MINUTES)
  uint8_t flags;                 // FLAG_... bits
};

// Quick-fill buttons on the setup page. The first one is also the default location.
struct LocationPreset {
  const char *name;
  float lat, lon;
};
extern const LocationPreset LOCATION_PRESETS[];
extern const uint8_t LOCATION_PRESET_COUNT;

// Saved settings; anything not saved yet gets its default (first preset,
// REFRESH_DEFAULT_MINUTES, all flags off).
void loadSettings(StationSettings &s);
void saveSettings(const StationSettings &s);

// Makes a name safe for the header: trims spaces at both ends, replaces characters the
// font doesn't have (non-ASCII) with '?', and cuts it to CITY_MAX_LEN.
// out must have room for CITY_MAX_LEN + 1 chars.
void cleanCityName(const char *in, char *out);

#endif
