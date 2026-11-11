#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "wifi_setup.h"
#include "settings.h"

// secrets.h is optional now: its values are only the default until the portal saves new ones.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  static const char *WIFI_SSID = "";
  static const char *WIFI_PASS = "";
#endif

static WebServer server(80);
static DNSServer dns;            // answers every name with our IP, so the phone opens the page by itself
static String netOptions;        // <option> list of nearby networks, scanned once at start
static bool finished = false;    // set by Save or Skip to close the portal early
static bool saved = false;

// Network names can contain < > " &, which would break the HTML.
static String htmlEscape(const String &s) {
  String out;
  for (char c : s) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else out += c;
  }
  return out;
}

static void sendPage(const String &body) {
  String page = F("<!doctype html><html><head><meta charset=utf-8>"
                  "<meta name=viewport content='width=device-width,initial-scale=1'>"
                  "<title>Weather station setup</title><style>"
                  "body{font-family:sans-serif;max-width:22em;margin:2em auto;padding:0 1em}"
                  "input,button{width:100%;padding:.6em;margin:.3em 0 1em;box-sizing:border-box;font-size:1em}"
                  "input[type=checkbox]{width:auto;margin-right:.5em}"
                  "h3{margin:1.5em 0 .5em;border-bottom:1px solid #ccc}"
                  ".hint{font-size:.85em;color:#555}.hint a{word-break:break-all}"
                  "</style></head><body><h2>Weather station setup</h2>");
  page += body;
  page += F("</body></html>");
  server.send(200, "text/html", page);
}

static void sendError(const char *msg) {
  sendPage(String("<p>") + msg + "</p><p><a href=/>Back</a></p>");
}

// Latitude / longitude as typed on the form: 1-10 chars, a number between -limit and limit.
// A comma is accepted as the decimal point, as some phone keyboards only offer that.
static bool parseCoord(String s, float limit, float &out) {
  s.trim();
  s.replace(',', '.');
  if (s.length() == 0 || s.length() > 10) return false;
  char *end;
  float v = strtof(s.c_str(), &end);
  if (*end != 0 || v < -limit || v > limit) return false;   // junk after the number, or out of range
  out = v;
  return true;
}

// Refresh period as typed on the form: a whole number of minutes,
// REFRESH_MIN_MINUTES..REFRESH_MAX_MINUTES.
static bool parseMinutes(String s, uint16_t &out) {
  s.trim();
  if (s.length() == 0 || s.length() > 4) return false;
  char *end;
  unsigned long v = strtoul(s.c_str(), &end, 10);
  if (*end != 0 || !isdigit((unsigned char)s[0])) return false;   // junk, or a sign like "-5"
  if (v < REFRESH_MIN_MINUTES || v > REFRESH_MAX_MINUTES) return false;
  out = (uint16_t)v;
  return true;
}

