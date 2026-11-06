#include <Preferences.h>
#include "settings.h"

// Quick-fill buttons on the setup page, in this order. The FIRST entry is also the
// location used until the portal saves another one.
// To add a button, add a line: name in plain ASCII (no quotes), at most CITY_MAX_LEN chars.
const LocationPreset LOCATION_PRESETS[] = {
  {"ATHENS", 37.98f, 23.73f},
};
const uint8_t LOCATION_PRESET_COUNT = sizeof(LOCATION_PRESETS) / sizeof(LOCATION_PRESETS[0]);

static const char *NVS_NAMESPACE = "station";   // separate from the "wifi" namespace

void loadSettings(StationSettings &s) {
  const LocationPreset &def = LOCATION_PRESETS[0];
  Preferences prefs;
  // Read-only. Before the first save the namespace doesn't exist yet; the getters then
  // return the defaults given here (the core may log an "nvs_open failed" line, harmless).
  prefs.begin(NVS_NAMESPACE, true);
  String city = prefs.getString("city", def.name);
  s.lat = prefs.getFloat("lat", def.lat);
  s.lon = prefs.getFloat("lon", def.lon);
  s.flags = prefs.getUChar("flags", 0);   // all options off by default
  prefs.end();
  strlcpy(s.city, city.c_str(), sizeof(s.city));   // never more than CITY_MAX_LEN
}

void saveSettings(const StationSettings &s) {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, false);   // false = read/write
  prefs.putString("city", s.city);
  prefs.putFloat("lat", s.lat);
  prefs.putFloat("lon", s.lon);
  prefs.putUChar("flags", s.flags);
  prefs.end();
}

void cleanCityName(const char *in, char *out) {
  while (*in && (unsigned char)*in <= ' ') in++;   // trim the start
  int n = 0;
  while (*in && n < CITY_MAX_LEN) {
    char c = *in++;
    out[n++] = (c >= ' ' && c <= '~') ? c : '?';   // the fonts only have ASCII 32..126
  }
  while (n > 0 && out[n - 1] == ' ') n--;          // trim the end
  out[n] = 0;
}
