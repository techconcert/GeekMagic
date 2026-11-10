// See safety.h. Keep this file boring and robust.
#include "safety.h"
#include "version.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <Updater.h>
#include <WiFiUdp.h>
#include <time.h>
#include <user_interface.h>

#include "st7789.h"

#include <coredecls.h>   // settimeofday_cb

namespace safety {

// ------------------------------------------------------------ constants
static const char AP_PASS[] = "GEEKQUEST";        // WPA2, 8+ chars; uppercase so it reads the same on the clock font
static const uint8_t CRASHES_FOR_SAFE_MODE = 3;
static const uint8_t CRASHES_FOR_NO_DISPLAY = 6;
static const uint32_t STABLE_AFTER_MS = 60UL * 1000;
static const uint32_t AP_GRACE_MS = 10UL * 60 * 1000;  // AP stays up 10 min after boot
static const uint32_t STA_LOST_AP_MS = 60UL * 1000;    // AP returns after 60 s without WiFi

// RTC user memory. Offset 0 is used by eboot to pass the OTA copy command,
// so we stay well clear of it (offset is in 4-byte blocks).
static const uint32_t RTC_OFFSET = 96;
static const uint32_t RTC_MAGIC = 0x47514247;  // "GQBG"
struct RtcState {
  uint32_t magic;
  uint32_t crashes;
  uint32_t planned;     // 1 = the last restart was ours (OTA, reboot button)
  uint32_t forceSafe;   // 1 = user asked for safe mode on next boot
};

// ------------------------------------------------------------ state
static ESP8266WebServer server(80);
static DNSServer dns;
static bool s_safe = false, s_noDisplay = false;
static bool s_updating = false;
static bool s_upFailed = false;
static String s_upError;
static uint32_t s_upBytes = 0;
static bool s_stableMarked = false;
static bool s_ap = false;
static uint32_t s_staLostSince = 0;
static char s_apName[24];
static uint32_t s_crashes = 0;
static bool s_timeSynced = false;
static String s_tz = "<-03>3";   // default UTC-3; change on the status page (not saved yet)

// ------------------------------------------------------------ boot guard
static void rtcRead(RtcState& s) { ESP.rtcUserMemoryRead(RTC_OFFSET, (uint32_t*)&s, sizeof(s)); }
static void rtcWrite(RtcState& s) { ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&s, sizeof(s)); }

static void bootGuard() {
  RtcState s;
  rtcRead(s);
  const rst_info* ri = ESP.getResetInfoPtr();
  uint32_t reason = ri ? ri->reason : REASON_DEFAULT_RST;
  bool coldBoot = (s.magic != RTC_MAGIC) || reason == REASON_DEFAULT_RST || reason == REASON_EXT_SYS_RST;
  bool forceSafe = (s.magic == RTC_MAGIC) && s.forceSafe == 1;

  if (coldBoot) s.crashes = 0;
  else if (s.planned == 1) s.crashes = 0;
  else s.crashes++;

  s.magic = RTC_MAGIC;
  s.planned = 0;
  s.forceSafe = 0;
  rtcWrite(s);

  s_crashes = s.crashes;
  s_safe = forceSafe || s.crashes >= CRASHES_FOR_SAFE_MODE;
  s_noDisplay = s.crashes >= CRASHES_FOR_NO_DISPLAY;
}

static void markStable() {
  RtcState s;
  rtcRead(s);
  s.magic = RTC_MAGIC;
  s.crashes = 0;
  rtcWrite(s);
  s_crashes = 0;
}

void plannedRestart(bool intoSafeMode) {
  RtcState s;
  rtcRead(s);
  s.magic = RTC_MAGIC;
  s.planned = 1;
  s.forceSafe = intoSafeMode ? 1 : 0;
  rtcWrite(s);
  delay(200);
  ESP.restart();
}

// ------------------------------------------------------------ WiFi
static void startAP() {
  if (s_ap) return;
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(s_apName, AP_PASS);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", IPAddress(192, 168, 4, 1));
  s_ap = true;
}

static void stopAP() {
  if (!s_ap) return;
  dns.stop();
  WiFi.softAPdisconnect(false);
  s_ap = false;
}

static void wifiPolicy() {
  bool sta = WiFi.status() == WL_CONNECTED;
  uint32_t now = millis();
  if (sta) s_staLostSince = 0;
  else if (!s_staLostSince) s_staLostSince = now ? now : 1;

  if (s_safe || s_updating) { startAP(); return; }
  if (!sta && s_staLostSince && now - s_staLostSince > STA_LOST_AP_MS) startAP();
  if (sta && now > AP_GRACE_MS && WiFi.softAPgetStationNum() == 0) stopAP();
}

