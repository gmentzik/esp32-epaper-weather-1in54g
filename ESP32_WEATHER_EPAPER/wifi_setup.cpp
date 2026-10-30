#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include "wifi_setup.h"

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
                  "</style></head><body><h2>WiFi setup</h2>");
  page += body;
  page += F("</body></html>");
  server.send(200, "text/html", page);
}

static void handleRoot() {
  String ssid, pass;
  getWifiCredentials(ssid, pass);
  String body = "<p>Current network: <b>" + (ssid.length() ? htmlEscape(ssid) : String("none")) + "</b></p>";
  body += F("<form method=POST action=/save>"
            "<label>Network name</label>"
            "<input name=ssid list=nets maxlength=32 required autocomplete=off>"
            "<datalist id=nets>");
  body += netOptions;
  body += F("</datalist>"
            "<label>Password</label><input name=pass type=password maxlength=63>"
            "<button>Save</button></form>"
            "<form method=POST action=/skip><button>Skip (keep current)</button></form>");
  sendPage(body);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  ssid.trim();
  // WiFi limits: name 1-32 chars, password empty (open network) or 8-63 chars
  if (ssid.length() == 0 || ssid.length() > 32 ||
      pass.length() > 63 || (pass.length() > 0 && pass.length() < 8)) {
    sendPage(F("<p>Invalid name or password (password must be 8-63 characters, "
               "or empty for an open network).</p><p><a href=/>Back</a></p>"));
    return;
  }
  Preferences prefs;
  prefs.begin("wifi", false);   // false = read/write
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
  sendPage("<p>Saved. The station will now connect to <b>" + htmlEscape(ssid) +
           "</b>.</p><p>You can close this page.</p>");
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
