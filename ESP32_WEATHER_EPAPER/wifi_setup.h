// WiFi setup portal: the ESP32 opens its own WiFi network with a small web page
// where the user enters the home WiFi name and password. They are saved in flash (NVS),
// so they survive deep sleep, resets and power loss.

#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include <Arduino.h>

#define SETUP_AP_NAME "Weather-Setup"   // network the phone joins
#define SETUP_AP_PASS "44444444"        // WiFi rule: at least 8 characters, or the AP won't start
#define SETUP_AP_IP   "192.168.4.1"     // ESP32 default access point address

// Runs the setup portal for up to `seconds`. Closes early when the user saves new
// credentials (returns true) or presses "Skip" (returns false).
bool runWifiSetupPortal(uint32_t seconds);

// Credentials saved by the portal. Falls back to secrets.h when nothing is saved yet.
void getWifiCredentials(String &ssid, String &pass);

#endif
