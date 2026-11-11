// Live weather schedule (docs/DESIGN-R3.md 2.5): when to fetch, when to give up, how old the data is.
// Pure C++: no Arduino includes, no heap, no libc calls, no floating point. Stepped once per second by firmware/wxnet.cpp,
// which carries out the returned action.
//
// Time rule: every comparison is `now - since >= period` in uint32_t, so the machine behaves across the 49.7 day wrap.
#pragma once
#include <stdint.h>

typedef struct {
  uint32_t now_ms, uptime_ms, free_heap, max_block;
  uint8_t  sta_up_s;      // seconds the station link has been up, saturating at 255
  uint8_t  configured, updating, safe_mode, screen_off, wake_soon, http_recent, strikes;
} WxIn;

enum { WXA_NONE = 0, WXA_START = 1, WXA_ABORT = 2 };

typedef struct WxFsm {
  uint32_t rng;
  uint32_t ref;            // the next fetch is due when now - ref >= delay
  uint32_t delay;
  uint32_t fetch_start;    // start of the running fetch
  uint32_t ok_ms;          // time of the last good reading
  uint8_t  running;
  uint8_t  fails;          // failures in a row (position in the retry ladder)
  uint8_t  have_ok;        // a good reading exists and has not expired
  uint8_t  expired;        // latched by wxfsm_step when the data got older than 125 minutes
  uint8_t  kick;           // wxfsm_kick() called, not yet handled
  uint8_t  rearm;          // a kick arrived during a fetch: the fetch is aborted and the next one is due in 2 s
} WxFsm;

#define WXFSM_MIN_UPTIME_MS   40000u      // first fetch no earlier than this after boot
#define WXFSM_MIN_LINK_S      20u         // and with the station link up this long
#define WXFSM_DEADLINE_MS     8000u       // DNS + connect + reply
#define WXFSM_MIN_HEAP        (14u * 1024u)
#define WXFSM_MIN_BLOCK       (6u * 1024u)
#define WXFSM_FRESH_MS        (65UL * 60000UL)    // up to here the reading is fresh
#define WXFSM_STALE_MS        (125UL * 60000UL)   // up to here it is stale (drawn grey), then expired

void     wxfsm_init(WxFsm* f, uint32_t now_ms, uint32_t seed);
uint8_t  wxfsm_step(WxFsm* f, const WxIn* in);          // call once per second; returns WXA_*
void     wxfsm_result(WxFsm* f, uint32_t now_ms, int ok);   // call when a fetch ends (also after WXA_ABORT, with ok = 0)
void     wxfsm_kick(WxFsm* f);                          // settings changed: fetch in 2 s
uint8_t  wxfsm_age_class(const WxFsm* f, uint32_t now_ms);  // 0 no data, 1 fresh, 2 stale (grey), 3 expired
uint32_t wxfsm_age_ms(const WxFsm* f, uint32_t now_ms);     // 0xFFFFFFFF when there is no data
uint8_t  wxfsm_fails(const WxFsm* f);                   // failures in a row
