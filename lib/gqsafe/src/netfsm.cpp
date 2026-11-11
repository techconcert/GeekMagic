// See netfsm.h and DESIGN.md section 7.2. Keep this file free of Arduino calls.
#include "netfsm.h"

namespace {

const uint32_t GOLDEN_MS       = 10UL * 60 * 1000;
const uint32_t MIN_UPTIME_MS   = 30UL * 60 * 1000;   // automatic restarts need this
const uint32_t BEGIN_GAP_MS    = 3000;               // never two NA_BEGIN closer than this
const uint32_t AP_BEGIN_MS     = 300UL * 1000;       // attempts apart while an AP client is connected
const uint32_t STOP_AFTER_MS   = 20UL * 1000;        // an attempt gets this long
const uint32_t AP_ON_DOWN_MS   = 60UL * 1000;
const uint32_t AP_OFF_UP_MS    = 60UL * 1000;
const uint32_t AP_HTTP_MS      = 300UL * 1000;
const uint32_t CYCLE_FIRST_MS  = 10UL * 60 * 1000;
const uint32_t CYCLE_EVERY_MS  = 30UL * 60 * 1000;
const uint32_t ZOMBIE_MS       = 6UL * 3600 * 1000;
const uint32_t ZOMBIE_NEVER_MS = 30UL * 60 * 1000;
const uint32_t ZOMBIE_UP_MS    = 120UL * 1000;       // give NTP time after a fresh link
const uint32_t ZOMBIE_BEGIN_MS = 2000;
const uint32_t SAFE_BEGIN_MS   = 300UL * 1000;
const uint32_t RESTART_MS[3]   = {6UL * 3600 * 1000, 12UL * 3600 * 1000, 24UL * 3600 * 1000};
const uint32_t RETRY_MS[6]     = {3000, 15000, 30000, 60000, 120000, 300000};
const uint8_t  RETRY_LAST      = 5;

uint32_t rnd(NetFsm* f) {            // xorshift32, state never 0
  uint32_t x = f->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  f->rng = x;
  return x;
}

// delay plus up to 10% jitter
uint32_t jitter(NetFsm* f, uint32_t d) { return d + rnd(f) % (d / 10 + 1); }

bool gapOk(const NetFsm* f, uint32_t now) {
  return !f->begun || now - f->last_begin >= BEGIN_GAP_MS;
}

// plain NA_BEGIN allowed with respect to the AP-client throttle
bool apThrottleOk(const NetFsm* f, uint32_t now, uint8_t clients) {
  return clients == 0 || !f->begun || now - f->last_begin >= AP_BEGIN_MS;
}

// Records an emitted NA_BEGIN and schedules the next ladder attempt.
uint8_t didBegin(NetFsm* f, uint32_t now) {
  uint8_t out = NA_BEGIN;
  f->last_begin = now;
  f->begun = 1;
  f->attempts++;
  if (f->idx < RETRY_LAST) f->idx++;
  f->begin_ref = now;
  f->begin_delay = jitter(f, RETRY_MS[f->idx]);
  f->stop_armed = 1;
  f->stop_ref = now;
  if (f->zombie_pending) {
    f->zombie_pending = 0;
    out |= NA_SNTP_KICK;
  }
  return out;
}

void linkLost(NetFsm* f, uint32_t now) {
  f->down_since = now;
  f->idx = 0;
  f->begin_ref = now;
  f->begin_delay = jitter(f, RETRY_MS[0]);
  f->stop_armed = 0;
  f->cycle_ref = now;
  f->cycle_delay = CYCLE_FIRST_MS;
  f->attempts = 0;
}

uint8_t computeStage(const NetFsm* f, const NetIn* in, uint32_t now) {
  if (!in->have_creds) return NS_NO_CREDS;
  if (in->sta_up) return NS_ONLINE;
  if (f->restart_sent) return NS_RESTART;
  if (now - f->down_since >= CYCLE_FIRST_MS) return NS_CYCLE;
  return NS_RETRY;
}

}  // namespace

void netfsm_init(NetFsm* f, uint32_t now_ms, uint32_t seed) {
  uint8_t* p = reinterpret_cast<uint8_t*>(f);
  for (unsigned i = 0; i < sizeof(NetFsm); i++) p[i] = 0;
  f->rng = seed ^ 0x9E3779B9u;
  if (f->rng == 0) f->rng = 0x2545F491u;
  f->start = now_ms;
  f->up_since = now_ms;
  f->golden = 1;
  f->ap_on = 1;            // safety::begin() starts the hotspot before the machine runs
  linkLost(f, now_ms);     // a fresh boot counts as "down since boot"
}

void netfsm_kick(NetFsm* f) { f->kick = 1; }
uint8_t netfsm_stage(const NetFsm* f) { return f->stage; }
uint32_t netfsm_attempts(const NetFsm* f) { return f->attempts; }

