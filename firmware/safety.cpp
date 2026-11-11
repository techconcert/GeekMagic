// See safety.h. Keep this file boring and robust.
#include "safety.h"
#include "version.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#if GQ_MDNS
#include <ESP8266mDNS.h>
#endif
#include <DNSServer.h>
#include <Updater.h>
#include <WiFiUdp.h>
#include <time.h>
#include <stdarg.h>
#include <stdlib.h>
#include <user_interface.h>

#include "st7789.h"
// RECOVERY-CHANGE (0B): the WiFi ladder lives in netwatch (state machine in lib/gqsafe)
#include "netwatch.h"
// RECOVERY-CHANGE (chunk1): pure decision code (host-tested) and the settings record (kept in RTC memory)
#include "bootlogic.h"
#include "upcheck.h"
#include "gq_crc32.h"
#include "settings.h"
#include "prefs.h"
// RECOVERY-CHANGE (R3-0F): page writer, and the status lines of the three R3 modules (stubs until their chunks land)
#include "webout.h"
#include "config.h"
#include "wxnet.h"
#include "nightmode.h"

#include <coredecls.h>   // settimeofday_cb

// RECOVERY-CHANGE (chunk1): approval switches (design 12.2). Both default OFF in platformio.ini.
//   GQ_AUTO_RESTART  1 = WiFi, heap and maintenance restarts really restart; 0 = they are only logged (A2, approved: 1)
//   GQ_STRICT_UPLOAD 1 = size, CRC and marker checks of an upload are enforced; 0 = computed and shown only (A4, still 0)
// GQ_MDNS (default 0) compiles the geekquest.local responder in; the clock shows its IP address instead.
#ifndef GQ_AUTO_RESTART
#define GQ_AUTO_RESTART 1
#endif
#ifndef GQ_STRICT_UPLOAD
#define GQ_STRICT_UPLOAD 0
#endif