// ------------------------------------------------------------ web pages
static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>GeekQuest clock</title>
<style>body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;background:#14171c;color:#e8eaee}
a,button{color:#14171c;background:#e89a3c;border:0;border-radius:6px;padding:8px 14px;font:inherit;text-decoration:none;display:inline-block;cursor:pointer}
.box{background:#1d2128;border:1px solid #2e333c;border-radius:8px;padding:14px;margin:12px 0}
td{padding:2px 10px 2px 0}.warn{color:#ffb4a8}.ok{color:#7fdc9b}input{font:inherit;padding:6px;width:100%;box-sizing:border-box;margin:4px 0}
progress{width:100%;height:18px}</style></head><body>)HTML";
static const char PAGE_TAIL[] PROGMEM = "</body></html>";

// The update page. GQ-OTA-PAGE-v1 is a marker the CI check looks for in every .bin.
static const char PAGE_UPDATE[] PROGMEM = R"HTML(<h2>Firmware update</h2><!-- GQ-OTA-PAGE-v1 -->
<div class="box"><p>Choose a firmware <b>.bin</b>. This page accepts GeekQuest builds and the original
SDPro stock firmware. The image is checked before anything is replaced; a failed upload leaves the
current firmware running.</p>
<input id="f" type="file" accept=".bin"><p id="chk"></p>
<button id="go" disabled>Upload and install</button><p><progress id="p" max="100" value="0" hidden></progress></p>
<p id="msg"></p></div><p><a href="/">Back</a></p>
<script>
const f=document.getElementById('f'),go=document.getElementById('go'),chk=document.getElementById('chk'),msg=document.getElementById('msg'),p=document.getElementById('p');
let free=0;fetch('/info').then(r=>r.json()).then(j=>{free=j.free_ota_bytes});
f.onchange=()=>{go.disabled=true;const x=f.files[0];if(!x)return;
 const rd=new FileReader();rd.onload=()=>{const b=new Uint8Array(rd.result);let e=[];
  if(b[0]!==0xE9)e.push('not an ESP8266 firmware image (first byte is not 0xE9)');
  else if((b[3]>>4)!==4)e.push('image is not built for 4MB flash');
  if(free&&x.size>free)e.push('too big: '+x.size+' bytes, only '+free+' free');
  if(e.length){chk.className='warn';chk.textContent='Cannot install: '+e.join('; ')}
  else{chk.className='ok';chk.textContent='Looks good: '+x.name+', '+x.size+' bytes.';go.disabled=false}};
 rd.readAsArrayBuffer(x.slice(0,16))};
go.onclick=()=>{const x=f.files[0];if(!x)return;go.disabled=true;f.disabled=true;p.hidden=false;msg.textContent='Uploading... do not unplug the clock.';
 const fd=new FormData();fd.append('firmware',x,x.name);const q=new XMLHttpRequest();q.open('POST','/update');
 q.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded*100/e.total)};
 q.onload=()=>{if(q.status==200){msg.className='ok';msg.textContent=q.responseText+' The clock restarts in a few seconds; this page will reload.';setTimeout(()=>location.href='/',15000)}
  else{msg.className='warn';msg.textContent='Update failed: '+q.responseText+'. The current firmware is still installed.';go.disabled=false;f.disabled=false}};
 q.onerror=()=>{msg.className='warn';msg.textContent='Connection lost. If the clock does not come back within a minute, power-cycle it; the previous firmware stays installed unless the upload completed.'};
 q.send(fd)};
</script>)HTML";

static String htmlEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    if (c == '<') o += F("&lt;");
    else if (c == '>') o += F("&gt;");
    else if (c == '&') o += F("&amp;");
    else if (c == '"') o += F("&quot;");
    else o += c;
  }
  return o;
}

static uint32_t freeOtaBytes() {
  uint32_t f = ESP.getFreeSketchSpace();
  return f > 0x1000 ? (f - 0x1000) & 0xFFFFF000 : 0;
}

static void sendPage(const String& body) {
  String s;
  s.reserve(body.length() + 1200);
  s += FPSTR(PAGE_HEAD);
  s += body;
  s += FPSTR(PAGE_TAIL);
  server.send(200, F("text/html; charset=utf-8"), s);
}