uint8_t netfsm_step(NetFsm* f, const NetIn* in) {
  const uint32_t now = in->now_ms;
  const bool up = in->sta_up != 0;
  uint8_t out = 0;

  if (f->golden && now - f->start >= GOLDEN_MS) f->golden = 0;
  const bool golden = f->golden != 0;

  // ---- edges
  if (!f->primed) {
    f->primed = 1;
    f->prev_up = up ? 1 : 0;
    if (up) f->up_since = now;
  } else if (up && !f->prev_up) {
    f->up_since = now;
    f->mdns_pending = 1;
    f->prev_up = 1;
  } else if (!up && f->prev_up) {
    linkLost(f, now);
    f->prev_up = 0;
  }
  if (up) f->stop_armed = 0;

  // ---- no credentials: hotspot only
  if (!in->have_creds) {
    if (!f->ap_on) { out |= NA_AP_ON; f->ap_on = 1; }
    f->stage = computeStage(f, in, now);
    return out;
  }

  // ---- upload: hotspot only, everything else waits
  if (in->updating) {
    if (!f->ap_on) { out |= NA_AP_ON; f->ap_on = 1; }
    f->stage = computeStage(f, in, now);
    return out;
  }

  // ---- user saved credentials: attempt now (also in safe mode)
  if (f->kick && gapOk(f, now)) {
    f->kick = 0;
    out |= didBegin(f, now);
  }

  // ---- safe mode: minimal policy
  if (in->safe_mode) {
    if (!f->ap_on) { out |= NA_AP_ON; f->ap_on = 1; }
    if (!(out & NA_BEGIN) && !up && in->ap_clients == 0 && gapOk(f, now)) {
      uint32_t ref = f->begun ? f->last_begin : f->start;
      if (now - ref >= SAFE_BEGIN_MS) out |= didBegin(f, now);
    }
    f->stage = computeStage(f, in, now);
    return out;
  }

  // ---- normal operation
  if (!up) {
    bool cycled = false;
    if (!golden && in->ap_clients == 0 && now - f->cycle_ref >= f->cycle_delay) {
      out |= NA_STA_CYCLE;
      cycled = true;
      f->cycle_ref = now;
      f->cycle_delay = CYCLE_EVERY_MS;
      // The next attempt must come after the radio is back (at least 3 s). The cycle
      // does not add attempts: it keeps the ladder's own schedule when that is later.
      uint32_t waited = now - f->begin_ref;
      uint32_t left = waited >= f->begin_delay ? 0 : f->begin_delay - waited;
      f->begin_ref = now;
      f->begin_delay = left > BEGIN_GAP_MS ? left : BEGIN_GAP_MS;
      f->stop_armed = 0;
    }
    if (!cycled && f->stop_armed && now - f->stop_ref >= STOP_AFTER_MS) {
      out |= NA_STA_STOP;
      f->stop_armed = 0;
    }
    if (!cycled && !(out & NA_BEGIN) && now - f->begin_ref >= f->begin_delay &&
        gapOk(f, now) && apThrottleOk(f, now, in->ap_clients)) {
      out |= didBegin(f, now);
    }
    // restart ladder (the caller decides whether a restart is actually allowed to happen)
    if (!golden && !f->restart_sent && in->ap_clients == 0 && now - f->start >= MIN_UPTIME_MS) {
      uint32_t down = now - f->down_since;
      uint8_t r = in->restarts_done > 2 ? 2 : in->restarts_done;
      if (down >= RESTART_MS[r] && (in->ever_connected || down >= RESTART_MS[2])) {
        out |= NA_RESTART;
        f->restart_sent = 1;
      }
    }
  } else {
    // zombie link: "up" but no NTP and no request for hours
    if (!golden && in->ap_clients == 0 && !f->zombie_pending &&
        now - f->up_since >= ZOMBIE_UP_MS &&
        (!f->zombie_done || now - f->last_zombie >= ZOMBIE_MS)) {
      bool bad;
      if (in->last_ntp_ms == 0) {
        bad = now - f->up_since >= ZOMBIE_NEVER_MS;
      } else {
        bool httpOld = in->last_http_ms == 0 || now - in->last_http_ms >= ZOMBIE_MS;
        bad = httpOld && now - in->last_ntp_ms >= ZOMBIE_MS;
      }
      if (bad) {
        out |= NA_STA_STOP;
        f->zombie_pending = 1;
        f->zombie_ref = now;
        f->zombie_done = 1;
        f->last_zombie = now;
      }
    }
  }

  // zombie follow-up: reconnect 2 s after the stop
  if (f->zombie_pending && !(out & (NA_STA_STOP | NA_BEGIN)) &&
      now - f->zombie_ref >= ZOMBIE_BEGIN_MS &&
      gapOk(f, now) && apThrottleOk(f, now, in->ap_clients)) {
    out |= didBegin(f, now);
  }

  // ---- mDNS after the link came back
  if (f->mdns_pending) {
    f->mdns_pending = 0;
    out |= NA_MDNS_RESTART;
  }

  // ---- hotspot
  if (!up) {
    if (!f->ap_on && now - f->down_since >= AP_ON_DOWN_MS) { out |= NA_AP_ON; f->ap_on = 1; }
  } else if (f->ap_on && !golden && in->ap_clients == 0 && now - f->up_since >= AP_OFF_UP_MS) {
    bool httpRecent = in->last_http_ms != 0 && now - in->last_http_ms < AP_HTTP_MS;
    if (!httpRecent) { out |= NA_AP_OFF; f->ap_on = 0; }
  }

  f->stage = computeStage(f, in, now);
  return out;
}