namespace safety {

// ------------------------------------------------------------ constants
static const char AP_PASS[] = "GEEKQUEST";        // WPA2, 8+ chars; uppercase so it reads the same on the clock font
static const uint32_t STABLE_AFTER_MS = 60UL * 1000;
static const uint32_t AP_GRACE_MS = 10UL * 60 * 1000;  // AP stays up 10 min after boot
// RECOVERY-CHANGE (0B): STA_LOST_AP_MS removed; the 60 s rule now lives in netfsm.cpp
// RECOVERY-CHANGE (chunk1): CRASHES_FOR_* moved to lib/gqsafe/src/bootlogic.h
// RECOVERY-CHANGE (chunk1): revision 2 = chunk 1 (design 8.10 rule 6); 3 = R2 (no game saving, automatic restarts on)
// RECOVERY-CHANGE (R3-0F): revision 5 = R3 plumbing (web() accessor, webout.h, weather strike words, boot kind, spare sector probe)
static const uint32_t GQ_RECOVERY_REV = 5;
// RECOVERY-CHANGE (chunk1): timers of the restart gates
static const uint32_t SLOW_STABLE_MS = 6UL * 3600 * 1000;   // `slow` counter cleared after 6 h of uptime
static const uint32_t AUTO_MIN_UPTIME_MS = 30UL * 60 * 1000;
static const uint32_t HEAP_SAMPLE_MS = 10UL * 1000;
static const uint32_t LOW_HEAP_PAGE = 6144;                 // below this `/` sends the tiny page
static const uint32_t STOCK_SIZE = 488304;                  // SDPro_V1.0.6 stock image
static const uint32_t STOCK_CRC32 = 0;                      // unknown until the user reports it (design 12.2 A6); 0 = size only

// RTC user memory. Offset 0 is used by eboot to pass the OTA copy command,
// so we stay well clear of it (offset is in 4-byte blocks).
static const uint32_t RTC_OFFSET = 96;
static const uint32_t RTC_MAGIC = 0x47514247;  // "GQBG"
struct RtcState {            // RECOVERY-CHANGE (chunk1): layout unchanged (blocks 96..100)
  uint32_t magic;
  uint32_t crashes;
  uint32_t planned;     // 1 = the last restart was ours (OTA, reboot button)
  uint32_t forceSafe;   // 1 = user asked for safe mode on next boot
  uint32_t stage;       // last boot stage reached (see safety::stage)
};
// RECOVERY-CHANGE (chunk1): second record, blocks 101..111 (44 bytes)
static const uint32_t RTC2_OFFSET = 101;
static const uint32_t RTC2_MAGIC = 0x47513242;  // "GQ2B"
struct RtcState2 {
  uint32_t magic2;
  uint32_t slow;            // unplanned resets, cleared after 6 h of uptime
  uint32_t spareSave;       // unused since R2 (was: the boot loaded a game save); keeps the layout
  uint32_t wifiRestarts;    // WiFi-caused restarts since the last good link
  uint32_t everConnected;   // since power-on
  uint32_t lastReason;      // four RestartReason bytes, newest in the low byte
  uint32_t bootCount;
  uint32_t wxBusy;          // R3: 1 while a weather fetch is running (wxMark); a boot after an unplanned reset with it set adds a strike
  uint32_t wxStrikes;       // R3: weather fetches that ended in an unplanned reset; cleared by power-on or wxClearStrikes()
  uint32_t spare2;
  uint32_t crc;             // CRC32 of the ten words above
};
static_assert(sizeof(RtcState2) == 44, "RtcState2 must fit blocks 101..111");

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
// RECOVERY-CHANGE (0B): s_staLostSince removed together with wifiPolicy()
static uint32_t s_bootMs = 0;        // RECOVERY-CHANGE (0B): latched time base for the two timers below
static bool s_golden = true;         // RECOVERY-CHANGE (0B): first 10 minutes of uptime, latched
static char s_apName[24];
static uint32_t s_crashes = 0;
static uint32_t s_lastStage = 0;     // stage reached before the previous reset
static bool s_timeSynced = false;
static int s_depth = 40;
// RECOVERY-CHANGE (chunk1): time zone is a fixed buffer (validated by gqs_tz_valid)
static char s_tz[GQS_TZ_MAX + 1] = "<-03>3";
// RECOVERY-CHANGE (chunk1): restart machinery, health numbers, upload checks
static RtcState2 s_r2;
static bool s_cold = false;
static uint8_t s_bootKind = 0;           // RECOVERY-CHANGE (R3-0F): BootKind
static bool s_slowCleared = false;
static uint8_t s_pending = RR_NONE;
static bool (*s_asleepFn)() = nullptr;
static uint32_t s_loggedAuto[3];          // WiFi, heap, maintenance requests that were only logged this boot
static HeapWatch s_hw;
static uint32_t s_lastHeapSample = 0;
static bool s_heapWarn = false, s_heapReq = false, s_maintReq = false;
static uint32_t s_lastReq = 0;            // millis() of the last web request, 0 = never
static uint16_t s_fps10 = 0;
static uint32_t s_loopMax = 0;
static uint32_t s_freeAtUpload = 0;
static UpChk s_upc;
static UpRule s_upRule;
static int s_upVerdict = -1;              // last upload check result (UV_*), -1 = none yet

// GQ_OTA_MARKER is the ONLY copy of the marker text in the firmware: check_bin.py requires it exactly once,
// the upload check scans for it in every image, and the page JavaScript only builds it from two pieces.
static const char GQ_OTA_MARKER[] __attribute__((used)) = "GQ-OTA-PAGE-v1";

// ------------------------------------------------------------ boot guard
static void rtcRead(RtcState& s) { ESP.rtcUserMemoryRead(RTC_OFFSET, (uint32_t*)&s, sizeof(s)); }
static void rtcWrite(RtcState& s) { ESP.rtcUserMemoryWrite(RTC_OFFSET, (uint32_t*)&s, sizeof(s)); }
// RECOVERY-CHANGE (chunk1): second record, always written through the RAM copy s_r2
static uint32_t r2Crc(const RtcState2& r) { return gq_crc32((const uint8_t*)&r, sizeof(r) - 4); }
static void r2Write() {
  s_r2.magic2 = RTC2_MAGIC;
  s_r2.crc = r2Crc(s_r2);
  ESP.rtcUserMemoryWrite(RTC2_OFFSET, (uint32_t*)&s_r2, sizeof(s_r2));
}
static void r2PushReason(uint8_t why) {
  s_r2.lastReason = (s_r2.lastReason << 8) | why;
}

static bool isAuto(uint8_t why);

static void bootGuard() {
  RtcState s;
  rtcRead(s);
  const rst_info* ri = ESP.getResetInfoPtr();
  uint32_t reason = ri ? ri->reason : REASON_DEFAULT_RST;
  bool valid1 = (s.magic == RTC_MAGIC);
  bool coldBoot = !valid1 || reason == REASON_DEFAULT_RST || reason == REASON_EXT_SYS_RST;

  // RECOVERY-CHANGE (chunk1): second record; invalid or foreign content counts as zeros
  RtcState2 r2;
  ESP.rtcUserMemoryRead(RTC2_OFFSET, (uint32_t*)&r2, sizeof(r2));
  bool valid2 = (r2.magic2 == RTC2_MAGIC) && (r2.crc == r2Crc(r2));
  if (!valid2 || coldBoot) memset(&r2, 0, sizeof(r2));

  s_lastStage = valid1 ? s.stage : 0;

  // RECOVERY-CHANGE (R3-0F): weather strikes. The busy word is set around a fetch; if the previous run ended without
  // a planned restart while it was set, the fetch is the suspect. The busy word never survives a boot.
  bool prevPlanned = valid1 && s.planned == 1;
  uint8_t prevWhy = (uint8_t)(r2.lastReason & 0xFF);   // zero after a power-on (r2 was cleared above)
  if (!coldBoot && r2.wxBusy && !prevPlanned && r2.wxStrikes < 100) r2.wxStrikes++;
  r2.wxBusy = 0;

  // RECOVERY-CHANGE (chunk1): the counting rules live in bootlogic.cpp (host-tested)
  bool wasPlanned = valid1 && s.planned == 1;
  BootCounters c;
  c.crashes = valid1 ? s.crashes : 0;
  c.slow = r2.slow;
  c.planned = valid1 ? s.planned : 0;
  c.force_safe = valid1 ? s.forceSafe : 0;
  BootDecision d;
  boot_decide(&c, coldBoot ? 1 : 0, &d);

  s.magic = RTC_MAGIC;
  s.crashes = c.crashes;
  s.planned = c.planned;
  s.forceSafe = c.force_safe;
  s.stage = 0;
  rtcWrite(s);

  r2.slow = c.slow;
  r2.bootCount++;
  s_r2 = r2;
  if (coldBoot) {
    s_r2.lastReason = 0;
    r2PushReason(RR_POWER);
  } else if (!wasPlanned) {
    r2PushReason(RR_CRASH);                      // the previous run ended without a planned restart
  }
  r2Write();
  s_cold = coldBoot;
  s_bootKind = coldBoot ? BK_POWER : (!wasPlanned ? BK_CRASH : (isAuto(prevWhy) ? BK_AUTO : BK_USER));

  s_crashes = c.crashes;
  s_safe = d.safe != 0;
  s_noDisplay = d.no_display != 0;
}

static void markStable() {
  RtcState s;
  rtcRead(s);
  s.magic = RTC_MAGIC;
  s.crashes = 0;
  rtcWrite(s);
  s_crashes = 0;
}

void stage(uint8_t n) {
  RtcState s;
  rtcRead(s);
  s.magic = RTC_MAGIC;
  s.stage = n;
  rtcWrite(s);
}

// RECOVERY-CHANGE (chunk1): one word only (the stage is the 5th word of RtcState)
void tickMarker(uint8_t n) {
  uint32_t w = n;
  ESP.rtcUserMemoryWrite(RTC_OFFSET + 4, &w, sizeof(w));
}

// RECOVERY-CHANGE (chunk1): restart reasons. Boot-time unplanned resets are logged here too.
const char* restartName(uint8_t reason) {
  static const char* const N[] = {"none", "user", "update", "WiFi", "heap", "maintenance",
                                  "safe mode", "-", "crash", "power-on"};
  return reason < sizeof(N) / sizeof(N[0]) ? N[reason] : "?";
}

static bool isAuto(uint8_t why) {
  return why == RR_WIFI || why == RR_HEAP || why == RR_MAINT; }

static void doRestart(uint8_t why) {
  RtcState s;
  rtcRead(s);
  s.magic = RTC_MAGIC;
  s.planned = 1;
  s.forceSafe = (why == RR_SAFEMODE) ? 1 : 0;
  rtcWrite(s);
  r2PushReason(why);
  if (why == RR_WIFI) s_r2.wifiRestarts++;
  // A restart the user chose from safe mode, and a freshly installed image, get a clean slate for `slow`.
  if (why == RR_UPDATE || (why == RR_USER && s_safe)) s_r2.slow = 0;
  r2Write();
  delay(300);                                  // let the browser receive the reply
  ESP.restart();
}

void requestRestart(RestartReason why) {
  if (why == RR_NONE || why == RR_CRASH || why == RR_POWER) return;
  if (isAuto(why) && s_updating) return;       // never restart by ourselves while a firmware upload is running
  if (isAuto(why) && !GQ_AUTO_RESTART) {       // switched off in this build: log only
    uint8_t i = why == RR_WIFI ? 0 : (why == RR_HEAP ? 1 : 2);
    s_loggedAuto[i]++;
    Serial.print(F("safety: automatic restart not enabled in this build, would restart: "));
    Serial.println(restartName(why));
    return;
  }
  if (s_pending == RR_NONE || !isAuto(why)) s_pending = why;   // a user request wins over an automatic one
}

bool autoRestartAllowed() {
  return millis() - s_bootMs >= AUTO_MIN_UPTIME_MS && !s_golden && !s_updating && !s_safe &&
         WiFi.softAPgetStationNum() == 0;
}

static void processRestart() {
  if (s_pending == RR_NONE) return;
  if (isAuto(s_pending) && !autoRestartAllowed()) return;   // wait until it is allowed
  uint8_t why = s_pending;
  s_pending = RR_NONE;
  doRestart(why);
}

void plannedRestart(bool intoSafeMode) {
  doRestart(intoSafeMode ? RR_SAFEMODE : RR_USER);
}

bool coldBoot() { return s_cold; }

// RECOVERY-CHANGE (R3-0F): R3 accessors
ESP8266WebServer& web() { return server; }
BootKind bootKind() { return (BootKind)s_bootKind; }
void wxMark(bool busy) {
  uint32_t v = busy ? 1 : 0;
  if (s_r2.wxBusy == v) return;
  s_r2.wxBusy = v;
  r2Write();
}
uint8_t wxStrikes() { return s_r2.wxStrikes > 255 ? 255 : (uint8_t)s_r2.wxStrikes; }
void wxClearStrikes() { if (s_r2.wxStrikes) { s_r2.wxStrikes = 0; r2Write(); } }

uint8_t wifiRestarts() { return s_r2.wifiRestarts > 255 ? 255 : (uint8_t)s_r2.wifiRestarts; }
void noteLinkGood() { if (s_r2.wifiRestarts) { s_r2.wifiRestarts = 0; r2Write(); } }
bool everConnected() { return s_r2.everConnected != 0; }
void noteConnected() { if (!s_r2.everConnected) { s_r2.everConnected = 1; r2Write(); } }

void setHealth(uint16_t fps10, uint32_t loopMaxMs) { s_fps10 = fps10; s_loopMax = loopMaxMs; }
void setAsleepFn(bool (*fn)()) { s_asleepFn = fn; }
void setDepth(int d) { s_depth = d < 0 ? 0 : (d > 100 ? 100 : d); }
uint32_t msSinceRequest() { return s_lastReq ? millis() - s_lastReq : 0xFFFFFFFFu; }

bool setTimeZone(const char* tz) {
  if (!tz || !gqs_tz_valid(tz)) return false;
  strncpy(s_tz, tz, sizeof(s_tz) - 1);
  s_tz[sizeof(s_tz) - 1] = 0;
  setenv("TZ", s_tz, 1);
  tzset();
  return true;
}

// ------------------------------------------------------------ WiFi
// RECOVERY-CHANGE (0B): startAP()/stopAP() are no longer static (netwatch calls them); bodies unchanged
void startAP() {
  if (s_ap) return;
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(s_apName, AP_PASS);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", IPAddress(192, 168, 4, 1));
  s_ap = true;
}

void stopAP() {
  if (!s_ap) return;
  dns.stop();
  WiFi.softAPdisconnect(false);
  s_ap = false;
}

// RECOVERY-CHANGE (0B): wifiPolicy() removed; netwatch::loop() replaces it

// ------------------------------------------------------------ web pages
// RECOVERY-CHANGE (chunk1): every page is sent in pieces straight from flash or from a 480-byte stack buffer
// (chunked transfer, no String of the page is ever built), so a page needs no more heap than the connection itself.
static const char PAGE_HEAD[] PROGMEM = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>GeekQuest clock</title>
<style>body{font-family:system-ui,sans-serif;max-width:560px;margin:0 auto;padding:16px;background:#14171c;color:#e8eaee}
a,button{color:#14171c;background:#e89a3c;border:0;border-radius:6px;padding:8px 14px;font:inherit;text-decoration:none;display:inline-block;cursor:pointer;margin:2px 0}
.box{background:#1d2128;border:1px solid #2e333c;border-radius:8px;padding:14px;margin:12px 0}
td{padding:2px 10px 2px 0}.warn{color:#ffb4a8}.ok{color:#7fdc9b}input{font:inherit;padding:6px;width:100%;box-sizing:border-box;margin:4px 0}
input[type=checkbox],input[type=file]{width:auto}progress{width:100%;height:18px}small{color:#9aa1ad}form{display:inline}</style></head><body>)HTML";
static const char PAGE_TAIL[] PROGMEM = "</body></html>";

// The update page, in pieces so the optional "install anyway" box can be compiled in.
static const char PAGE_UPDATE_A[] PROGMEM = R"HTML(<h2>Firmware update</h2>
<div class="box"><p>Choose a firmware <b>.bin</b>. This page accepts GeekQuest builds and the original
SDPro stock firmware. The image is checked before anything is replaced; a failed upload leaves the
current firmware running. Keep the clock powered for 60 seconds after it says Installed.</p>
<input id="f" type="file" accept=".bin"><p id="chk"></p><p id="crc"></p>)HTML";
#if GQ_STRICT_UPLOAD
static const char PAGE_UPDATE_FORCE[] PROGMEM = R"HTML(<p><label><input type="checkbox" id="force"> Install anyway (skip the check that the file contains the GeekQuest update page)</label></p>)HTML";
#endif
static const char PAGE_UPDATE_B[] PROGMEM = R"HTML(<button id="go" disabled>Upload and install</button><p><progress id="p" max="100" value="0" hidden></progress></p>
<p id="msg"></p></div><p><a href="/">Back</a></p>
<script>
const f=document.getElementById('f'),go=document.getElementById('go'),chk=document.getElementById('chk'),cr=document.getElementById('crc'),msg=document.getElementById('msg'),p=document.getElementById('p');
let free=0,crc='',n=0;fetch('/info').then(r=>r.json()).then(j=>{free=j.free_ota_bytes});
function crc32(b){let c=-1;for(let i=0;i<b.length;i++){c^=b[i];for(let k=0;k<8;k++)c=(c>>>1)^(0xEDB88320&-(c&1))}return((c^-1)>>>0).toString(16).padStart(8,'0')}
function has(b){const m=Array.from('GQ-OTA-'+'PAGE-v1',c=>c.charCodeAt(0));o:for(let i=0;i+m.length<=b.length;i++){if(b[i]!==m[0])continue;for(let j=1;j<m.length;j++)if(b[i+j]!==m[j])continue o;return true}return false}
f.onchange=()=>{go.disabled=true;cr.textContent='';const x=f.files[0];if(!x)return;
 const rd=new FileReader();rd.onload=()=>{const b=new Uint8Array(rd.result);let e=[],w=[];n=b.length;crc=crc32(b);
  if(b[0]!==0xE9)e.push('not an ESP8266 firmware image (first byte is not 0xE9)');
  else if((b[3]>>4)!==4)e.push('image is not built for 4MB flash');
  if(free&&x.size>free)e.push('too big: '+x.size+' bytes, only '+free+' free');
  if(!has(b)&&x.size!==488304)w.push('this file does not contain the GeekQuest update page; after installing it you could not update from this page again');
  cr.textContent='CRC32 '+crc+', '+x.size+' bytes';
  if(e.length){chk.className='warn';chk.textContent='Cannot install: '+e.join('; ')}
  else{chk.className=w.length?'warn':'ok';chk.textContent=(w.length?'Warning: '+w.join('; ')+'. ':'Looks good: ')+x.name+', '+x.size+' bytes.';go.disabled=false}};
 rd.readAsArrayBuffer(x)};
go.onclick=()=>{const x=f.files[0];if(!x)return;go.disabled=true;f.disabled=true;p.hidden=false;msg.textContent='Uploading... do not unplug the clock.';
 const fc=document.getElementById('force'),fd=new FormData();fd.append('firmware',x,x.name);const q=new XMLHttpRequest();q.open('POST','/update?crc='+crc+'&size='+n+'&force='+(fc&&fc.checked?1:0));
 q.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded*100/e.total)};
 q.onload=()=>{if(q.status==200){msg.className='ok';msg.textContent=q.responseText+' The clock restarts in a few seconds; this page will reload.';setTimeout(()=>location.href='/',15000)}
  else{msg.className='warn';msg.textContent='Update failed: '+q.responseText+'. The current firmware is still installed.';go.disabled=false;f.disabled=false}};
 q.onerror=()=>{msg.className='warn';msg.textContent='Connection lost. If the clock does not come back within a minute, power-cycle it; the previous firmware stays installed unless the upload completed.'};
 q.send(fd)};
</script>)HTML";

static const char PAGE_WIFI[] PROGMEM = R"HTML(<h2>WiFi setup</h2><div class='box'><form method='post' action='/wifi'>
Network name<input name='ssid' maxlength='32'>Password<input name='pass' type='password' maxlength='64'>
<button>Save and connect</button></form><p><small>Saved in the ESP8266's own WiFi settings area, not in the stock firmware's files.</small></p></div><p><a href='/'>Back</a></p>)HTML";

static const char PAGE_LOWMEM[] PROGMEM = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>GeekQuest</title>"
    "<body style='font-family:sans-serif;padding:16px'><h3>GeekQuest clock: low memory</h3><p>Firmware " GQ_VERSION
    ". The full status page is skipped to save memory.</p><p><a href='/update'>Update firmware</a></p>"
    "<form method='post' action='/reboot'><button>Restart</button></form></body>";

using webout::Out;   // RECOVERY-CHANGE (R3-0F): the writer lives in webout.h

void touch() {
  netwatch::noteHttp();
  uint32_t n = millis();
  s_lastReq = n ? n : 1;
}

void beginChunked(int code, const __FlashStringHelper* type) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(code, type, "");
}
void endChunked() { server.sendContent(""); }
void beginPage() { beginChunked(200, F("text/html; charset=utf-8")); server.sendContent_P(PAGE_HEAD); }
void endPage() { server.sendContent_P(PAGE_TAIL); endChunked(); }
static void redirectHome() {
  server.sendHeader(F("Location"), F("/"));
  server.send(303);
}