static void handleRoot() {
  String ssid, pass;
  getWifiCredentials(ssid, pass);
  StationSettings s;
  loadSettings(s);
  char buf[16];

  // One form for everything, so a single Save stores it all.
  String body = F("<form method=POST action=/save><h3>WiFi</h3>");
  body += "<p>Current network: <b>" + (ssid.length() ? htmlEscape(ssid) : String("none")) + "</b></p>";
  body += F("<label>Network name (leave empty to keep the current one)</label>"
            "<input name=ssid list=nets maxlength=32 autocomplete=off>"
            "<datalist id=nets>");
  body += netOptions;
  body += F("</datalist>"
            "<label>Password</label><input name=pass type=password maxlength=63>"

            // Location. maxlength / pattern let the phone refuse bad input before sending;
            // handleSave() checks the same again.
            "<h3>Location</h3>"
            "<label>Name (shown in the header, up to 10 characters)</label>"
            "<input name=city id=city maxlength=10 required autocomplete=off "
            "pattern='[ -~]+' title='Up to 10 plain characters (A-Z, 0-9, spaces)' value=\"");
  body += htmlEscape(s.city);
  body += F("\"><label>Latitude (-90 to 90, north is positive)</label>"
            "<input name=lat id=lat maxlength=10 required autocomplete=off "
            "pattern='-?[0-9]{1,3}([.,][0-9]+)?' title='A number such as 37.98' value=\"");
  snprintf(buf, sizeof(buf), "%.2f", s.lat);
  body += buf;
  body += F("\"><label>Longitude (-180 to 180, east is positive)</label>"
            "<input name=lon id=lon maxlength=10 required autocomplete=off "
            "pattern='-?[0-9]{1,3}([.,][0-9]+)?' title='A number such as 23.73' value=\"");
  snprintf(buf, sizeof(buf), "%.2f", s.lon);
  body += buf;
  body += F("\">");

  // Quick-fill buttons (type=button, so they only fill the fields and don't submit).
  for (uint8_t i = 0; i < LOCATION_PRESET_COUNT; i++) {
    const LocationPreset &p = LOCATION_PRESETS[i];
    char call[64];
    snprintf(call, sizeof(call), "fill('%s','%.2f','%.2f')", p.name, p.lat, p.lon);
    body += "<button type=button onclick=\"" + htmlEscape(call) + "\">Fill in " + htmlEscape(p.name) + "</button>";
  }

  body += F("<p class=hint>To find the latitude and longitude of a place, open this link on a "
            "device with internet (this setup network has none), with your place as "
            "<b>name</b>:<br>"
            "<a href='https://geocoding-api.open-meteo.com/v1/search?name=Athens&amp;count=5&amp;language=en&amp;format=json&amp;countryCode=GR'>"
            "https://geocoding-api.open-meteo.com/v1/search?name=Athens&amp;count=5&amp;language=en&amp;format=json&amp;countryCode=GR</a><br>"
            "Copy <b>latitude</b> and <b>longitude</b> of the right result (check <b>country</b> "
            "and <b>admin1</b> if several places share the name). "
            "<b>&amp;countryCode=GR</b> is optional: it keeps only places in that country "
            "(2-letter code, e.g. IT, DE); remove it to search everywhere. "
            "Two decimals are enough. South and west are negative.</p>");

  // Refresh period. min/max come from settings.h, the same limits handleSave() checks.
  snprintf(buf, sizeof(buf), "%d to %d", REFRESH_MIN_MINUTES, REFRESH_MAX_MINUTES);
  body += F("<h3>Updates</h3><label>Refresh every (minutes, ");
  body += buf;
  body += F(")</label><input name=refresh type=number step=1 required min=");
  body += REFRESH_MIN_MINUTES;
  body += F(" max=");
  body += REFRESH_MAX_MINUTES;
  body += F(" value=");
  body += s.refreshMin;
  body += F(">"
            "<p class=hint>A full refresh takes about 15-20 s. Waveshare recommends at least "
            "3 minutes between refreshes. Longer = longer battery life.</p>"

            "<h3>Bottom area</h3>"
            "<label><input type=checkbox name=img value=1");
  if (s.flags & FLAG_SHOW_IMAGE) body += F(" checked");
  body += F(">Show the picture view</label>"

            "<button>Save</button></form>"
            "<form method=POST action=/skip><button>Skip (keep current)</button></form>"
            "<script>function fill(n,a,o){"
            "document.getElementById('city').value=n;"
            "document.getElementById('lat').value=a;"
            "document.getElementById('lon').value=o}</script>");
  sendPage(body);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  String city = server.arg("city");
  ssid.trim();
  city.trim();

  // Check everything first, so nothing is saved when one field is wrong.
  // WiFi: an empty name keeps the current network. Otherwise: name 1-32 chars,
  // password empty (open network) or 8-63 chars.
  bool newWifi = ssid.length() > 0;
  if (newWifi && (ssid.length() > 32 || pass.length() > 63 ||
                  (pass.length() > 0 && pass.length() < 8))) {
    sendError("Invalid WiFi name or password (password must be 8-63 characters, "
              "or empty for an open network).");
    return;
  }
  // Location name: 1-CITY_MAX_LEN plain ASCII characters, as the header font has no others.
  bool cityOk = city.length() > 0 && city.length() <= CITY_MAX_LEN;
  for (size_t i = 0; cityOk && i < city.length(); i++) {
    if (city[i] < ' ' || city[i] > '~') cityOk = false;
  }
  if (!cityOk) {
    sendError("Invalid location name: use 1-10 plain characters (A-Z, 0-9, spaces).");
    return;
  }
  float lat, lon;
  if (!parseCoord(server.arg("lat"), 90, lat) || !parseCoord(server.arg("lon"), 180, lon)) {
    sendError("Invalid latitude or longitude (latitude -90 to 90, longitude -180 to 180, "
              "e.g. 37.98 and 23.73).");
    return;
  }
  uint16_t refresh;
  if (!parseMinutes(server.arg("refresh"), refresh)) {
    char msg[96];
    snprintf(msg, sizeof(msg), "Invalid refresh time: use a whole number of minutes from %d to %d.",
             REFRESH_MIN_MINUTES, REFRESH_MAX_MINUTES);
    sendError(msg);
    return;
  }

  if (newWifi) {
    Preferences prefs;
    prefs.begin("wifi", false);   // false = read/write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();
  }
  StationSettings s;
  loadSettings(s);   // start from the saved values, so flag bits not on this page stay as they are
  strlcpy(s.city, city.c_str(), sizeof(s.city));
  s.lat = lat;
  s.lon = lon;
  s.refreshMin = refresh;
  if (server.hasArg("img")) s.flags |= FLAG_SHOW_IMAGE;    // a checkbox is only sent when ticked
  else                      s.flags &= ~FLAG_SHOW_IMAGE;
  saveSettings(s);

  String msg = "<p>Saved. Location: <b>" + htmlEscape(s.city) + "</b>, refresh every <b>" +
               String(s.refreshMin) + "</b> min.</p>";
  if (newWifi) msg += "<p>The station will now connect to <b>" + htmlEscape(ssid) + "</b>.</p>";
  msg += F("<p>You can close this page.</p>");
  sendPage(msg);
  saved = finished = true;
}

