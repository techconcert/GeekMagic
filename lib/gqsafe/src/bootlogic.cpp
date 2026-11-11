#include "bootlogic.h"

void boot_decide(BootCounters* c, int cold, BootDecision* d) {
  uint32_t force = c->force_safe;
  bool planned = c->planned == 1;
  if (cold) {
    c->crashes = 0;
    c->slow = 0;
  } else if (planned) {
    c->crashes = 0;                       // slow is only cleared by 6 h of uptime or a user choice
  } else {
    if (c->crashes < BOOT_COUNTER_CAP) c->crashes++;
    if (c->slow < BOOT_COUNTER_CAP) c->slow++;
  }
  d->safe = (force == 1 || c->crashes >= BOOT_CRASHES_SAFE || c->slow >= BOOT_SLOW_SAFE) ? 1 : 0;
  d->no_display = c->crashes >= BOOT_CRASHES_NO_DISPLAY ? 1 : 0;
  c->planned = 0;
  c->force_safe = 0;
}

void heapwatch_init(HeapWatch* w) {
  w->low_since = w->crit_since = 0;
  w->min_free = 0xFFFFFFFFu;
  w->low = w->crit = w->primed = 0;
}

uint8_t heapwatch_step(HeapWatch* w, uint32_t now, uint32_t free_heap, uint32_t max_block) {
  if (free_heap < w->min_free) w->min_free = free_heap;
  bool low = free_heap < HW_WARN_FREE;
  bool crit = free_heap < HW_RESTART_FREE || max_block < HW_RESTART_BLOCK;
  if (low && !w->low) w->low_since = now;
  if (crit && !w->crit) w->crit_since = now;
  w->low = low ? 1 : 0;
  w->crit = crit ? 1 : 0;
  uint8_t out = 0;
  if (low && now - w->low_since >= HW_HOLD_MS) out |= HW_WARN;
  if (crit && now - w->crit_since >= HW_HOLD_MS) out |= HW_RESTART;
  return out;
}

int maint_due(const MaintIn* in) {
  if (!in->enabled || in->updating || in->ap_clients) return 0;
  if (in->uptime_ms < MAINT_UPTIME_MS) return 0;
  if (in->since_http_ms < MAINT_QUIET_MS) return 0;
  if (in->clock_valid) return in->hour == 4 && in->minute < 30;
  return in->hero_asleep != 0;
}