static uint32_t freeOtaBytes() {
  uint32_t f = ESP.getFreeSketchSpace();
  return f > 0x1000 ? (f - 0x1000) & 0xFFFFF000 : 0;
}

static const char* verdictText(int v) {
  switch (v) {
    case UV_OK: return "ok";
    case UV_SIZE: return "fewer bytes arrived than the page sent: the upload was cut short. Try again";
    case UV_CRC: return "the file arrived damaged (CRC32 does not match). Nothing was installed. Try again";
    case UV_NO_MARKER: return "this image does not contain the GeekQuest update page, so you could not update from the page afterwards. If it is the original SDPro firmware or you are sure, tick Install anyway and retry";
    default: return "?";
  }
}

static void getBssid(char* out, size_t n) {
  const uint8_t* b = WiFi.BSSID();
  if (!b) { snprintf(out, n, "-"); return; }
  snprintf(out, n, "%02X:%02X:%02X:%02X:%02X:%02X", b[0], b[1], b[2], b[3], b[4], b[5]);
}

static void sendReasons(Out& o, bool json) {
  uint32_t r = s_r2.lastReason;
  for (int i = 0; i < 4; i++) {
    uint8_t why = (uint8_t)(r >> (8 * i));
    if (json) o.f(PSTR("%s\"%s\""), i ? "," : "", restartName(why));
    else if (why) o.f(PSTR("%s%s"), i ? ", " : "", restartName(why));
  }
}

