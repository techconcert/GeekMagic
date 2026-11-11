// See netwatch.h and DESIGN.md section 7. The decisions live in netfsm.cpp
// (host-tested); this file only reads inputs and performs the actions.
#include "netwatch.h"
#include "safety.h"
#include "netfsm.h"

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <user_interface.h>

// Approval switches. All default OFF (design 12.2 A2 and A3).
//   -DGQ_NET_NO_SLEEP          WiFi modem sleep off (about 0.25 W more power)
//   -DGQ_AUTO_RESTART=1        NA_RESTART really restarts (otherwise safety::requestRestart only logs it)
// RECOVERY-CHANGE (chunk1): GQ_NET_PLANNED_RESTART is replaced by GQ_AUTO_RESTART, decided in safety.cpp
#ifndef GQ_AUTO_RESTART
#define GQ_AUTO_RESTART 0
#endif

namespace netwatch {

static char s_ssid[33];
static char s_pass[65];
static NetFsm s_fsm;
static bool s_started = false;
static bool s_safe = false;
static bool s_staOnPending = false;     // enableSTA(true) due on the next step
static bool s_everConnected = false;    // since power-on (RTC-backed in chunk 1)
static bool s_down = false;
static uint32_t s_lastTick = 0;
static uint32_t s_downSince = 0;
static uint32_t s_lastHttp = 0;         // 0 = never
static uint32_t s_lastNtp = 0;          // 0 = never
static uint8_t s_lastReason = 0;
static uint32_t s_reconnects = 0;
// RECOVERY-CHANGE (chunk1): link-good tracking and the network event log in RTC blocks 112..127
static uint32_t s_upSince = 0;
static bool s_linkGoodSent = false;
static uint8_t s_prevStage = 0xFF;
static uint8_t s_evSeq = 0;
static const uint32_t EV_OFFSET = 112;
static const uint8_t EV_SLOTS = 8;
enum { EV_UP = 1, EV_DOWN, EV_STAGE, EV_RESTART_REQ, EV_SNTP_KICK, EV_AP_ON, EV_AP_OFF };

struct EvRec { uint32_t stamp; uint8_t kind, reason, seq, chk; };
static uint8_t evChk(const EvRec& e) {
  return (uint8_t)(e.stamp ^ (e.stamp >> 8) ^ (e.stamp >> 16) ^ (e.stamp >> 24) ^ e.kind ^ e.reason ^ e.seq ^ 0xA5);
}
static bool evRead(uint8_t slot, EvRec& e) {
  uint32_t w[2];
  ESP.rtcUserMemoryRead(EV_OFFSET + slot * 2, w, sizeof(w));
  e.stamp = w[0];
  e.kind = (uint8_t)w[1];
  e.reason = (uint8_t)(w[1] >> 8);
  e.seq = (uint8_t)(w[1] >> 16);
  e.chk = (uint8_t)(w[1] >> 24);
  return e.kind != 0 && e.chk == evChk(e);
}
static void evAdd(uint8_t kind, uint8_t reason) {
  EvRec e;
  time_t t = time(nullptr);
  e.stamp = t > 1600000000 ? (uint32_t)t : (0x80000000u | (millis() / 1000));
  e.kind = kind;
  e.reason = reason;
  e.seq = s_evSeq;
  e.chk = evChk(e);
  uint32_t w[2] = {e.stamp, (uint32_t)e.kind | ((uint32_t)e.reason << 8) | ((uint32_t)e.seq << 16) | ((uint32_t)e.chk << 24)};
  ESP.rtcUserMemoryWrite(EV_OFFSET + (s_evSeq & (EV_SLOTS - 1)) * 2, w, sizeof(w));
  s_evSeq++;
}
static void evInit(bool cold) {
  if (cold) {
    uint32_t z[16] = {0};
    ESP.rtcUserMemoryWrite(EV_OFFSET, z, sizeof(z));
    s_evSeq = 0;
    return;
  }
  bool have[256] = {false};
  bool any = false;
  EvRec e;
  for (uint8_t i = 0; i < EV_SLOTS; i++) if (evRead(i, e)) { have[e.seq] = true; any = true; }
  s_evSeq = 0;
  if (!any) return;
  for (uint16_t q = 0; q < 256; q++)           // newest = valid entry whose successor is not present
    if (have[q] && !have[(q + 1) & 0xFF]) { s_evSeq = (uint8_t)(q + 1); break; }
}

// Handlers run in the SDK's context: set flags only.
static WiFiEventHandler s_hDisc, s_hGot;
static volatile bool s_discFlag = false, s_gotFlag = false;
static volatile uint8_t s_discReason = 0;

static bool linkUp() {
  return WiFi.status() == WL_CONNECTED && WiFi.localIP()[0] != 0;
}

void begin(bool safeMode) {
  s_safe = safeMode;

  station_config conf;
  memset(&conf, 0, sizeof(conf));
  wifi_station_get_config_default(&conf);
  memcpy(s_ssid, conf.ssid, 32);
  s_ssid[32] = 0;
  memcpy(s_pass, conf.password, 64);
  s_pass[64] = 0;

  // Our own ladder replaces the SDK's auto-reconnect (design 7.1.5).
  WiFi.setAutoReconnect(false);
#ifdef GQ_NET_NO_SLEEP
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif

  s_hDisc = WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected& e) {
    s_discReason = (uint8_t)e.reason;
    s_discFlag = true;
  });
  s_hGot = WiFi.onStationModeGotIP([](const WiFiEventStationModeGotIP&) { s_gotFlag = true; });

  evInit(safety::coldBoot());             // RECOVERY-CHANGE (chunk1)
  uint32_t now = millis();
  netfsm_init(&s_fsm, now, now ^ ESP.getChipId() ^ ESP.getCycleCount());
  s_lastTick = now;
  s_downSince = now;
  s_down = true;
  s_started = true;
}