static void handleRoot() {
  String b;
  b.reserve(1800);
  b += F("<h2>GeekQuest clock</h2>");
  if (s_safe) b += F("<div class='box warn'><b>Safe mode.</b> The game is not running (it crashed repeatedly or safe mode was requested). Install a fixed firmware below, or reboot normally.</div>");
  b += F("<div class='box'><table>");
  b += F("<tr><td>Firmware</td><td>"); b += F(GQ_VERSION); b += F("</td></tr>");
  b += F("<tr><td>WiFi</td><td>");
  if (WiFi.status() == WL_CONNECTED) { b += htmlEscape(WiFi.SSID()); b += F(" ("); b += WiFi.localIP().toString(); b += F(")"); }
  else b += F("not connected");
  b += F("</td></tr><tr><td>Rescue hotspot</td><td>");
  if (s_ap) { b += s_apName; b += F(" (on, password "); b += AP_PASS; b += F(")"); }
  else b += F("off");
  b += F("</td></tr><tr><td>Free heap</td><td>"); b += ESP.getFreeHeap();
  b += F(" bytes</td></tr><tr><td>Firmware size</td><td>"); b += ESP.getSketchSize();
  b += F(" bytes</td></tr><tr><td>Room for update</td><td>"); b += freeOtaBytes();
  b += F(" bytes</td></tr><tr><td>Last reset</td><td>"); b += htmlEscape(ESP.getResetReason());
  b += F("</td></tr><tr><td>Crash counter</td><td>"); b += s_crashes;
  b += F("</td></tr></table></div>");
  b += F("<p><a href='/update'>Update firmware</a> <a href='/wifi'>WiFi setup</a></p>");
  b += F("<div class='box'><form method='post' action='/settings'>Brightness (0-100)<input name='bl' value='");
  b += lcd::brightness();
  b += F("'>Time zone (POSIX TZ, e.g. &lt;-03&gt;3 for UTC-3)<input name='tz' value='");
  b += htmlEscape(s_tz);
  b += F("'><button>Apply</button> <small>Not saved across restarts yet.</small></form></div>");
  b += F("<div class='box'>Display test (only if colours look wrong; not saved):<br><br>"
         "<a href='/display?madctl=0'>RGB order</a> <a href='/display?madctl=8'>BGR order</a> "
         "<a href='/display?inv=1'>Inversion on</a> <a href='/display?inv=0'>Inversion off</a></div>");
  b += F("<form method='post' action='/reboot' style='display:inline'><button>Restart</button></form> "
         "<form method='post' action='/safemode' style='display:inline'><button>Restart in safe mode</button></form>");
  sendPage(b);
}

static void handleInfo() {
  String j;
  j.reserve(400);
  j += F("{\"version\":\""); j += F(GQ_VERSION);
  j += F("\",\"safe_mode\":"); j += s_safe ? F("true") : F("false");
  j += F(",\"crashes\":"); j += s_crashes;
  j += F(",\"heap\":"); j += ESP.getFreeHeap();
  j += F(",\"sketch_bytes\":"); j += ESP.getSketchSize();
  j += F(",\"free_ota_bytes\":"); j += freeOtaBytes();
  j += F(",\"flash_real\":"); j += ESP.getFlashChipRealSize();
  j += F(",\"sta_ip\":\""); j += WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String();
  j += F("\",\"ap\":"); j += s_ap ? F("true") : F("false");
  j += F(",\"uptime_s\":"); j += millis() / 1000;
  j += F("}");
  server.send(200, F("application/json"), j);
}

static void handleUpdatePage() { sendPage(FPSTR(PAGE_UPDATE)); }

static void uploadFail(const String& why) {
  if (!s_upFailed) {
    s_upFailed = true;
    s_upError = why;
  }
  if (Update.isRunning()) Update.end(false);  // abandon: nothing is committed
}

static void handleUpdateChunk() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_updating = true;
    s_upFailed = false;
    s_upError = "";
    s_upBytes = 0;
    WiFiUDP::stopAll();
    uint32_t maxSpace = freeOtaBytes();
    Update.clearError();
    if (!Update.begin(maxSpace, U_FLASH)) uploadFail(String(F("cannot start: ")) + Update.getErrorString());
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (s_upFailed) return;
    if (s_upBytes == 0) {
      if (up.currentSize < 4 || up.buf[0] != 0xE9) { uploadFail(F("not an ESP8266 firmware image (bad magic byte)")); return; }
      if ((up.buf[3] >> 4) != 4) { uploadFail(F("image is not built for 4MB flash")); return; }
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      uploadFail(String(F("write failed: ")) + Update.getErrorString());
      return;
    }
    s_upBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_upFailed) return;
    if (s_upBytes == 0) { uploadFail(F("empty file")); return; }
    if (!Update.end(true)) uploadFail(String(F("verify failed: ")) + Update.getErrorString());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    uploadFail(F("upload aborted"));
  }
  yield();
}

