// Run: g++ -std=c++17 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Itest/config_shim -Ifirmware -Ilib/gqsafe/src test/config_shim/harness.cpp firmware/config.cpp lib/gqsafe/src/gqconf.cpp lib/gqsafe/src/cfgstore.cpp lib/gqsafe/src/gq_crc32.cpp -o /tmp/cfgh && (cd /tmp && ./cfgh)
// Drives the REAL firmware/config.cpp against the shim: routes, page, JSON, validation, RTC persistence.
#include "Arduino.h"
#include "ESP8266WebServer.h"
#include "safety.h"
#include "webout.h"
#include "wxnet.h"
#include "nightmode.h"
#include "config.h"
#include <assert.h>

EspClass ESP;
static ESP8266WebServer g_srv;
static bool g_upd = false;
static uint8_t g_strikes = 0, g_cmd = 0;
static int g_kicks = 0, g_clears = 0;
static bool g_have = false;
static int g_wx = 2, g_t10 = 73;

namespace safety {
ESP8266WebServer& web() { return g_srv; }
void touch() {}
bool updating() { return g_upd; }
uint8_t wxStrikes() { return g_strikes; }
void wxClearStrikes() { g_strikes = 0; g_clears++; }
void beginChunked(int code, const __FlashStringHelper* type) { g_srv.code = code; g_srv.ctype = (const char*)type; }
void endChunked() {}
void beginPage() { beginChunked(200, F("text/html; charset=utf-8")); g_srv.sendContent_P("<HEAD>"); }
void endPage() { g_srv.sendContent_P("</BODY>"); }
}  // namespace safety
namespace wxnet {
bool reading(int* wx, int* t, int* f) { *wx = g_wx; *t = g_t10; *f = 0; return g_have; }
void kick() { g_kicks++; }
void statusLine(char* b, size_t n) { snprintf(b, n, "Weather: ok, \"quoted\" \\ back"); }
}  // namespace wxnet
namespace nightmode { void command(uint8_t c) { g_cmd = c; } }

#define CHECK(c) do { if (!(c)) { printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void call(const char* route, std::map<std::string, String> args) {
  g_srv.args = args; g_srv.code = 0; g_srv.body.clear(); g_srv.location.clear(); g_srv.ctype.clear();
  auto it = g_srv.routes.find(route);
  CHECK(it != g_srv.routes.end());
  it->second();
}

int main() {
  memset(ESP.rtc, 0, sizeof(ESP.rtc));
  config::begin(false);
  CHECK(config::isDefault());
  CHECK(g_srv.routes.size() == 5);
  call("GET /config", {});
  CHECK(g_srv.code == 200 && g_srv.body.rfind("<HEAD>", 0) == 0 && g_srv.body.find("Clock settings") != std::string::npos && g_srv.body.size() > 3000);
  { FILE* f = fopen("page_out.html", "wb"); fwrite(g_srv.body.data(), 1, g_srv.body.size(), f); fclose(f); }
  call("GET /config.json", {});
  printf("%s\n", g_srv.body.c_str());
  CHECK(g_srv.body.find("\"defaults\":true") != std::string::npos && g_srv.body.find("\"sleep_time\":\"23:00\"") != std::string::npos);

  // invalid: nothing changes, 400
  call("POST /config", {{"f", "1"}, {"wx", "1"}, {"lat", "", }, {"lon", ""}, {"place", ""}, {"st", "23:00"}, {"wt", "07:00"}, {"j", "1"}});
  CHECK(g_srv.code == 400 && g_srv.body.find("needs a latitude") != std::string::npos && config::isDefault());
  call("POST /config", {{"f", "1"}, {"lat", "99"}, {"lon", "1"}, {"j", "1"}});
  CHECK(g_srv.code == 400 && g_srv.body.find("Latitude") != std::string::npos);
  call("POST /config", {{"f", "1"}, {"place", "<script>"}, {"j", "1"}});
  CHECK(g_srv.code == 400 && config::get().place[0] == 0);
  // valid save
  call("POST /config", {{"f", "1"}, {"wx", "1"}, {"lat", "38,72"}, {"lon", "-9.14"}, {"place", "Lisbon, Portugal"}, {"unit", "F"}, {"sl", "1"}, {"st", "23:30"}, {"wt", "06:45"}, {"j", "1"}});
  CHECK(g_srv.code == 200 && g_srv.body == "Saved." && !config::isDefault() && g_kicks == 1);
  CHECK(config::get().lat_c == 3872 && config::get().lon_c == -914 && config::get().seq == 1);
  CHECK((config::get().flags & 7) == 7);
  // identical save: no new seq, no kick
  call("POST /config", {{"f", "1"}, {"wx", "1"}, {"lat", "38.72"}, {"lon", "-9.14"}, {"place", "Lisbon, Portugal"}, {"unit", "F"}, {"sl", "1"}, {"st", "23:30"}, {"wt", "06:45"}});
  CHECK(g_srv.code == 303 && g_srv.location == "/config" && config::get().seq == 1 && g_kicks == 1);
  // survives a "restart": new begin() reads RTC
  config::begin(false);
  CHECK(!config::isDefault() && config::get().lat_c == 3872 && strcmp(config::get().place, "Lisbon, Portugal") == 0);
  g_have = true;
  call("GET /config.json", {});
  printf("%s\n", g_srv.body.c_str());
  CHECK(g_srv.body.find("\"reading\":{\"wx\":2,\"t10\":73}") != std::string::npos && g_srv.body.find("\"unit\":\"F\"") != std::string::npos);
  CHECK(g_srv.body.find("\\\"quoted\\\" \\\\ back") != std::string::npos);
  // unchecked boxes with the form marker mean off
  call("POST /config", {{"f", "1"}, {"lat", "38.72"}, {"lon", "-9.14"}, {"j", "1"}});
  CHECK(g_srv.code == 200 && !(config::get().flags & GQC_F_WEATHER) && !(config::get().flags & GQC_F_SLEEP));
  // safe mode: no routes
  g_srv.routes.clear();
  config::begin(true);
  CHECK(g_srv.routes.empty());
  config::begin(false);
  // night buttons, retry
  call("POST /config/night", {{"cmd", "wake"}, {"j", "1"}}); CHECK(g_cmd == 1 && g_srv.code == 200);
  call("POST /config/night", {{"cmd", "sleep"}}); CHECK(g_cmd == 2 && g_srv.code == 303);
  g_cmd = 0;
  call("POST /config/night", {{"cmd", "x"}}); CHECK(g_cmd == 0 && g_srv.code == 400);
  g_strikes = 2;
  call("POST /config/retry", {{"j", "1"}}); CHECK(g_strikes == 0 && g_clears == 1 && g_srv.code == 200);
  // busy / low heap
  g_upd = true; call("POST /config", {{"f", "1"}}); CHECK(g_srv.code == 503); g_upd = false;
  ESP.heap = 3000; call("GET /config", {}); CHECK(g_srv.code == 503); ESP.heap = 20000;
  // RTC write failure: 500 and nothing changed
  ESP.failRtcWrite = 1;
  unsigned seq = config::get().seq;
  call("POST /config", {{"f", "1"}, {"sl", "1"}, {"j", "1"}}); CHECK(g_srv.code == 500 && config::get().seq == seq && !(config::get().flags & GQC_F_SLEEP));
  ESP.failRtcWrite = 0;
  char b[128]; config::statusLine(b, sizeof(b)); printf("status: %s\n", b);
  puts("harness ok");
  return 0;
}
