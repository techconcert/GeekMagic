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

// Approval switches. Both default OFF (design 12.2 A2 and A3).
//   -DGQ_NET_NO_SLEEP          WiFi modem sleep off (about 0.25 W more power)
//   -DGQ_NET_PLANNED_RESTART   NA_RESTART really restarts (otherwise it is only logged)

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
  if (a & NA_AP_ON) safety::startAP();
  if (a & NA_AP_OFF) safety::stopAP();
  if (a & NA_MDNS_RESTART) {
    MDNS.end();
    MDNS.begin("geekquest");
    MDNS.addService("http", "tcp", 80);
  }
  if (a & NA_SNTP_KICK) {
    const char* tz = getenv("TZ");   // safety.cpp's configTime() set it
    configTime(tz && *tz ? tz : "UTC0", "pool.ntp.org", "time.google.com");
  }
  if (a & NA_RESTART) {
#ifdef GQ_NET_PLANNED_RESTART
    Serial.println(F("netwatch: WiFi down too long, restarting"));
    safety::plannedRestart(false);
#else
    Serial.println(F("netwatch: restart requested (not enabled in this build)"));
#endif
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
  }
  if (s_gotFlag) {
    s_gotFlag = false;
    s_reconnects++;
  }

  NetIn in;
  in.now_ms = now;
  in.sta_up = linkUp() ? 1 : 0;
  in.have_creds = s_ssid[0] ? 1 : 0;
  in.ap_clients = (uint8_t)WiFi.softAPgetStationNum();
  in.updating = safety::updating() ? 1 : 0;
  in.safe_mode = s_safe ? 1 : 0;
  if (in.sta_up) s_everConnected = true;
  in.ever_connected = s_everConnected ? 1 : 0;
  in.restarts_done = 0;                 // chunk 1 keeps this in RTC
  in.last_http_ms = s_lastHttp;
  in.last_ntp_ms = s_lastNtp;

  if (in.sta_up) s_down = false;
  else if (!s_down) { s_down = true; s_downSince = now; }

  doActions(netfsm_step(&s_fsm, &in));
}

uint8_t stage() {
  uint8_t s = netfsm_stage(&s_fsm);
#ifndef GQ_NET_PLANNED_RESTART
  if (s == NS_RESTART) s = NS_CYCLE;    // no restart will follow in this build
#endif
  return s;
}
uint32_t attempts() { return netfsm_attempts(&s_fsm); }
uint32_t downSeconds() { return s_down ? (millis() - s_downSince) / 1000 : 0; }
uint8_t lastReason() { return s_lastReason; }

void noteNtp() {
  uint32_t n = millis();
  s_lastNtp = n ? n : 1;
}
void noteHttp() {
  uint32_t n = millis();
  s_lastHttp = n ? n : 1;
}

}  // namespace netwatch