void setCredentials(const char* ssid, const char* pass) {
  if (!ssid) return;
  strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
  s_ssid[sizeof(s_ssid) - 1] = 0;
  strncpy(s_pass, pass ? pass : "", sizeof(s_pass) - 1);
  s_pass[sizeof(s_pass) - 1] = 0;
  if (s_started) netfsm_kick(&s_fsm);
}

static void doActions(uint8_t a) {
  if (a & NA_BEGIN) WiFi.begin(s_ssid, s_pass[0] ? s_pass : nullptr);
  if (a & NA_STA_STOP) wifi_station_disconnect();
  if (a & NA_STA_CYCLE) {
    WiFi.enableSTA(false);
    s_staOnPending = true;
  }
  if (a & NA_AP_ON) { safety::startAP(); evAdd(EV_AP_ON, 0); }
  if (a & NA_AP_OFF) { safety::stopAP(); evAdd(EV_AP_OFF, 0); }
  if (a & NA_MDNS_RESTART) {
    MDNS.end();
    MDNS.begin("geekquest");
    MDNS.addService("http", "tcp", 80);
  }
  if (a & NA_SNTP_KICK) {
    const char* tz = getenv("TZ");   // safety.cpp's configTime() set it
    configTime(tz && *tz ? tz : "UTC0", "pool.ntp.org", "time.google.com");
    evAdd(EV_SNTP_KICK, 0);              // RECOVERY-CHANGE (chunk1)
  }
  if (a & NA_RESTART) {
    // RECOVERY-CHANGE (chunk1): safety decides (gate, GQ_AUTO_RESTART); without the flag it is only logged
    evAdd(EV_RESTART_REQ, 0);
    safety::requestRestart(safety::RR_WIFI);
  }
}

