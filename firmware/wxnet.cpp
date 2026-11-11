// R3 live weather client on raw lwIP (DESIGN-R3.md 2.6). Thin glue: every decision lives in the host-tested pure code
// (wxfsm.cpp: when to fetch and when to give up; wxparse.cpp: what the reply says). This is the only file that may use
// tcp_*, dns_gethostbyname or pbuf_* (lint L11), and it has no WiFi control, no delay() and no blocking wait.
//
// Binding rules (DESIGN-R3.md 2.6), each one is ticked in the review checklist of the W1 hand-off note:
//  - Callbacks only feed the parser and set flags. They never close or abort, never allocate, never print.
//  - onErr: lwIP has already freed the connection: forget the pointer, set the error flag, nothing else.
//  - Closing happens only in finish()/closeConn(), called from loop(): callbacks cleared first, tcp_close, tcp_abort
//    when close fails or when the fetch is aborted.
//  - DNS cannot be cancelled: every fetch has a generation number; a callback carrying an old one is ignored.
//  - An upload (safety::updating()) aborts the connection at once, on every loop() iteration.
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <lwip/err.h>
#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/dns.h>

#include "wxnet.h"
#include "safety.h"
#include "config.h"
#include "nightmode.h"
#include "wxparse.h"
#include "wxfsm.h"
#include "gamecore.h"      // GC_WXF_* bits of the reading