static void handleSkip() {
  sendPage(F("<p>Setup closed, keeping the current network.</p><p>You can close this page.</p>"));
  finished = true;
}

// Any other address (including the phone's "is there internet?" checks) goes to the setup page.
static void handleRedirect() {
  server.sendHeader("Location", "http://" SETUP_AP_IP "/", true);
  server.send(302, "text/plain", "");
}

bool runWifiSetupPortal(uint32_t seconds) {
  finished = saved = false;

  // Scan first, so the page can suggest nearby networks (takes ~2 s).
  WiFi.mode(WIFI_AP_STA);
  int n = WiFi.scanNetworks();
  netOptions = "";
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i).length()) netOptions += "<option value=\"" + htmlEscape(WiFi.SSID(i)) + "\">";
  }
  WiFi.scanDelete();

  WiFi.softAP(SETUP_AP_NAME, SETUP_AP_PASS);
  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/skip", HTTP_POST, handleSkip);
  server.onNotFound(handleRedirect);
  server.begin();

  uint32_t t0 = millis();
  while (!finished && millis() - t0 < seconds * 1000UL) {
    dns.processNextRequest();
    server.handleClient();
    delay(2);
  }
  // Give the phone a moment to receive the "Saved" / "Closed" page before the network disappears.
  if (finished) {
    uint32_t t1 = millis();
    while (millis() - t1 < 1000) { server.handleClient(); delay(2); }
  }

  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  return saved;
}

void getWifiCredentials(String &ssid, String &pass) {
  Preferences prefs;
  prefs.begin("wifi", true);   // true = read-only
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
  // Nothing saved by the portal yet: use secrets.h (empty if that file doesn't exist)
  if (ssid.length() == 0) {
    ssid = WIFI_SSID;
    pass = WIFI_PASS;
  }
}
