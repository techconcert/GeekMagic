// See wxfsm.h and DESIGN-R3.md 2.5. Keep this file free of Arduino calls.
#include "wxfsm.h"

namespace {

const uint32_t PERIOD_MS = 30UL * 60 * 1000;
const uint32_t JITTER_MS = 60UL * 1000;                   // 0 to 60 s added after a success
const uint32_t KICK_MS   = 2000;                          // settings changed: fetch in 2 s
const uint32_t RETRY_MS[5] = {60000UL, 120000UL, 300000UL, 900000UL, 1800000UL};   // 1, 2, 5, 15 min, then every 30 min

uint32_t rnd(WxFsm* f) {            // xorshift32, state never 0
  uint32_t x = f->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  f->rng = x;
  return x;
}

}  // namespace

void wxfsm_init(WxFsm* f, uint32_t now_ms, uint32_t seed) {
  uint8_t* b = (uint8_t*)f;
  for (unsigned i = 0; i < sizeof(WxFsm); i++) b[i] = 0;
  f->rng = seed ? seed : 0x9E3779B9u;
  f->ref = now_ms;
  f->delay = 0;                       // due at once; the uptime and link gates hold it back
}

uint8_t wxfsm_step(WxFsm* f, const WxIn* in) {
  const uint32_t now = in->now_ms;
  if (f->have_ok && now - f->ok_ms > WXFSM_STALE_MS) { f->have_ok = 0; f->expired = 1; }   // latched: immune to the 49.7 day wrap

  if (f->running) {
    if (!in->configured || in->updating || in->safe_mode) return WXA_ABORT;
    if (f->kick) { f->kick = 0; f->rearm = 1; }
    if (f->rearm) return WXA_ABORT;                       // the fetch used old settings
    if (now - f->fetch_start >= WXFSM_DEADLINE_MS) return WXA_ABORT;
    return WXA_NONE;
  }

  if (f->kick) {
    f->kick = 0; f->rearm = 0;
    f->ref = now; f->delay = KICK_MS; f->fails = 0;
  }
  if (!in->configured || in->safe_mode || in->updating || in->strikes >= 2) return WXA_NONE;
  if (in->uptime_ms < WXFSM_MIN_UPTIME_MS || in->sta_up_s < WXFSM_MIN_LINK_S) return WXA_NONE;
  if (in->http_recent) return WXA_NONE;
  if (in->free_heap < WXFSM_MIN_HEAP || in->max_block < WXFSM_MIN_BLOCK) return WXA_NONE;
  if (in->screen_off && !in->wake_soon) return WXA_NONE;
  if (now - f->ref < f->delay) return WXA_NONE;
  f->running = 1;
  f->fetch_start = now;
  return WXA_START;
}

void wxfsm_result(WxFsm* f, uint32_t now_ms, int ok) {
  f->running = 0;
  f->ref = now_ms;
  if (ok) {
    f->ok_ms = now_ms; f->have_ok = 1; f->expired = 0; f->fails = 0;
    f->delay = PERIOD_MS + rnd(f) % (JITTER_MS + 1);
  } else {
    f->delay = RETRY_MS[f->fails < 4 ? f->fails : 4];
    if (f->fails < 255) f->fails++;
  }
  if (f->rearm) { f->rearm = 0; f->delay = KICK_MS; f->fails = 0; }
}

void wxfsm_kick(WxFsm* f) { f->kick = 1; }

uint32_t wxfsm_age_ms(const WxFsm* f, uint32_t now_ms) {
  return f->have_ok ? now_ms - f->ok_ms : 0xFFFFFFFFu;
}

uint8_t wxfsm_age_class(const WxFsm* f, uint32_t now_ms) {
  if (f->expired) return 3;
  if (!f->have_ok) return 0;
  uint32_t age = now_ms - f->ok_ms;
  if (age <= WXFSM_FRESH_MS) return 1;
  if (age <= WXFSM_STALE_MS) return 2;
  return 3;
}

uint8_t wxfsm_fails(const WxFsm* f) { return f->fails; }