static void handleUpdateDone() {
  if (s_upFailed || Update.hasError()) {
    s_updating = false;
    server.send(500, F("text/plain"), s_upError.length() ? s_upError : String(F("unknown error")));
    return;
  }
  server.sendHeader(F("Connection"), F("close"));
  server.send(200, F("text/plain"), String(F("Installed ")) + s_upBytes + F(" bytes."));
  delay(500);
  plannedRestart(false);
}

static void handleWifiPage() {
  String b = F("<h2>WiFi setup</h2><div class='box'><form method='post' action='/wifi'>"
               "Network name<input name='ssid' maxlength='32'>Password<input name='pass' type='password' maxlength='64'>"
               "<button>Save and connect</button></form><p><small>Saved in the ESP8266's own WiFi settings area, not in the stock firmware's files.</small></p></div><p><a href='/'>Back</a></p>");
  sendPage(b);
}

static void handleWifiSave() {
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (!ssid.length()) { server.send(400, F("text/plain"), F("Network name is empty")); return; }
  sendPage(F("<div class='box'>Connecting... Find the clock's new address on its screen, or keep using the rescue hotspot.</div><p><a href='/'>Back</a></p>"));
  delay(300);
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.persistent(false);
}

static void handleSettings() {
  if (server.hasArg("bl")) lcd::setBrightness(server.arg("bl").toInt());
  if (server.hasArg("tz") && server.arg("tz").length() && server.arg("tz").length() < 48) {
    s_tz = server.arg("tz");
    setenv("TZ", s_tz.c_str(), 1);
    tzset();
  }
  server.sendHeader(F("Location"), F("/"));
  server.send(303);
}

static void handleDisplay() {
  if (!s_noDisplay) {
    if (server.hasArg("madctl")) lcd::setMadctl((uint8_t)server.arg("madctl").toInt());
    if (server.hasArg("inv")) lcd::setInversion(server.arg("inv").toInt() != 0);
  }
  server.sendHeader(F("Location"), F("/"));
  server.send(303);
}

static void handleNotFound() {
  if (s_ap && server.hostHeader() != WiFi.softAPIP().toString() && WiFi.softAPgetStationNum() > 0) {
    server.sendHeader(F("Location"), String(F("http://")) + WiFi.softAPIP().toString() + F("/"));
    server.send(302);
    return;
  }
  server.send(404, F("text/plain"), F("Not found"));
}

// ------------------------------------------------------------ public
void begin() {
  bootGuard();

  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(s_apName, sizeof(s_apName), "GEEKQUEST-%02X%02X", mac[4], mac[5]);

  // Never let routine WiFi calls write to flash; only the WiFi setup form does.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  startAP();

  // Reconnect to the network saved in the ESP8266's WiFi settings (the stock
  // firmware normally leaves its network there).
  station_config conf;
  memset(&conf, 0, sizeof(conf));
  wifi_station_get_config_default(&conf);
  if (conf.ssid[0]) WiFi.begin();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateChunk);
  server.on("/update_ota", HTTP_GET, handleUpdatePage);   // same path as the stock firmware
  server.on("/update_ota", HTTP_POST, handleUpdateDone, handleUpdateChunk);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/settings", HTTP_POST, handleSettings);
  server.on("/display", HTTP_GET, handleDisplay);
  server.on("/reboot", HTTP_POST, []() { server.send(200, F("text/plain"), F("Restarting")); delay(300); plannedRestart(false); });
  server.on("/safemode", HTTP_POST, []() { server.send(200, F("text/plain"), F("Restarting into safe mode")); delay(300); plannedRestart(true); });
  server.onNotFound(handleNotFound);
  server.begin();

  MDNS.begin("geekquest");
  MDNS.addService("http", "tcp", 80);

  settimeofday_cb([]() { s_timeSynced = true; });
  configTime(s_tz.c_str(), "pool.ntp.org", "time.google.com");
}

void loop() {
  server.handleClient();
  if (s_ap) dns.processNextRequest();
  MDNS.update();
  wifiPolicy();
  if (!s_stableMarked && millis() > STABLE_AFTER_MS) {
    s_stableMarked = true;
    markStable();
  }
}

bool safeMode() { return s_safe; }
bool noDisplay() { return s_noDisplay; }
bool updating() { return s_updating; }
bool staConnected() { return WiFi.status() == WL_CONNECTED; }
bool apActive() { return s_ap; }
String ipString() { return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String(); }
const char* apName() { return s_apName; }
const char* apPassword() { return AP_PASS; }

bool localTime(int& hour, int& minute) {
  if (!s_timeSynced) return false;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  hour = t.tm_hour;
  minute = t.tm_min;
  return true;
}

}  // namespace safety
