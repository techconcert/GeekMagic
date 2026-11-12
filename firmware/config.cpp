// R3 settings page and record (docs/DESIGN-R3.md 5, R3 addendum). The record lives in RTC user memory blocks 32..47 only:
// there is NO flash backend, no restore from the phone. Storage sits behind CfgBackend (lib/gqsafe/src/cfgstore.h) so a
// flash backend can be added later by another owner decision.
//   GET  /config         the page (static, streamed from flash; config_page.h)
//   GET  /config.json    current values and state
//   POST /config         save (whole form validated first; an invalid value changes nothing)
//   POST /config/night   cmd=wake | sleep  -> nightmode::command
//   POST /config/retry   clear weather strikes and fetch again
// No route is registered in safe mode. All handlers: touch() first, no String of a page, one 480-byte Out buffer.
#include "config.h"
#include "webout.h"
#include "safety.h"
#include "wxnet.h"
#include "nightmode.h"
#include "cfgstore.h"
#include "store.h"
#include "prefs.h"
#include "config_page_gz.h"

namespace config {

static const uint32_t RTC_BLOCK = 32;            // blocks 32..47 (64 bytes); see the RTC map in DESIGN-R3.md 5.3
static const uint32_t LOW_HEAP_CONFIG = 7000;    // below this the handlers answer 503 instead of building anything
static const uint8_t NM_WAKE_NOW = 1;            // nightmode::command values: ASSUMED, S1 owns them (see hand-off note)
static const uint8_t NM_SLEEP_NOW = 2;

static GqConf s_conf;                            // 64 bytes of .bss
static bool s_default = true;                    // nothing valid was loaded from RTC and nothing was saved yet

// ---- the RTC backend (the only one in R3)
static int rtcRead(void*, uint8_t* buf, uint32_t n) {
  uint32_t tmp[16];
  if (n != sizeof(tmp)) return 0;
  if (!ESP.rtcUserMemoryRead(RTC_BLOCK, tmp, sizeof(tmp))) return 0;
  memcpy(buf, tmp, n);
  return 1;
}
static int rtcWrite(void*, const uint8_t* buf, uint32_t n) {
  uint32_t tmp[16];
  if (n != sizeof(tmp)) return 0;
  memcpy(tmp, buf, n);
  return ESP.rtcUserMemoryWrite(RTC_BLOCK, tmp, sizeof(tmp)) ? 1 : 0;
}
static const CfgBackend s_rtc = { "rtc", rtcRead, rtcWrite, nullptr };

// The record saved in flash (store.cpp), read once at boot after a power cut.
static int memRead(void* ctx, uint8_t* buf, uint32_t n) {
  if (n != sizeof(GqConf) || !ctx) return 0;
  memcpy(buf, ctx, n);
  return 1;
}

// ---- messages, one per GQC_E_* code (flash)
static const char M_OK[] PROGMEM = "Saved.";
static const char M_LAT[] PROGMEM = "Latitude must be a number from -90 to 90, for example 38.72. Nothing was changed.";
static const char M_LON[] PROGMEM = "Longitude must be a number from -180 to 180, for example -9.14. Nothing was changed.";
static const char M_PAIR[] PROGMEM = "Give both latitude and longitude, or leave both empty. Nothing was changed.";
static const char M_PLACE[] PROGMEM = "Place name: at most 23 characters from A-Z a-z 0-9 space , . ' - only. Nothing was changed.";
static const char M_SLEEP[] PROGMEM = "Sleep time must look like 23:00. Nothing was changed.";
static const char M_WAKE[] PROGMEM = "Wake time must look like 07:00. Nothing was changed.";
static const char M_SAME[] PROGMEM = "Sleep time and wake time must be different. Nothing was changed.";
static const char M_NOCOORD[] PROGMEM = "Live weather needs a latitude and longitude: search for a place or type them in. Nothing was changed.";
static const char M_LOWMEM[] PROGMEM = "The clock is low on memory. Try again in a minute.";
static const char M_BUSY[] PROGMEM = "A firmware update is running. Try again afterwards.";
static const char M_STORE[] PROGMEM = "The settings could not be stored in the clock's memory.";
static const char M_BL[] PROGMEM = "Brightness must be a whole number from 0 to 100. Nothing was changed.";
static const char M_DEPTH[] PROGMEM = "Colour depth must be a whole number from 0 to 100. Nothing was changed.";
static const char M_TZ[] PROGMEM = "Time zone rule: 3 to 34 characters from A-Z a-z 0-9 < > + - : , . / only. Nothing was changed.";
static const char M_RESET[] PROGMEM = "These settings are back to defaults.";
static const char M_CMD[] PROGMEM = "Unknown command.";
static const char M_WAKE_SENT[] PROGMEM = "Wake now sent.";
static const char M_SLEEP_SENT[] PROGMEM = "Sleep now sent.";
static const char M_RETRY[] PROGMEM = "Trying the weather again in a moment.";

static PGM_P errorText(int code) {
  switch (code) {
    case GQC_OK: return M_OK;
    case GQC_E_LAT: return M_LAT;
    case GQC_E_LON: return M_LON;
    case GQC_E_COORD_PAIR: return M_PAIR;
    case GQC_E_PLACE: return M_PLACE;
    case GQC_E_SLEEP_TIME: return M_SLEEP;
    case GQC_E_WAKE_TIME: return M_WAKE;
    case GQC_E_SAME_TIME: return M_SAME;
    case GQC_E_NO_COORDS: return M_NOCOORD;
    default: return M_STORE;
  }
}

static ESP8266WebServer& srv() { return safety::web(); }

static void reply(int code, PGM_P text) { srv().send_P(code, PSTR("text/plain"), text); }

// A handler may run only when memory is fine and no upload is running. Returns false after having answered.
static bool admit() {
  safety::touch();
  if (safety::updating()) { reply(503, M_BUSY); return false; }
  if (ESP.getFreeHeap() < LOW_HEAP_CONFIG) { reply(503, M_LOWMEM); return false; }
  return true;
}

static void redirectConfig() {
  srv().sendHeader(F("Location"), F("/config"));
  srv().send(303);
}

// ---- GET /config: the static page, 400 bytes at a time through the Out buffer
static void handlePage() {
  if (!admit()) return;
  srv().sendHeader(F("Content-Encoding"), F("gzip"));   // the page is stored gzipped (tools/gen_config_gz.py), every browser inflates it
  safety::beginChunked(200, F("text/html; charset=utf-8"));
  webout::Out o(srv());
  size_t left = CONFIG_PAGE_GZ_LEN;
  const unsigned char* p = CONFIG_PAGE_GZ;
  while (left) {                                 // bounded: at most left/400 + 1 passes
    size_t n = left > 400 ? 400 : left;
    o.flush();
    memcpy_P(o.b, p, n);
    o.n = (uint16_t)n;
    p += n;
    left -= n;
  }
  o.flush();
  safety::endChunked();
}

// ---- GET /config.json
static void putJson(webout::Out& o, const char* t) {   // JSON string body: quote, backslash and control characters escaped
  for (; *t; t++) {
    unsigned char c = (unsigned char)*t;
    if (c == '"' || c == '\\') { char e[2] = { '\\', (char)c }; o.put(e, 2); }
    else if (c >= 32 && c < 127) o.put(t, 1);
    else o.put(" ", 1);
  }
}

static void handleJson() {
  if (!admit()) return;
  srv().sendHeader(F("Cache-Control"), F("no-store"));
  safety::beginChunked(200, F("application/json"));
  webout::Out o(srv());
  const GqConf& c = s_conf;
  char lat[8], lon[8], st[6], wt[6], place[sizeof(c.place)];
  gqc_format_coord(c.lat_c, lat);
  gqc_format_coord(c.lon_c, lon);
  gqc_format_time(c.sleep_min, st);
  gqc_format_time(c.wake_min, wt);
  memcpy(place, c.place, sizeof(place));
  place[sizeof(place) - 1] = 0;
  o.f(PSTR("{\"weather\":%u,\"lat\":\"%s\",\"lon\":\"%s\",\"place\":\""), (c.flags & GQC_F_WEATHER) ? 1u : 0u, lat, lon);
  putJson(o, place);
  o.f(PSTR("\",\"unit\":\"%c\",\"sleep\":%u,\"sleep_time\":\"%s\",\"wake_time\":\"%s\",\"seq\":%u,\"defaults\":%s,\"storage\":\"%s\""),
      (c.flags & GQC_F_FAHRENHEIT) ? 'F' : 'C', (c.flags & GQC_F_SLEEP) ? 1u : 0u, st, wt, (unsigned)c.seq, s_default ? "true" : "false", store::active() ? "flash" : s_rtc.name);
  int wx = 0, t10 = 0, fl = 0;
  const bool have = (c.flags & GQC_F_WEATHER) && wxnet::reading(&wx, &t10, &fl);
  const unsigned strikes = safety::wxStrikes();
  if (have) o.f(PSTR(",\"reading\":{\"wx\":%d,\"t10\":%d}"), wx, t10);
  else o.p(PSTR(",\"reading\":null"));
  o.f(PSTR(",\"strikes\":%u,\"retry\":%s,\"status\":\""), strikes, ((c.flags & GQC_F_WEATHER) && (strikes > 0 || !have)) ? "true" : "false");
  char status[96];
  status[0] = 0;
  wxnet::statusLine(status, sizeof(status));
  status[sizeof(status) - 1] = 0;
  putJson(o, status);
  o.f(PSTR("\",\"bl\":%u,\"depth\":%u,\"mt\":%u,\"tz\":\""), (unsigned)prefs::settings().brightness, (unsigned)prefs::settings().depth, (prefs::settings().flags & GQS_FLAG_MAINT) ? 1u : 0u);
  putJson(o, prefs::settings().tz);
  o.p(PSTR("\"}"));
  o.flush();
  safety::endChunked();
}

// whole number 0..100, nothing else
static bool pct(const String& v, uint8_t* out) {
  if (v.length() < 1 || v.length() > 3) return false;
  unsigned n = 0;
  for (unsigned i = 0; i < v.length(); i++) { if (v[i] < '0' || v[i] > '9') return false; n = n * 10 + (unsigned)(v[i] - '0'); }
  if (n > 100) return false;
  *out = (uint8_t)n;
  return true;
}

// ---- POST /config
static void handleSave() {
  if (!admit()) return;
  ESP8266WebServer& s = srv();
  GqSettings ns = prefs::settings();             // screen part: validated first, applied only after the conf is accepted
  bool screen = false;
  if (s.hasArg("bl")) { if (!pct(s.arg("bl"), &ns.brightness)) { reply(400, M_BL); return; } screen = true; }
  if (s.hasArg("depth")) { if (!pct(s.arg("depth"), &ns.depth)) { reply(400, M_DEPTH); return; } screen = true; }
  if (s.hasArg("tz")) {
    const String& t = s.arg("tz");
    if (t.length() > GQS_TZ_MAX) { reply(400, M_TZ); return; }
    char tmp[sizeof(ns.tz)];
    memset(tmp, 0, sizeof(tmp));
    memcpy(tmp, t.c_str(), t.length());
    if (!gqs_tz_valid(tmp)) { reply(400, M_TZ); return; }
    memcpy(ns.tz, tmp, sizeof(ns.tz));
    screen = true;
  }
  if (s.hasArg("f")) { ns.flags = (uint8_t)((ns.flags & ~GQS_FLAG_MAINT) | (s.hasArg("mt") ? GQS_FLAG_MAINT : 0)); screen = true; }
  const bool wholeForm = s.hasArg("f");          // the hidden marker: unchecked boxes then mean "off", not "keep"
  GqcInput in;
  in.weather = wholeForm ? (s.hasArg("wx") ? 1 : 0) : -1;
  in.sleep_on = wholeForm ? (s.hasArg("sl") ? 1 : 0) : -1;
  in.fahrenheit = -1;
  if (s.hasArg("unit")) { const String& u = s.arg("unit"); in.fahrenheit = (u.length() == 1 && u[0] == 'F') ? 1 : 0; }
  in.lat = s.hasArg("lat") ? s.arg("lat").c_str() : nullptr;
  in.lon = s.hasArg("lon") ? s.arg("lon").c_str() : nullptr;
  in.place = s.hasArg("place") ? s.arg("place").c_str() : nullptr;
  in.sleep_time = s.hasArg("st") ? s.arg("st").c_str() : nullptr;
  in.wake_time = s.hasArg("wt") ? s.arg("wt").c_str() : nullptr;

  GqConf n;
  const int err = gqc_validate(&s_conf, &in, &n);
  if (err != GQC_OK) { reply(400, errorText(err)); return; }     // nothing was changed
  if (!gqc_same(&n, &s_conf)) {
    n.seq = (uint16_t)(s_conf.seq + 1);
    if (!cfg_save(&s_rtc, &n)) { reply(500, M_STORE); return; }
    const GqConf old = s_conf;
    s_conf = n;
    s_default = false;
    if ((n.flags & GQC_F_WEATHER) && (!(old.flags & GQC_F_WEATHER) || old.lat_c != n.lat_c || old.lon_c != n.lon_c)) wxnet::kick();
  }
  if (screen && !prefs::set(ns)) { reply(400, M_TZ); return; }
  if (s.hasArg("j")) reply(200, M_OK); else redirectConfig();
}

// ---- POST /config/reset: this page's settings only (weather, sleep, screen); the game settings keep their values
static void handleReset() {
  if (!admit()) return;
  GqConf n;
  gqc_defaults(&n);
  n.seq = (uint16_t)(s_conf.seq + 1);
  if (!cfg_save(&s_rtc, &n)) { reply(500, M_STORE); return; }
  s_conf = n;
  s_default = false;
  prefs::resetScreen();
  reply(200, M_RESET);
}

// ---- POST /config/night, POST /config/retry
static void handleNight() {
  if (!admit()) return;
  ESP8266WebServer& s = srv();
  const String& cmd = s.arg("cmd");
  if (cmd == "wake") { nightmode::command(NM_WAKE_NOW); if (s.hasArg("j")) reply(200, M_WAKE_SENT); else redirectConfig(); }
  else if (cmd == "sleep") { nightmode::command(NM_SLEEP_NOW); if (s.hasArg("j")) reply(200, M_SLEEP_SENT); else redirectConfig(); }
  else reply(400, M_CMD);
}

static void handleRetry() {
  if (!admit()) return;
  safety::wxClearStrikes();
  wxnet::kick();
  if (srv().hasArg("j")) reply(200, M_RETRY); else redirectConfig();
}

// ---- public
void begin(bool safeMode) {
  if (cfg_load(&s_rtc, &s_conf)) s_default = false;
  else {
    const GqConf* sv = store::savedConf();
    const CfgBackend fb = { "flash", memRead, nullptr, (void*)sv };
    if (sv && cfg_load(&fb, &s_conf)) { s_default = false; cfg_save(&s_rtc, &s_conf); }   // power cut: back into RTC too
    else s_default = true;
  }
  if (safeMode) return;                          // safe mode registers no route
  ESP8266WebServer& s = srv();
  s.on("/config", HTTP_GET, handlePage);
  s.on("/config", HTTP_POST, handleSave);
  s.on("/config.json", HTTP_GET, handleJson);
  s.on("/config/reset", HTTP_POST, handleReset);
  s.on("/config/night", HTTP_POST, handleNight);
  s.on("/config/retry", HTTP_POST, handleRetry);
}

void loop() {}
const GqConf& get() { return s_conf; }
bool isDefault() { return s_default; }

void statusLine(char* buf, size_t n) {
  if (!n) return;
  buf[0] = 0;
  if (s_default) return;
  snprintf_P(buf, n, PSTR("Clock settings saved (change %u)."), (unsigned)s_conf.seq);
}

}  // namespace config