// RECOVERY-CHANGE (R3-0F): the k-th non-empty status line of the R3 modules (nightmode, weather, settings), or false.
// Stubs return empty lines, so the status page and /info are unchanged until the modules are filled in.
static bool r3Line(uint8_t k, char* buf, size_t n) {
  for (uint8_t i = 0; i < 3; i++) {
    buf[0] = 0;
    if (i == 0) nightmode::statusLine(buf, n);
    else if (i == 1) wxnet::statusLine(buf, n);
    else config::statusLine(buf, n);
    if (buf[0] && k-- == 0) return true;
  }
  buf[0] = 0;
  return false;
}

static void handleRoot() {
  touch();
  if (ESP.getFreeHeap() < LOW_HEAP_PAGE) { server.send_P(200, PSTR("text/html"), PAGE_LOWMEM); return; }
  beginPage();
  Out o(server);
  char tmp[64];
  o.p(PSTR("<h2>GeekQuest clock</h2>"));
  if (s_safe) o.p(PSTR("<div class='box warn'><b>Safe mode.</b> The game is not running (it crashed repeatedly or safe mode was requested). Install a fixed firmware below, or restart normally.</div>"));
  if (s_heapWarn) o.p(PSTR("<div class='box warn'>Memory is low (under 12 KB free for a minute). Restarting from this page clears it.</div>"));

  // ---- status
  o.p(PSTR("<div class='box'><table>"));
  o.f(PSTR("<tr><td>Firmware</td><td>%s</td></tr><tr><td>WiFi</td><td>"), GQ_VERSION);
  if (WiFi.status() == WL_CONNECTED) { o.esc(WiFi.SSID().c_str()); o.f(PSTR(" (%s)"), WiFi.localIP().toString().c_str()); }
  else o.p(PSTR("not connected"));
  o.p(PSTR("</td></tr><tr><td>Rescue hotspot</td><td>"));
  if (s_ap) o.f(PSTR("%s (on, password %s)"), s_apName, AP_PASS);
  else o.p(PSTR("off"));
  o.f(PSTR("</td></tr><tr><td>Firmware size</td><td>%u bytes</td></tr><tr><td>Room for update</td><td>%u bytes</td></tr>"),
      (unsigned)ESP.getSketchSize(), (unsigned)freeOtaBytes());
  o.f(PSTR("<tr><td>Last reset</td><td>%s</td></tr><tr><td>Crash counter</td><td>%u fast, %u slow</td></tr>"),
      ESP.getResetReason().c_str(), (unsigned)s_crashes, (unsigned)s_r2.slow);
  o.p(PSTR("<tr><td>Last restarts</td><td>"));
  sendReasons(o, false);
  o.f(PSTR("</td></tr><tr><td>Automatic restarts</td><td>%s"), GQ_AUTO_RESTART ? "on" : "off in this build");
  if (!GQ_AUTO_RESTART && (s_loggedAuto[0] | s_loggedAuto[1] | s_loggedAuto[2]))
    o.f(PSTR(" (would have restarted: WiFi %u, heap %u, weekly %u)"), (unsigned)s_loggedAuto[0], (unsigned)s_loggedAuto[1], (unsigned)s_loggedAuto[2]);
  o.f(PSTR("</td></tr><tr><td>Upload checks</td><td>%s"), GQ_STRICT_UPLOAD ? "enforced" : "computed, not enforced");
  if (s_upVerdict > UV_OK) o.f(PSTR("; last upload %s"), GQ_STRICT_UPLOAD ? "refused" : "would have been refused");
  o.p(PSTR("</td></tr>"));
  const rst_info* ri = ESP.getResetInfoPtr();
  if (ri && ri->reason == REASON_EXCEPTION_RST) {
    o.f(PSTR("<tr><td>Crash details</td><td>cause %u, epc1 0x%08x, excvaddr 0x%08x, depc 0x%08x</td></tr>"),
        (unsigned)ri->exccause, (unsigned)ri->epc1, (unsigned)ri->excvaddr, (unsigned)ri->depc);
    o.f(PSTR("<tr><td>Boot stage at crash</td><td>%u</td></tr>"), (unsigned)s_lastStage);
  }
  o.p(PSTR("</table></div>"));

  // ---- network (design 7.4)
  o.f(PSTR("<div class='box'><b>Network</b><table><tr><td>Ladder</td><td>stage %u, attempts %u, down %u s, last reason %u</td></tr>"),
      (unsigned)netwatch::stage(), (unsigned)netwatch::attempts(), (unsigned)netwatch::downSeconds(), (unsigned)netwatch::lastReason());
  if (WiFi.status() == WL_CONNECTED) {
    getBssid(tmp, sizeof(tmp));
    o.f(PSTR("<tr><td>Signal</td><td>%d dBm, channel %d, BSSID %s</td></tr>"), (int)WiFi.RSSI(), (int)WiFi.channel(), tmp);
  }
  uint32_t ntp = netwatch::ntpAgeSeconds();
  if (ntp == 0xFFFFFFFFu) o.p(PSTR("<tr><td>Last time sync</td><td>never</td></tr>"));
  else o.f(PSTR("<tr><td>Last time sync</td><td>%u s ago</td></tr>"), (unsigned)ntp);
  o.f(PSTR("<tr><td>Reconnects</td><td>%u since power-on, WiFi restarts %u</td></tr></table><small>Events:<br>"),
      (unsigned)netwatch::reconnects(), (unsigned)s_r2.wifiRestarts);
  for (uint8_t i = 0; i < 8 && netwatch::eventLine(i, tmp, sizeof(tmp)); i++) { o.s(tmp); o.p(PSTR("<br>")); }
  o.p(PSTR("</small></div>"));

  // ---- health
  uint32_t up = (millis() - s_bootMs) / 1000;
  o.f(PSTR("<div class='box'><b>Health</b><table><tr><td>Free heap</td><td>%u bytes (lowest %u, largest block %u)</td></tr>"),
      (unsigned)ESP.getFreeHeap(), (unsigned)(s_hw.min_free == 0xFFFFFFFFu ? ESP.getFreeHeap() : s_hw.min_free), (unsigned)ESP.getMaxFreeBlockSize());
  o.f(PSTR("<tr><td>Uptime</td><td>%ud %02uh %02um</td></tr><tr><td>FPS</td><td>%u.%u</td></tr><tr><td>Longest loop</td><td>%u ms</td></tr></table></div>"),
      (unsigned)(up / 86400), (unsigned)(up / 3600 % 24), (unsigned)(up / 60 % 60), (unsigned)(s_fps10 / 10), (unsigned)(s_fps10 % 10), (unsigned)s_loopMax);

  {                                       // read-only probe of the sector a later release may save into (R2 never writes flash)
    char pt[128];
    prefs::probeText(pt, sizeof(pt));
    o.p(PSTR("<div class='box'><small>")); o.esc(pt); o.p(PSTR("</small></div>"));
  }
  {                                       // RECOVERY-CHANGE (R3-0F): second read-only probe (program area, 0xFF000) and the R3 hook lines
    char pt[176];
    prefs::spareText(pt, sizeof(pt));
    o.p(PSTR("<div class='box'><small>")); o.esc(pt); o.p(PSTR("</small></div>"));
    if (r3Line(0, pt, sizeof(pt))) {
      o.p(PSTR("<div class='box'><small>"));
      for (uint8_t i = 0; r3Line(i, pt, sizeof(pt)); i++) { if (i) o.p(PSTR("<br>")); o.esc(pt); }
      o.p(PSTR("</small></div>"));
    }
  }
  o.p(PSTR("<p><a href='/update'>Update firmware</a> <a href='/wifi'>WiFi setup</a>"));
#if GQ_HAVE_CONFIG_PAGE
  if (!s_safe) o.p(PSTR(" <a href='/config'>Clock settings</a>"));
#endif
  o.p(PSTR("</p>"));

  // ---- settings
  const GqSettings& st = prefs::settings();
  o.f(PSTR("<div class='box'><form method='post' action='/settings' style='display:block'><input type='hidden' name='f' value='1'>Brightness (0-100)<input name='bl' value='%d'>"), lcd::brightness());
  o.f(PSTR("Colour depth (0-100, higher = deeper colours, fixes washed-out look)<input name='depth' value='%d'>Time zone (POSIX TZ, e.g. &lt;-03&gt;3 for UTC-3)<input name='tz' value='"), s_depth);
  o.esc(s_tz);
  o.f(PSTR("'>Info card every (minutes, 0 = off)<input name='card' value='%u'>"), (unsigned)st.card_period);
  o.f(PSTR("<label><input type='checkbox' name='maint' value='1'%s> Weekly maintenance restart (about 04:00)</label><br>"
           "<label><input type='checkbox' name='night' value='1'%s> Quiet at night</label><br>"),
      (st.flags & GQS_FLAG_MAINT) ? " checked" : "", (st.flags & GQS_FLAG_NIGHT) ? " checked" : "");
  o.p(PSTR("<button>Apply</button> <small>Kept in the clock's memory (survives restarts and updates, not unplugging).</small></form>"
           "<form method='post' action='/reset_settings' onsubmit=\"return confirm('Put brightness, colour depth, time zone and card period back to defaults?')\"><button>Reset settings</button></form></div>"));
  o.p(PSTR("<div class='box'>Display test (only if colours look wrong; not saved):<br><br>"
           "<a href='/display?madctl=0'>RGB order</a> <a href='/display?madctl=8'>BGR order</a> "
           "<a href='/display?inv=1'>Inversion on</a> <a href='/display?inv=0'>Inversion off</a></div>"
           "<form method='post' action='/reboot'><button>Restart</button></form> "
           "<form method='post' action='/safemode'><button>Restart in safe mode</button></form>"));
  o.f(PSTR("<p><small>GQ-RECOVERY-REV %u</small></p>"), (unsigned)GQ_RECOVERY_REV);
  o.flush();
  endPage();
}