namespace {

enum Phase : uint8_t { P_IDLE = 0, P_DNS, P_CONNECT, P_WAIT };

// Why the last fetch failed (status line). 0..5 are the WXP_* codes of the parser, 20.. are network failures.
enum NetErr : uint8_t {
  E_NONE = 0,
  E_DNS = 20, E_CONNECT = 21, E_RESET = 22, E_TIMEOUT = 23, E_MEM = 24, E_REQUEST = 25, E_UPDATE = 26
};

const char     HOST[]  = "api.open-meteo.com";
const uint16_t PORT    = 80;
const uint32_t STEP_MS = 1000;

bool          s_on = false;               // begin() ran and this is not safe mode
bool          s_stepped = false;
uint32_t      s_lastStep = 0;
uint32_t      s_linkSince = 0;            // millis() when the station link was last seen coming up
bool          s_linkWas = false;

WxFsm         s_fsm;
WxParser      s_parser;
char          s_req[200];                 // the request: static, and not touched while a fetch runs (tcp_write does not copy)
int           s_reqLen = 0;

tcp_pcb*      s_pcb = nullptr;
ip_addr_t     s_ip;
uint16_t      s_gen = 0;                  // generation of the current fetch, never 0 while a fetch exists

// Written by the callbacks, read by loop(). The ESP8266 runs lwIP and loop() in turn, never at the same instant.
volatile uint8_t s_phase = P_IDLE;
volatile uint8_t s_err = E_NONE;          // a callback saw a failure
volatile uint8_t s_end = 0;               // the peer closed the connection
volatile uint8_t s_dnsDone = 0;
volatile uint8_t s_dnsOk = 0;

// The newest good reading and the settings it was fetched for.
bool          s_have = false;
WxReading     s_rd;
int16_t       s_rdLat = 0, s_rdLon = 0;
int16_t       s_reqLat = 0, s_reqLon = 0;

uint8_t       s_lastErr = E_NONE;
uint16_t      s_okCount = 0, s_failCount = 0;
uint32_t      s_minHeap = 0xFFFFFFFFu;    // lowest free heap seen while a fetch was running

inline uint16_t genOf(void* arg) { return (uint16_t)(uintptr_t)arg; }

// ------------------------------------------------------------------ callbacks (system context, little stack)

void onDns(const char* name, const ip_addr_t* ip, void* arg) {
  (void)name;
  if (genOf(arg) != s_gen || s_phase != P_DNS) return;            // an old fetch's answer, or none expected
  if (ip != nullptr && !ip_addr_isany(ip)) {
    ip_addr_copy(s_ip, *ip);
    s_dnsOk = 1;
  } else {
    s_dnsOk = 0;
  }
  s_dnsDone = 1;
}

err_t onConnected(void* arg, tcp_pcb* pcb, err_t err) {
  if (genOf(arg) != s_gen || s_phase != P_CONNECT || pcb != s_pcb) return ERR_OK;
  if (err != ERR_OK) { s_err = E_CONNECT; return ERR_OK; }
  err_t e = tcp_write(pcb, s_req, (u16_t)s_reqLen, 0);
  if (e == ERR_OK) e = tcp_output(pcb);
  if (e != ERR_OK) { s_err = E_MEM; return ERR_OK; }              // loop() closes it
  s_phase = P_WAIT;
  return ERR_OK;
}

err_t onRecv(void* arg, tcp_pcb* pcb, pbuf* p, err_t err) {
  if (p == nullptr) {                                              // the peer closed the connection
    if (genOf(arg) == s_gen && s_phase == P_WAIT) s_end = 1;
    return ERR_OK;
  }
  const u16_t total = p->tot_len;
  if (genOf(arg) == s_gen && s_phase == P_WAIT && err == ERR_OK) {
    for (pbuf* q = p; q != nullptr; q = q->next)                   // the chain is finite and tot_len long
      wxp_feed(&s_parser, (const uint8_t*)q->payload, (uint16_t)q->len);
  } else if (err != ERR_OK && genOf(arg) == s_gen && s_phase == P_WAIT) {
    s_err = E_RESET;
  }
  tcp_recved(pcb, total);
  pbuf_free(p);
  return ERR_OK;
}

void onErr(void* arg, err_t err) {
  (void)err;
  if (genOf(arg) != s_gen || s_phase == P_IDLE) return;
  s_pcb = nullptr;                                                 // lwIP has already freed it
  s_err = (s_phase == P_WAIT) ? E_RESET : E_CONNECT;
}

// ------------------------------------------------------------------ connection control (called from loop() only)

void closeConn(bool abortNow) {
  tcp_pcb* pcb = s_pcb;
  s_pcb = nullptr;
  if (pcb == nullptr) return;
  tcp_arg(pcb, nullptr);                                           // no callback of ours can run on it any more
  tcp_recv(pcb, nullptr);
  tcp_err(pcb, nullptr);
  tcp_sent(pcb, nullptr);
  tcp_poll(pcb, nullptr, 0);
  if (abortNow) tcp_abort(pcb);
  else if (tcp_close(pcb) != ERR_OK) tcp_abort(pcb);
}

void bumpGen() {
  s_gen++;
  if (s_gen == 0) s_gen = 1;
}

// A fetch is over: tear down, tell the schedule, clear the crash marker.
void finish(bool ok, uint8_t code, bool abortNow) {
  closeConn(abortNow);
  s_phase = P_IDLE;
  s_err = E_NONE; s_end = 0; s_dnsDone = 0; s_dnsOk = 0;
  bumpGen();                                                       // a late DNS answer finds a different generation
  s_lastErr = code;
  if (ok) { if (s_okCount < 65535) s_okCount++; }
  else    { if (s_failCount < 65535) s_failCount++; }
  wxfsm_result(&s_fsm, millis(), ok ? 1 : 0);
  safety::wxMark(false);
}

void connectNow() {
  if (ip_addr_isany(&s_ip)) { finish(false, E_DNS, true); return; }
  tcp_pcb* pcb = tcp_new();
  if (pcb == nullptr) { finish(false, E_MEM, true); return; }
  s_pcb = pcb;
  s_phase = P_CONNECT;
  tcp_arg(pcb, (void*)(uintptr_t)s_gen);
  tcp_err(pcb, onErr);
  tcp_recv(pcb, onRecv);
  if (tcp_connect(pcb, &s_ip, PORT, onConnected) != ERR_OK) finish(false, E_CONNECT, true);
}

void startFetch() {
  const GqConf& c = config::get();
  s_reqLat = c.lat_c;
  s_reqLon = c.lon_c;
  s_reqLen = wx_build_request(s_req, (int)sizeof s_req, c.lat_c, c.lon_c);
  if (s_reqLen <= 0) {                                             // coordinates out of range: nothing to send
    s_lastErr = E_REQUEST;
    if (s_failCount < 65535) s_failCount++;
    wxfsm_result(&s_fsm, millis(), 0);
    return;
  }
  safety::wxMark(true);                                            // a crash from here to finish() earns a strike
  wxp_init(&s_parser);
  s_err = E_NONE; s_end = 0; s_dnsDone = 0; s_dnsOk = 0;
  bumpGen();
  s_phase = P_DNS;
  s_minHeap = ESP.getFreeHeap();
  err_t e = dns_gethostbyname(HOST, &s_ip, onDns, (void*)(uintptr_t)s_gen);
  if (e == ERR_OK) connectNow();                                   // cached answer: no callback will come
  else if (e != ERR_INPROGRESS) finish(false, E_DNS, true);
}

void complete() {
  WxReading r;
  int rc = wxp_finish(&s_parser, &r);
  if (rc == WXP_OK) {
    s_rd = r;
    s_rdLat = s_reqLat;
    s_rdLon = s_reqLon;
    s_have = true;
  }
  finish(rc == WXP_OK, (uint8_t)rc, false);
}

// Every loop() iteration while a fetch is running (cheap): upload check, callback flags, heap bookkeeping.
void service() {
  uint32_t heap = ESP.getFreeHeap();
  if (heap < s_minHeap) s_minHeap = heap;
  if (safety::updating()) { finish(false, E_UPDATE, true); return; }
  switch (s_phase) {
    case P_DNS:
      if (s_dnsDone) {
        if (s_dnsOk) connectNow();
        else finish(false, E_DNS, true);
      }
      break;
    case P_CONNECT:
      if (s_err) finish(false, s_err, true);
      break;
    case P_WAIT:
      if (s_end || wxp_settled(&s_parser)) complete();              // reply finished, or its outcome is already certain
      else if (s_err) finish(false, s_err, true);
      break;
    default:
      break;
  }
}

}  // namespace

