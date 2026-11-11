// WiFi reconnect state machine (design section 7.2). Pure C++: no Arduino
// includes, no heap, no libc calls, no floating point. Stepped once per second
// by firmware/netwatch.cpp, which carries out the returned action bits.
//
// Time rule: every comparison is `now - since >= period` in uint32_t, so the
// machine behaves across the 49.7 day millis() wrap.
#pragma once
#include <stdint.h>

typedef struct {
  uint32_t now_ms;          // millis()
  uint8_t  sta_up;          // WiFi.status()==WL_CONNECTED and IP != 0
  uint8_t  have_creds;      // s_ssid[0] != 0
  uint8_t  ap_clients;      // WiFi.softAPgetStationNum()
  uint8_t  updating;        // upload in progress
  uint8_t  safe_mode;
  uint8_t  ever_connected;  // since power-on (kept in RTC)
  uint8_t  restarts_done;   // WiFi-caused restarts since the last good link (RTC)
  uint32_t last_http_ms;    // last request served (0 = never)
  uint32_t last_ntp_ms;     // last successful NTP sync (0 = never)
} NetIn;

enum {                      // action bits
  NA_BEGIN        = 1,      // WiFi.begin(s_ssid, s_pass)
  NA_STA_STOP     = 2,      // wifi_station_disconnect(): stop scanning, keep config
  NA_STA_CYCLE    = 4,      // WiFi.enableSTA(false); then true on the next step
  NA_AP_ON        = 8,
  NA_AP_OFF       = 16,
  NA_MDNS_RESTART = 32,
  NA_SNTP_KICK    = 64,     // call configTime() again
  NA_RESTART      = 128     // ask safety to restart (logged only unless approved)
};

enum {                      // netfsm_stage() values
  NS_ONLINE = 0, NS_RETRY = 1, NS_CYCLE = 2, NS_RESTART = 3, NS_NO_CREDS = 4
};

typedef struct NetFsm {
  uint32_t rng;
  uint32_t start;                    // init time (golden window and uptime)
  uint32_t up_since, down_since;
  uint32_t begin_ref, begin_delay;   // next ladder NA_BEGIN is due when now-ref >= delay
  uint32_t last_begin;
  uint32_t stop_ref;
  uint32_t cycle_ref, cycle_delay;
  uint32_t zombie_ref, last_zombie;  // zombie_ref: NA_BEGIN due 2 s after the stop
  uint32_t attempts;                 // NA_BEGIN count since the link was lost
  uint8_t  idx;                      // position in the retry delay table
  uint8_t  stage;
  uint8_t  primed, prev_up, golden, ap_on, begun, stop_armed;
  uint8_t  zombie_pending, zombie_done, restart_sent, kick, mdns_pending;
} NetFsm;

void     netfsm_init(NetFsm* f, uint32_t now_ms, uint32_t seed);
uint8_t  netfsm_step(NetFsm* f, const NetIn* in);   // call once per second
void     netfsm_kick(NetFsm* f);                    // user saved new credentials: attempt now
uint8_t  netfsm_stage(const NetFsm* f);
uint32_t netfsm_attempts(const NetFsm* f);