static void handleInfo() {
  touch();
  beginChunked(200, F("application/json"));
  Out o(server);
  char tmp[64];
  o.f(PSTR("{\"version\":\"%s\",\"safe_mode\":%s,\"crashes\":%u,\"slow\":%u"), GQ_VERSION, s_safe ? "true" : "false", (unsigned)s_crashes, (unsigned)s_r2.slow);
  {
    const rst_info* ri = ESP.getResetInfoPtr();
    if (ri && ri->reason == REASON_EXCEPTION_RST)
      o.f(PSTR(",\"exccause\":%u,\"epc1\":%u,\"excvaddr\":%u,\"stage\":%u"),
          (unsigned)ri->exccause, (unsigned)ri->epc1, (unsigned)ri->excvaddr, (unsigned)s_lastStage);
  }
  o.f(PSTR(",\"heap\":%u,\"heap_min\":%u,\"heap_block\":%u,\"sketch_bytes\":%u,\"free_ota_bytes\":%u,\"flash_real\":%u"),
      (unsigned)ESP.getFreeHeap(), (unsigned)(s_hw.min_free == 0xFFFFFFFFu ? ESP.getFreeHeap() : s_hw.min_free),
      (unsigned)ESP.getMaxFreeBlockSize(), (unsigned)ESP.getSketchSize(), (unsigned)freeOtaBytes(), (unsigned)ESP.getFlashChipRealSize());
  o.f(PSTR(",\"sta_ip\":\"%s\",\"ap\":%s,\"uptime_s\":%u"),
      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "", s_ap ? "true" : "false", (unsigned)(millis() / 1000));
  // RECOVERY-CHANGE (0B): WiFi ladder diagnostics and recovery revision
  o.f(PSTR(",\"net_stage\":%u,\"net_attempts\":%u,\"net_down_s\":%u,\"net_reason\":%u,\"recovery_rev\":%u"),
      (unsigned)netwatch::stage(), (unsigned)netwatch::attempts(), (unsigned)netwatch::downSeconds(), (unsigned)netwatch::lastReason(), (unsigned)GQ_RECOVERY_REV);
  // RECOVERY-CHANGE (chunk1): the same numbers as the status page, on one line
  getBssid(tmp, sizeof(tmp));
  o.f(PSTR(",\"rssi\":%d,\"channel\":%d,\"bssid\":\"%s\",\"ntp_age_s\":%d,\"reconnects\":%u,\"wifi_restarts\":%u"),
      WiFi.status() == WL_CONNECTED ? (int)WiFi.RSSI() : 0, (int)WiFi.channel(), tmp,
      netwatch::ntpAgeSeconds() == 0xFFFFFFFFu ? -1 : (int)netwatch::ntpAgeSeconds(), (unsigned)netwatch::reconnects(), (unsigned)s_r2.wifiRestarts);
  o.f(PSTR(",\"fps\":%u.%u,\"loop_max_ms\":%u,\"heap_warn\":%s,\"boots\":%u,\"restarts\":["),
      (unsigned)(s_fps10 / 10), (unsigned)(s_fps10 % 10), (unsigned)s_loopMax, s_heapWarn ? "true" : "false", (unsigned)s_r2.bootCount);
  sendReasons(o, true);
  o.f(PSTR("],\"auto_restart\":%d,\"strict_upload\":%d,\"save_sector\":\""), (int)GQ_AUTO_RESTART, (int)GQ_STRICT_UPLOAD);
  {
    char pt[128];
    prefs::probeText(pt, sizeof(pt));
    o.esc(pt);
  }
  {                                       // RECOVERY-CHANGE (R3-0F): spare sector probe and R3 lines (key only when a module has something to say)
    char pt[176];
    prefs::spareText(pt, sizeof(pt));
    o.p(PSTR("\",\"spare_sector\":\"")); o.esc(pt); o.p(PSTR("\""));
    if (r3Line(0, pt, sizeof(pt))) {
      o.p(PSTR(",\"r3\":\""));
      for (uint8_t i = 0; r3Line(i, pt, sizeof(pt)); i++) { if (i) o.p(PSTR(" | ")); o.esc(pt); }
      o.p(PSTR("\""));
    }
  }
  o.p(PSTR("}"));
  o.flush();
  endChunked();
}