namespace wxnet {

void begin(bool safeMode) {
  s_on = !safeMode;
  if (!s_on) return;
  uint32_t seed = (uint32_t)ESP.getChipId() ^ ((uint32_t)micros() * 2654435761u);
  wxfsm_init(&s_fsm, millis(), seed);
}

void loop() {
  if (!s_on) return;
  if (s_phase != P_IDLE) service();
  const uint32_t now = millis();
  if (s_stepped && now - s_lastStep < STEP_MS) return;
  s_stepped = true;
  s_lastStep = now;

  const bool up = safety::staConnected();
  if (up && !s_linkWas) s_linkSince = now;
  s_linkWas = up;

  WxIn in;
  in.now_ms = now;
  in.uptime_ms = now;
  in.free_heap = ESP.getFreeHeap();
  in.max_block = ESP.getMaxFreeBlockSize();
  uint32_t upS = up ? (now - s_linkSince) / 1000 : 0;
  in.sta_up_s = (uint8_t)(upS > 255 ? 255 : upS);
  in.configured = (config::get().flags & GQC_F_WEATHER) ? 1 : 0;
  in.updating = safety::updating() ? 1 : 0;
  in.safe_mode = safety::safeMode() ? 1 : 0;
  in.screen_off = nightmode::screenOff() ? 1 : 0;
  in.wake_soon = nightmode::wakeSoon() ? 1 : 0;
  in.http_recent = (safety::msSinceRequest() < 2000) ? 1 : 0;
  in.strikes = safety::wxStrikes();

  uint8_t act = wxfsm_step(&s_fsm, &in);
  if (act == WXA_START) startFetch();
  else if (act == WXA_ABORT) finish(false, in.updating ? E_UPDATE : E_TIMEOUT, true);
}

bool reading(int* wx, int* temp_c10, int* flags) {
  if (!s_on || !s_have || wx == nullptr || temp_c10 == nullptr || flags == nullptr) return false;
  const GqConf& c = config::get();
  if (!(c.flags & GQC_F_WEATHER)) return false;
  if (c.lat_c != s_rdLat || c.lon_c != s_rdLon) return false;      // the place changed since this was fetched
  const uint8_t age = wxfsm_age_class(&s_fsm, millis());
  if (age != 1 && age != 2) return false;                          // none or expired: the game shows its own weather
  *wx = wx_state(&s_rd);
  *temp_c10 = s_rd.temp_c10;
  int fl = 0;
  if (c.flags & GQC_F_FAHRENHEIT) fl |= GC_WXF_FAHRENHEIT;
  if (!s_rd.is_day) fl |= GC_WXF_NIGHT;
  if (age == 2) fl |= GC_WXF_STALE;
  *flags = fl;
  return true;
}

void kick() {
  if (s_on) wxfsm_kick(&s_fsm);
}

static const char* errName(uint8_t c) {
  switch (c) {
    case E_NONE:          return "none";
    case WXP_HTTP_STATUS: return "bad HTTP status";
    case WXP_NO_BODY:     return "no body";
    case WXP_MISSING:     return "reply incomplete";
    case WXP_RANGE:       return "value out of range";
    case WXP_TOO_BIG:     return "reply too big";
    case E_DNS:           return "DNS failed";
    case E_CONNECT:       return "connect failed";
    case E_RESET:         return "connection reset";
    case E_TIMEOUT:       return "timeout";
    case E_MEM:           return "out of memory";
    case E_REQUEST:       return "bad coordinates";
    case E_UPDATE:        return "upload running";
    default:              return "error";
  }
}

void statusLine(char* buf, size_t n) {
  if (buf == nullptr || n == 0) return;
  buf[0] = 0;
  if (!s_on || !(config::get().flags & GQC_F_WEATHER)) return;
  char t[48];
  const uint32_t ageMs = wxfsm_age_ms(&s_fsm, millis());          // 0xFFFFFFFF once the reading expired
  if (s_have && ageMs != 0xFFFFFFFFu) {
    int v = s_rd.temp_c10;
    const char* sign = v < 0 ? "-" : "";
    if (v < 0) v = -v;
    snprintf(t, sizeof t, "%s%d.%d C, %u min old", sign, v / 10, v % 10, (unsigned)(ageMs / 60000));
  } else if (s_have) {
    snprintf(t, sizeof t, "last reading expired");
  } else {
    snprintf(t, sizeof t, "no reading yet");
  }
  uint32_t heap = (s_minHeap == 0xFFFFFFFFu) ? 0 : s_minHeap;
  snprintf(buf, n, "%s; fetches ok %u, failed %u; last error: %s; lowest heap in a fetch %u", t,
           (unsigned)s_okCount, (unsigned)s_failCount, errName(s_lastErr), (unsigned)heap);
}

}  // namespace wxnet