void loop() {
  if (!s_started) return;
  uint32_t now = millis();
  if (now - s_lastTick < 1000) return;
  s_lastTick = now;

  if (s_staOnPending) {
    s_staOnPending = false;
    WiFi.enableSTA(true);
  }
  if (s_discFlag) {
    s_discFlag = false;
    s_lastReason = s_discReason;
    evAdd(EV_DOWN, s_discReason);        // RECOVERY-CHANGE (chunk1)
  }
  if (s_gotFlag) {
    s_gotFlag = false;
    s_reconnects++;
    evAdd(EV_UP, 0);                     // RECOVERY-CHANGE (chunk1)
  }

  NetIn in;
  in.now_ms = now;
  in.sta_up = linkUp() ? 1 : 0;
  in.have_creds = s_ssid[0] ? 1 : 0;
  in.ap_clients = (uint8_t)WiFi.softAPgetStationNum();
  in.updating = safety::updating() ? 1 : 0;
  in.safe_mode = s_safe ? 1 : 0;
  if (in.sta_up) s_everConnected = true;
  // RECOVERY-CHANGE (chunk1): both live in RTC now, so they survive the restart the ladder asks for
  if (in.sta_up) {
    safety::noteConnected();
    if (!s_linkGoodSent && s_upSince && now - s_upSince >= 120000UL) { s_linkGoodSent = true; safety::noteLinkGood(); }
  }
  in.ever_connected = safety::everConnected() ? 1 : 0;
  in.restarts_done = safety::wifiRestarts();
  in.last_http_ms = s_lastHttp;
  in.last_ntp_ms = s_lastNtp;

  if (in.sta_up) {
    s_down = false;
    if (!s_upSince) s_upSince = now ? now : 1;
  } else {
    s_upSince = 0;
    s_linkGoodSent = false;
    if (!s_down) { s_down = true; s_downSince = now; }
  }

  doActions(netfsm_step(&s_fsm, &in));
  uint8_t st = netfsm_stage(&s_fsm);       // RECOVERY-CHANGE (chunk1): log ladder stage changes
  if (st != s_prevStage) {
    if (s_prevStage != 0xFF) evAdd(EV_STAGE, st);
    s_prevStage = st;
  }
}

uint8_t stage() {
  uint8_t s = netfsm_stage(&s_fsm);
#if !GQ_AUTO_RESTART                    // RECOVERY-CHANGE (chunk1)
  if (s == NS_RESTART) s = NS_CYCLE;    // no restart will follow in this build
#endif
  return s;
}
uint32_t attempts() { return netfsm_attempts(&s_fsm); }
uint32_t downSeconds() { return s_down ? (millis() - s_downSince) / 1000 : 0; }
uint8_t lastReason() { return s_lastReason; }

// RECOVERY-CHANGE (chunk1): diagnostics for the status page
uint32_t reconnects() { return s_reconnects; }
uint32_t ntpAgeSeconds() { return s_lastNtp ? (millis() - s_lastNtp) / 1000 : 0xFFFFFFFFu; }
bool eventLine(uint8_t i, char* buf, size_t n) {
  if (i >= EV_SLOTS) return false;
  uint8_t seq = (uint8_t)(s_evSeq - 1 - i);
  EvRec e;
  if (!evRead(seq & (EV_SLOTS - 1), e) || e.seq != seq) return false;
  char when[24];
  if (e.stamp & 0x80000000u) snprintf(when, sizeof(when), "boot+%us", (unsigned)(e.stamp & 0x7FFFFFFFu));
  else {
    time_t t = (time_t)e.stamp;
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(when, sizeof(when), "%02d-%02d %02d:%02d", tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
  }
  switch (e.kind) {
    case EV_UP: snprintf(buf, n, "%s link up", when); break;
    case EV_DOWN: snprintf(buf, n, "%s link down, SDK reason %u", when, (unsigned)e.reason); break;
    case EV_STAGE: snprintf(buf, n, "%s ladder stage %u", when, (unsigned)e.reason); break;
    case EV_RESTART_REQ: snprintf(buf, n, "%s restart requested", when); break;
    case EV_SNTP_KICK: snprintf(buf, n, "%s time sync restarted", when); break;
    case EV_AP_ON: snprintf(buf, n, "%s hotspot on", when); break;
    case EV_AP_OFF: snprintf(buf, n, "%s hotspot off", when); break;
    default: snprintf(buf, n, "%s event %u", when, (unsigned)e.kind); break;
  }
  return true;
}

void noteNtp() {
  uint32_t n = millis();
  s_lastNtp = n ? n : 1;
}
void noteHttp() {
  uint32_t n = millis();
  s_lastHttp = n ? n : 1;
}

}  // namespace netwatch