static void handleUpdatePage() {
  touch();
  beginPage();
  server.sendContent_P(PAGE_UPDATE_A);
#if GQ_STRICT_UPLOAD
  server.sendContent_P(PAGE_UPDATE_FORCE);
#endif
  server.sendContent_P(PAGE_UPDATE_B);
  endPage();
}

#if GQ_MDNS
static void mdnsStart() {
  MDNS.begin("geekquest");
  MDNS.addService("http", "tcp", 80);
}
#else
static void mdnsStart() {}
#endif

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
    // RECOVERY-CHANGE (chunk1): what the page told us (checked at the end), and memory for the updater
    memset(&s_upRule, 0, sizeof(s_upRule));
    s_upRule.want_size = (uint32_t)strtoul(server.arg("size").c_str(), nullptr, 10);
    String c = server.arg("crc");
    s_upRule.have_crc = c.length() ? 1 : 0;
    s_upRule.want_crc = (uint32_t)strtoul(c.c_str(), nullptr, 16);
    s_upRule.force = server.arg("force") == "1" ? 1 : 0;
    s_upRule.stock_size = STOCK_SIZE;
    s_upRule.stock_crc = STOCK_CRC32;
    upchk_init(&s_upc, GQ_OTA_MARKER);
    s_upVerdict = -1;
    WiFiUDP::stopAll();
