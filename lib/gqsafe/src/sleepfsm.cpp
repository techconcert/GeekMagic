#include "sleepfsm.h"

static int sf_dist(int from, int to) { return (to + 1440 - from) % 1440; }   // minutes from `from` until `to`

static int sf_ok(uint16_t s, uint16_t w) { return s < 1440 && w < 1440 && s != w; }

uint8_t sleepfsm_in_window(uint16_t m, uint16_t s, uint16_t w) {
  if (m >= 1440 || !sf_ok(s, w)) return 0;
  if (s < w) return (m >= s && m < w) ? 1 : 0;
  return (m >= s || m < w) ? 1 : 0;       // crosses midnight
}

uint8_t sleepfsm_in_lead(uint16_t m, uint16_t s, uint16_t w) {
  if (m >= 1440 || !sf_ok(s, w)) return 0;
  if (sleepfsm_in_window(m, s, w)) return 0;
  return sf_dist(m, s) <= (int)SF_BED_LEAD_MIN ? 1 : 0;   // m != s here, so the distance is 1..5
}

void sleepfsm_init(SleepFsm* f, uint32_t now_ms, uint8_t boot_kind) {
  uint8_t* p = (uint8_t*)f;
  for (unsigned i = 0; i < sizeof(*f); i++) p[i] = 0;
  f->start = now_ms;
  f->boot_kind = boot_kind;
  f->state = SF_AWAKE;
  f->hero_mode = 0;
  f->grace_over = (boot_kind == SF_BK_AUTO) ? 1 : 0;   // an automatic restart gets no lit grace (3.2 boot grace)
}

static void sf_reset(SleepFsm* f, uint8_t hero_mode) {
  f->state = SF_AWAKE;
  f->hero_mode = hero_mode;
  f->screen_off = 0;
  f->wake_soon = 0;
  f->ovr = f->ovr_seen_in = f->force_active = f->due = f->abed = 0;
}

uint8_t sleepfsm_step(SleepFsm* f, const SleepIn* in) {
  const uint32_t now = in->now_ms;
  const uint8_t cmd = in->cmd;
  if (!f->grace_over && now - f->start >= SF_GRACE_MS) f->grace_over = 1;   // latched: never true again after the wrap

  const uint16_t s = in->sleep_min, w = in->wake_min;
  const int enabled = in->enabled && sf_ok(s, w);
  int valid = in->clock_valid && in->minute < 1440;
  int m = in->minute;
  if (valid) {
    f->last_min = in->minute; f->last_ms = now; f->have_min = 1;
  } else if (f->state == SF_ASLEEP && f->have_min) {
    // once asleep the clock counts as valid (latched): carry the last known minute forward on the system timer
    m = (int)((f->last_min + ((now - f->last_ms) / 60000u) % 1440u) % 1440u);
    valid = 1;
  }

  if (in->safe_mode) { sf_reset(f, 0); return f->state; }
  // "Sleep now" works with the schedule switched off too; without it (or once it ran out) it is the built-in night, screen on.
  if (!enabled && !f->force_active && !(cmd == SF_CMD_SLEEP && valid)) { sf_reset(f, 0); f->hold_done = 1; return f->state; }

  if (!f->hold_done) {
    if (f->boot_kind == SF_BK_AUTO && !valid && now - f->start < SF_HOLD_MS) {
      // after an automatic restart (04:00 maintenance) stay dark until the time is known, at most 60 s
      f->state = SF_ASLEEP; f->hero_mode = 1; f->screen_off = 1; f->wake_soon = 0;
      return f->state;
    }
    f->hold_done = 1;
  }

  const int inwin = valid ? sleepfsm_in_window((uint16_t)m, s, w) : 0;
  const int lead  = valid ? sleepfsm_in_lead((uint16_t)m, s, w) : 0;

  // commands
  if (cmd == SF_CMD_WAKE) {
    if (f->state != SF_AWAKE || inwin || lead || f->force_active) {
      f->force_active = 0; f->ovr = 1; f->ovr_seen_in = inwin ? 1 : 0;
    }
  } else if (cmd == SF_CMD_SLEEP && valid) {
    int d = sf_dist(m, w);
    if (d == 0) d = 1440;
    if (d > (int)SF_FORCE_MAX_MIN) d = (int)SF_FORCE_MAX_MIN;
    f->force_active = 1; f->force_until = now + (uint32_t)d * 60000u; f->ovr = 0; f->ovr_seen_in = 0;
  }
  if (f->force_active && (int32_t)(f->force_until - now) <= 0) f->force_active = 0;
  if (f->ovr) {   // "Wake now" lasts until the window is next left
    if (inwin) f->ovr_seen_in = 1;
    else if (f->ovr_seen_in || !lead) { f->ovr = 0; f->ovr_seen_in = 0; }
  }

  const int due = (inwin && !f->ovr) || f->force_active;
  if (due) {
    if (!f->due) { f->due = 1; f->due_since = now; f->abed = 0; }
    if (in->hero_abed) { if (!f->abed) { f->abed = 1; f->abed_since = now; } } else f->abed = 0;
    int off = 0;
    if (f->state == SF_ASLEEP) {
      off = 1;                                            // level: stays asleep while due, whatever else happens
    } else if (!in->updating) {                           // never go to sleep during an upload
      const int gate = f->force_active || f->grace_over;  // the user's "Sleep now" ignores the boot grace
      if (gate && ((f->abed && now - f->abed_since >= SF_ABED_MS) || now - f->due_since >= SF_MAX_WAIT_MS)) off = 1;
    }
    f->state = off ? SF_ASLEEP : SF_BEDTIME;
  } else {
    f->due = 0; f->abed = 0;
    f->state = (lead && !f->ovr) ? SF_BEDTIME : SF_AWAKE;
  }

  f->hero_mode = (f->state == SF_AWAKE) ? 1 : 2;
  f->screen_off = (f->state == SF_ASLEEP) ? 1 : 0;
  f->wake_soon = 0;
  if (f->state == SF_ASLEEP && valid) {
    const int d = sf_dist(m, w);
    if ((d >= 1 && d <= (int)SF_WAKE_SOON_MIN) ||
        (f->force_active && (int32_t)(f->force_until - now) <= (int32_t)(SF_WAKE_SOON_MIN * 60000u)))
      f->wake_soon = 1;
  }
  return f->state;
}