#if GQ_MDNS
    MDNS.end();
#endif
    s_freeAtUpload = ESP.getFreeHeap();
    Serial.print(F("update: start, free heap "));
    Serial.println(s_freeAtUpload);
    uint32_t maxSpace = freeOtaBytes();
    Update.clearError();
    if (!Update.begin(maxSpace, U_FLASH))
      uploadFail(String(F("cannot start (free heap ")) + s_freeAtUpload + F("): ") + Update.getErrorString());
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
    upchk_update(&s_upc, up.buf, (uint32_t)up.currentSize);   // RECOVERY-CHANGE (chunk1)
  } else if (up.status == UPLOAD_FILE_END) {
    if (s_upFailed) return;
    if (s_upBytes == 0) { uploadFail(F("empty file")); return; }
    // RECOVERY-CHANGE (chunk1): size, CRC and marker rule. Always computed and remembered; enforced only with GQ_STRICT_UPLOAD=1.
    s_upVerdict = upchk_verdict(&s_upc, &s_upRule);
    if (s_upVerdict != UV_OK) {
      Serial.print(F("update: check says: "));
      Serial.println(verdictText(s_upVerdict));
#if GQ_STRICT_UPLOAD
      uploadFail(String(verdictText(s_upVerdict)));
      return;
#endif
    }
    if (!Update.end(true)) uploadFail(String(F("verify failed: ")) + Update.getErrorString());
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    uploadFail(F("upload aborted"));
    s_updating = false;                        // nothing was committed: the game and the restart rules resume
  }
  yield();
}

static void handleUpdateDone() {
  if (s_upFailed || Update.hasError()) {
    s_updating = false;
    mdnsStart();                               // RECOVERY-CHANGE (chunk1): stopped when the upload began
    server.send(500, F("text/plain"), s_upError.length() ? s_upError : String(F("unknown error")));
    return;
  }
  server.sendHeader(F("Connection"), F("close"));
  char m[64];
  snprintf_P(m, sizeof(m), PSTR("Installed %u bytes (CRC32 %08x)."), (unsigned)s_upBytes, (unsigned)upchk_crc(&s_upc));
  server.send(200, F("text/plain"), m);
  requestRestart(RR_UPDATE);                   // RECOVERY-CHANGE (chunk1): was delay(500) + plannedRestart(false)
}

static void handleWifiPage() {
  touch();
  beginPage();
  server.sendContent_P(PAGE_WIFI);
  endPage();
}

static void handleWifiSave() {
  touch();
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  if (!ssid.length()) { server.send(400, F("text/plain"), F("Network name is empty")); return; }
  server.send(200, F("text/html"), F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><body style='font-family:sans-serif;padding:16px'>"
                                      "Connecting... Find the clock's new address on its screen, or keep using the rescue hotspot.<p><a href='/'>Back</a>"));
  delay(300);
  WiFi.persistent(true);
  WiFi.begin(ssid.c_str(), pass.c_str());
  WiFi.persistent(false);
  netwatch::setCredentials(ssid.c_str(), pass.c_str());   // RECOVERY-CHANGE (0B): RAM copy + attempt now
}

static int argInt(const char* name, int lo, int hi, int dflt) {
  if (!server.hasArg(name)) return dflt;
  long v = server.arg(name).toInt();
  return v < lo ? lo : (v > hi ? hi : (int)v);
}

static void handleSettings() {
  touch();
  // RECOVERY-CHANGE (chunk1): the whole form is validated first; an invalid value changes nothing.
  GqSettings s = prefs::settings();
  s.brightness = (uint8_t)argInt("bl", 0, 100, s.brightness);
  s.depth = (uint8_t)argInt("depth", 0, 100, s.depth);
  s.card_period = (uint8_t)argInt("card", 0, GQS_CARD_PERIOD_MAX, s.card_period);
  if (server.hasArg("f"))
    s.flags = (uint8_t)((server.hasArg("maint") ? GQS_FLAG_MAINT : 0) | (server.hasArg("night") ? GQS_FLAG_NIGHT : 0));
  if (server.hasArg("tz") && server.arg("tz").length()) {
    String tz = server.arg("tz");
    if (!gqs_tz_valid(tz.c_str())) {
      server.send(400, F("text/plain"), F("Time zone rejected: use 3 to 34 characters from A-Z a-z 0-9 < > + - : , . / (for example <-03>3). Nothing was changed."));
      return;
    }
    strncpy(s.tz, tz.c_str(), sizeof(s.tz) - 1);
    s.tz[sizeof(s.tz) - 1] = 0;
  }
  gqs_seal(&s);
  prefs::set(s);
  redirectHome();
}

static void handleDisplay() {
  touch();
  if (!s_noDisplay) {
    if (server.hasArg("madctl")) lcd::setMadctl((uint8_t)server.arg("madctl").toInt());
    if (server.hasArg("inv")) lcd::setInversion(server.arg("inv").toInt() != 0);
  }
  redirectHome();
}

static void handleResetSettings() { touch(); prefs::reset(); redirectHome(); }

static void handleNotFound() {
  netwatch::noteHttp();   // RECOVERY-CHANGE (0B)
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
  // RECOVERY-CHANGE (0B): WiFi.setAutoReconnect(true) removed; netwatch::begin() turns it off
  startAP();
  s_bootMs = millis();                 // RECOVERY-CHANGE (0B): time base for golden() and the stable marker
  heapwatch_init(&s_hw);               // RECOVERY-CHANGE (chunk1)
  netwatch::begin(s_safe);             // RECOVERY-CHANGE (0B): after startAP(), before the first WiFi.begin()

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
  // RECOVERY-CHANGE (chunk1): restarts go through requestRestart(); the reply is sent first, loop() restarts
  server.on("/reboot", HTTP_POST, []() { server.send(200, F("text/plain"), F("Restarting")); requestRestart(RR_USER); });
  server.on("/safemode", HTTP_POST, []() { server.send(200, F("text/plain"), F("Restarting into safe mode")); requestRestart(RR_SAFEMODE); });
  server.on("/reset_settings", HTTP_POST, handleResetSettings);
  server.onNotFound(handleNotFound);
  server.begin();

#if GQ_MDNS
  mdnsStart();
#endif

  settimeofday_cb([]() { s_timeSynced = true; netwatch::noteNtp(); });   // RECOVERY-CHANGE (0B): noteNtp
  configTime(s_tz, "pool.ntp.org", "time.google.com");
}

static void heapWatchdog() {
  uint32_t now = millis();
  if (now - s_lastHeapSample < HEAP_SAMPLE_MS) return;
  s_lastHeapSample = now;
  uint8_t r = heapwatch_step(&s_hw, now, ESP.getFreeHeap(), ESP.getMaxFreeBlockSize());
  s_heapWarn = (r & HW_WARN) != 0;
  if (r & HW_RESTART) {
    if (!s_heapReq && !s_updating) { s_heapReq = true; requestRestart(RR_HEAP); }
  } else {
    s_heapReq = false;
    if (s_pending == RR_HEAP) s_pending = RR_NONE;       // the condition went away: cancel a waiting restart
  }
}

static void maintenanceCheck() {
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last < 1000) return;
  last = now;
  MaintIn in;
  memset(&in, 0, sizeof(in));
  in.enabled = (prefs::settings().flags & GQS_FLAG_MAINT) ? 1 : 0;
  in.uptime_ms = now - s_bootMs;
  int h = 0, m = 0;
  in.clock_valid = localTime(h, m) ? 1 : 0;
  in.hour = (uint8_t)h;
  in.minute = (uint8_t)m;
  in.hero_asleep = (s_asleepFn && s_asleepFn()) ? 1 : 0;
  in.ap_clients = (uint8_t)WiFi.softAPgetStationNum();
  in.updating = s_updating ? 1 : 0;
  in.since_http_ms = msSinceRequest();
  if (maint_due(&in)) {
    if (!s_maintReq && !s_updating) { s_maintReq = true; requestRestart(RR_MAINT); }
  } else {
    s_maintReq = false;
    if (s_pending == RR_MAINT) s_pending = RR_NONE;
  }
}

void loop() {
  server.handleClient();
  if (s_ap) dns.processNextRequest();
#if GQ_MDNS
  MDNS.update();
#endif
  // RECOVERY-CHANGE (0B): wifiPolicy() replaced by the ladder; absolute millis() comparisons replaced by latched flags
  if (s_golden && millis() - s_bootMs >= AP_GRACE_MS) s_golden = false;
  netwatch::loop();
  if (!s_stableMarked && millis() - s_bootMs >= STABLE_AFTER_MS) {
    s_stableMarked = true;
    markStable();
  }
  // RECOVERY-CHANGE (chunk1): slow-crash counter, heap watchdog, weekly restart, pending restarts
  if (!s_slowCleared && millis() - s_bootMs >= SLOW_STABLE_MS) {
    s_slowCleared = true;
    s_r2.slow = 0;
    r2Write();
  }
  heapWatchdog();
  maintenanceCheck();
  processRestart();
}

// RECOVERY-CHANGE (0B): golden()
bool golden() { return s_golden; }
int depth() { return s_depth; }
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
