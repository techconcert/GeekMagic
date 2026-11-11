// Host tests for the sleep schedule (docs/DESIGN-R3.md 3.2, 9.5).
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_sleep.cpp lib/gqsafe/src/*.cpp
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../lib/gqsafe/src/sleepfsm.h"

static const char* g_test = "";
#define CHECK(c)                                                                      \
  do {                                                                                \
    if (!(c)) {                                                                       \
      fprintf(stderr, "CHECK failed: %s  (%s:%d, %s)\n", #c, __FILE__, __LINE__, g_test); \
      exit(1);                                                                        \
    }                                                                                 \
  } while (0)

#define HH(h, m) ((h) * 60 + (m))

// One simulated clock: wall time in seconds since local midnight of day 0 (offset adjustable to model jumps).
struct Sim {
  SleepFsm f;
  SleepIn in;
  uint32_t ms;            // millis() (may start near the wrap)
  int64_t wall;           // local seconds since day-0 midnight
  uint8_t abed;           // hero lies in bed (set by the test)
  int abed_delay;         // seconds after the bedtime lead the hero needs to be in bed (-1 = never)
  int last_state;
  int bedsecs = 0;
  int sleeps, wakes;      // transitions
  explicit Sim(uint8_t boot, uint32_t start_ms, int64_t start_wall, uint16_t s, uint16_t w) {
    ms = start_ms; wall = start_wall; abed = 0; abed_delay = 5; last_state = SF_AWAKE; sleeps = wakes = 0;
    in = SleepIn();
    in.clock_valid = 1; in.enabled = 1; in.sleep_min = s; in.wake_min = w;
    sleepfsm_init(&f, ms, boot);
  }
  int minute() const { return (int)((wall / 60) % 1440); }
  int step(uint8_t cmd = 0) {
    in.now_ms = ms; in.minute = (uint16_t)minute(); in.cmd = cmd; in.hero_abed = abed;
    int st = sleepfsm_step(&f, &in);
    if (st == SF_ASLEEP && last_state != SF_ASLEEP) sleeps++;
    if (st != SF_ASLEEP && last_state == SF_ASLEEP) wakes++;
    last_state = st;
    // a hero that obeys: in bed after abed_delay seconds of hero_mode 2
    if (f.hero_mode == 2) { bedsecs++; abed = (abed_delay >= 0 && bedsecs >= abed_delay) ? 1 : 0; }
    else { bedsecs = 0; abed = 0; }
    return st;
  }
  void tick(int secs) { for (int i = 0; i < secs; i++) { step(); ms += 1000; wall += 1; } }
  void jump(int minutes) { wall += (int64_t)minutes * 60; if (wall < 0) wall += 86400LL * 400; }
};

static void t_window_table() {
  g_test = "window table";
  const int tab[][2] = {{HH(23, 30), HH(7, 0)}, {HH(1, 0), HH(9, 0)}, {HH(13, 0), HH(15, 0)}, {0, HH(23, 59)}, {HH(23, 59), 0}};
  for (auto& t : tab) {
    const int s = t[0], w = t[1];
    for (int m = 0; m < 1440; m++) {
      int expect_in;
      if (s < w) expect_in = m >= s && m < w; else expect_in = m >= s || m < w;
      CHECK(sleepfsm_in_window((uint16_t)m, (uint16_t)s, (uint16_t)w) == expect_in);
      int d = ((s - m) % 1440 + 1440) % 1440;           // minutes until sleep time
      int expect_lead = !expect_in && d >= 1 && d <= 5;
      CHECK(sleepfsm_in_lead((uint16_t)m, (uint16_t)s, (uint16_t)w) == expect_lead);
    }
  }
  // lead across midnight: sleep 00:02, minutes 23:57..00:01 are lead
  CHECK(sleepfsm_in_lead(HH(23, 57), 2, HH(7, 0)) == 1);
  CHECK(sleepfsm_in_lead(HH(23, 56), 2, HH(7, 0)) == 0);
  CHECK(sleepfsm_in_lead(1, 2, HH(7, 0)) == 1);
  CHECK(sleepfsm_in_window(2, 2, HH(7, 0)) == 1);
  // negative and boundary
  CHECK(sleepfsm_in_window(100, 600, 600) == 0);       // s == w refused
  CHECK(sleepfsm_in_window(1440, 600, 700) == 0);
  CHECK(sleepfsm_in_window(650, 1440, 700) == 0);
  CHECK(sleepfsm_in_window(650, 600, 1440) == 0);
  CHECK(sleepfsm_in_lead(599, 600, 700) == 1);
  CHECK(sleepfsm_in_window(699, 600, 700) == 1 && sleepfsm_in_window(700, 600, 700) == 0);
}

static void t_week() {
  g_test = "week";
  for (int delay = -1; delay <= 40; delay += 41) {      // hero in bed after 5 s, and never
    Sim s(SF_BK_USER, 1000, HH(12, 0) * 60, HH(23, 30), HH(7, 0));
    s.abed_delay = delay < 0 ? -1 : 5;
    int day_sleep_at = -1;
    for (int64_t t = 0; t < 7LL * 86400; t++) {
      const int prev = s.last_state;
      s.tick(1);
      const int64_t sec_of_day = (s.wall - 1) % 86400;
      const int m = (int)(sec_of_day / 60);
      if (s.last_state == SF_ASLEEP && prev != SF_ASLEEP) {
        // screen off: no earlier than sleep time, no later than 120 s (+1 step) after it
        CHECK(sec_of_day >= HH(23, 30) * 60 && sec_of_day <= HH(23, 30) * 60 + 121);
        day_sleep_at = (int)sec_of_day;
      }
      if (s.last_state != SF_ASLEEP && prev == SF_ASLEEP) CHECK(sec_of_day / 60 == HH(7, 0) && (sec_of_day % 60) <= 1);
      if (m >= HH(7, 0) && m < HH(23, 25)) CHECK(s.last_state == SF_AWAKE);
      if (m >= HH(23, 25) && m < HH(23, 30)) { CHECK(s.last_state == SF_BEDTIME); CHECK(s.f.hero_mode == 2); }
      if (s.last_state == SF_AWAKE) CHECK(s.f.hero_mode == 1);
    }
    CHECK(day_sleep_at >= 0);
    // started at noon: 7 nights, 7 sleeps, 6 or 7 wakes (the last night may still be running)
    CHECK(s.sleeps == 7);
    CHECK(s.wakes == 6 || s.wakes == 7);
  }
  // hero in bed quickly: screen off 10 s after he lies down, once the window has begun
  Sim s(SF_BK_USER, 5, HH(23, 29) * 60 - 601, HH(23, 30), HH(7, 0));
  s.abed_delay = 3;
  s.tick(661);                                           // 23:30:00
  CHECK(s.last_state == SF_BEDTIME);
  s.tick(30);
  CHECK(s.last_state == SF_ASLEEP);
  CHECK(s.f.screen_off == 1 && s.f.hero_mode == 2);
}

static void t_clock() {
  g_test = "clock invalid";
  Sim s(SF_BK_USER, 77, HH(2, 0) * 60, HH(23, 30), HH(7, 0));
  s.in.clock_valid = 0;
  s.tick(SF_GRACE_MS / 1000 + 600);
  CHECK(s.sleeps == 0 && s.last_state == SF_AWAKE && s.f.hero_mode == 1);
  s.in.clock_valid = 1;                                 // valid inside the window: bedtime, then asleep
  s.step(); CHECK(s.last_state == SF_BEDTIME);
  s.tick(130);
  CHECK(s.last_state == SF_ASLEEP);
  // once asleep the clock is latched: invalid input does not wake and the minute keeps counting
  s.in.clock_valid = 0;
  s.tick(600);
  CHECK(s.last_state == SF_ASLEEP);
  s.tick(5 * 3600 + 600);                               // now past 07:00 by the carried minute
  CHECK(s.last_state == SF_AWAKE);
  // enabled but clock never valid and no command: sleep now refused
  Sim n(SF_BK_USER, 9, 0, HH(23, 30), HH(7, 0));
  n.in.clock_valid = 0;
  n.step(SF_CMD_SLEEP); n.tick(200);
  CHECK(n.sleeps == 0);
  // minute out of range counts as invalid
  SleepFsm f; sleepfsm_init(&f, 0, SF_BK_USER);
  SleepIn in = SleepIn(); in.enabled = 1; in.clock_valid = 1; in.minute = 1440; in.sleep_min = 600; in.wake_min = 700;
  in.now_ms = 1000; CHECK(sleepfsm_step(&f, &in) == SF_AWAKE);
}

static void t_jumps() {
  g_test = "time jumps";
  const int jumps[] = {-60, 60, 600};
  for (int j : jumps) {
    for (int start = 0; start < 1440; start += 7) {
      Sim s(SF_BK_USER, 123456, (int64_t)start * 60, HH(23, 30), HH(7, 0));
      s.tick(SF_GRACE_MS / 1000 + 5);                   // grace over
      s.tick(600 + start % 5);                          // arbitrary moment
      s.jump(j);
      // settle within 2 steps: the state equals the level state of the new time
      s.step(); s.ms += 1000; s.wall++; s.step(); s.ms += 1000; s.wall++;
      const int m = s.minute();
      const int inwin = sleepfsm_in_window((uint16_t)m, HH(23, 30), HH(7, 0));
      if (!inwin && !sleepfsm_in_lead((uint16_t)m, HH(23, 30), HH(7, 0))) CHECK(s.last_state == SF_AWAKE);
      if (inwin) CHECK(s.last_state != SF_AWAKE);
      // from here on at most one sleep entry before the window next ends
      const int before = s.sleeps;
      s.tick(14 * 3600);
      CHECK(s.sleeps - before <= 1 + 1);                // may enter once now and once for the next night only
      if (inwin) CHECK(s.sleeps - before <= 1);
    }
  }
  // jump while asleep: stays one sleep while still inside the window, wakes when the jump leaves it
  Sim s(SF_BK_USER, 5, HH(23, 0) * 60, HH(23, 30), HH(7, 0));
  s.tick(3 * 3600);                                   // asleep since 23:30, now 02:00
  CHECK(s.last_state == SF_ASLEEP && s.sleeps == 1);
  s.jump(-60); s.tick(2);
  CHECK(s.last_state == SF_ASLEEP && s.sleeps == 1);
  s.jump(600); s.tick(2);
  CHECK(s.last_state == SF_AWAKE);
}

static void t_boot_grace() {
  g_test = "boot grace";
  const uint8_t kinds[] = {SF_BK_POWER, SF_BK_CRASH, SF_BK_USER};
  for (uint8_t k : kinds) {
    Sim s(k, 4000000000u, HH(3, 0) * 60, HH(23, 30), HH(7, 0));   // 03:00 inside the window, millis near the wrap
    s.tick(599);
    CHECK(s.last_state != SF_ASLEEP && s.sleeps == 0);
    s.tick(5);                                          // 10 minutes up: goes to sleep at once (120 s long past)
    CHECK(s.last_state == SF_ASLEEP);
  }
  // automatic restart: dark at once while the time is unknown, at most 60 s
  Sim a(SF_BK_AUTO, 1000, HH(4, 0) * 60, HH(23, 30), HH(7, 0));
  a.in.clock_valid = 0;
  a.tick(30);
  CHECK(a.last_state == SF_ASLEEP && a.f.screen_off == 1);
  a.in.clock_valid = 1;                                 // time known, 04:00 inside the window: stays dark, no lit gap
  a.tick(2);
  CHECK(a.last_state == SF_ASLEEP && a.sleeps == 1);
  // automatic restart in the daytime and no time: lit again after 60 s
  Sim d(SF_BK_AUTO, 1000, HH(12, 0) * 60, HH(23, 30), HH(7, 0));
  d.in.clock_valid = 0;
  d.tick(59); CHECK(d.last_state == SF_ASLEEP);
  d.tick(3);  CHECK(d.last_state == SF_AWAKE);
  d.in.clock_valid = 1; d.tick(20); CHECK(d.last_state == SF_AWAKE && d.sleeps == 1);
  // automatic restart with the schedule off: nothing dark
  Sim o(SF_BK_AUTO, 1000, HH(4, 0) * 60, HH(23, 30), HH(7, 0));
  o.in.enabled = 0; o.in.clock_valid = 0;
  o.tick(5); CHECK(o.last_state == SF_AWAKE && o.f.hero_mode == 0);
}

static void t_commands() {
  g_test = "commands";
  {   // wake now inside the window stays awake until the window ends, then the next night sleeps normally
    Sim s(SF_BK_USER, 99, HH(1, 0) * 60, HH(23, 30), HH(7, 0));
    s.tick(SF_GRACE_MS / 1000 + 300);                   // asleep
    CHECK(s.last_state == SF_ASLEEP);
    s.step(SF_CMD_WAKE); s.ms += 1000; s.wall++;
    CHECK(s.last_state == SF_AWAKE && s.f.hero_mode == 1);
    s.tick(5 * 3600);                                   // to ~06:20
    CHECK(s.last_state == SF_AWAKE);
    s.tick(3 * 3600);                                   // past 07:00: override cleared
    CHECK(s.last_state == SF_AWAKE && s.f.ovr == 0);
    s.tick(15 * 3600);                                  // next night 23:30 +
    CHECK(s.last_state == SF_ASLEEP);
    CHECK(s.sleeps == 2);
  }
  {   // wake now during the lead cancels that night
    Sim t(SF_BK_USER, 99, HH(23, 26) * 60, HH(23, 30), HH(7, 0));
    t.step(); CHECK(t.last_state == SF_BEDTIME);
    t.step(SF_CMD_WAKE); t.ms += 1000; t.wall++;
    CHECK(t.last_state == SF_AWAKE);
    t.tick(3600 * 2);
    CHECK(t.sleeps == 0);
  }
  {   // wake now when there is nothing to wake from is ignored
    Sim s(SF_BK_USER, 99, HH(12, 0) * 60, HH(23, 30), HH(7, 0));
    s.step(SF_CMD_WAKE);
    CHECK(s.f.ovr == 0);
  }
  {   // sleep now outside the window sleeps until the next wake time (here 7 h, 12 h cap not reached)
    Sim s(SF_BK_USER, 99, HH(0, 0) * 60 + 0, HH(1, 0), HH(7, 0));
    s.tick(SF_GRACE_MS / 1000 + 10);                    // 00:10:10 awake (window 01:00..07:00)
    CHECK(s.last_state == SF_AWAKE);
    s.step(SF_CMD_SLEEP); s.ms += 1000; s.wall++;
    CHECK(s.last_state == SF_BEDTIME);
    s.tick(125);
    CHECK(s.last_state == SF_ASLEEP);
    s.tick(5 * 3600);                                   // 02:xx..07:xx: window covers it, wakes at 07:00
    s.tick(2 * 3600);
    CHECK(s.last_state == SF_AWAKE);
  }
  {   // sleep now with a wake time more than 12 h away: capped at 12 h
    Sim s(SF_BK_USER, 99, HH(8, 0) * 60, HH(2, 0), HH(7, 30));   // wake in 23.5 h
    s.tick(SF_GRACE_MS / 1000 + 10);
    s.step(SF_CMD_SLEEP); s.ms += 1000; s.wall++;
    s.tick(130);
    CHECK(s.last_state == SF_ASLEEP);
    s.tick(12 * 3600 - 200);
    CHECK(s.last_state == SF_ASLEEP);
    s.tick(400);
    CHECK(s.last_state == SF_AWAKE);
  }
  {   // sleep now ignores the boot grace; wake now ends it
    Sim s(SF_BK_USER, 99, HH(12, 0) * 60, HH(23, 30), HH(7, 0));
    s.step(SF_CMD_SLEEP); s.ms += 1000; s.wall++;
    s.tick(125);
    CHECK(s.last_state == SF_ASLEEP);
    s.step(SF_CMD_WAKE); s.ms += 1000; s.wall++;
    CHECK(s.last_state == SF_AWAKE && s.f.force_active == 0);
    s.tick(20); CHECK(s.last_state == SF_AWAKE);
  }
  {   // unknown commands change nothing
    Sim s(SF_BK_USER, 99, HH(12, 0) * 60, HH(23, 30), HH(7, 0));
    s.step(3); s.step(255);
    CHECK(s.last_state == SF_AWAKE && s.f.force_active == 0 && s.f.ovr == 0);
  }
}

static void t_blockers() {
  g_test = "blockers";
  {   // updating: no transition to asleep
    Sim s(SF_BK_USER, 5, HH(3, 0) * 60, HH(23, 30), HH(7, 0));
    s.in.updating = 1;
    s.tick(SF_GRACE_MS / 1000 + 600);
    CHECK(s.sleeps == 0 && s.last_state == SF_BEDTIME);
    s.in.updating = 0; s.tick(2);
    CHECK(s.last_state == SF_ASLEEP);
    s.in.updating = 1; s.tick(60);                      // an upload that starts while asleep does not light the screen
    CHECK(s.last_state == SF_ASLEEP);
  }
  {   // safe mode: never, hero mode 0
    Sim s(SF_BK_USER, 5, HH(3, 0) * 60, HH(23, 30), HH(7, 0));
    s.in.safe_mode = 1;
    s.tick(SF_GRACE_MS / 1000 + 600);
    CHECK(s.sleeps == 0 && s.last_state == SF_AWAKE && s.f.hero_mode == 0);
    s.step(SF_CMD_SLEEP); s.tick(300);
    CHECK(s.sleeps == 0);
  }
  {   // schedule off: always awake, hero mode 0; s == w and out of range also count as off
    Sim s(SF_BK_USER, 5, HH(3, 0) * 60, HH(23, 30), HH(7, 0));
    s.in.enabled = 0;
    s.tick(SF_GRACE_MS / 1000 + 600);
    CHECK(s.sleeps == 0 && s.last_state == SF_AWAKE && s.f.hero_mode == 0);
    s.in.enabled = 1; s.in.sleep_min = 600; s.in.wake_min = 600;
    s.tick(100); CHECK(s.last_state == SF_AWAKE && s.f.hero_mode == 0);
    s.in.sleep_min = 1440; s.in.wake_min = 100;
    s.tick(100); CHECK(s.last_state == SF_AWAKE && s.f.hero_mode == 0);
    s.in.sleep_min = 100; s.in.wake_min = 1500;
    s.tick(100); CHECK(s.last_state == SF_AWAKE && s.f.hero_mode == 0);
    // "Sleep now" still works with the schedule off: bed, then screen off, until the wake time; "Wake now" ends it
    Sim u(SF_BK_USER, 5, HH(19, 30) * 60, HH(23, 30), HH(7, 0));
    u.in.enabled = 0;
    u.tick(30); CHECK(u.last_state == SF_AWAKE);
    u.step(SF_CMD_SLEEP); u.ms += 1000; u.wall += 1;
    CHECK(u.f.hero_mode == 2);
    u.tick(30); CHECK(u.last_state == SF_ASLEEP && u.f.screen_off == 1);
    u.step(SF_CMD_WAKE); u.ms += 1000; u.wall += 1; u.tick(3);
    CHECK(u.last_state == SF_AWAKE);
    // switching the schedule off while asleep wakes at once
    Sim t(SF_BK_USER, 5, HH(3, 0) * 60, HH(23, 30), HH(7, 0));
    t.tick(SF_GRACE_MS / 1000 + 200);
    CHECK(t.last_state == SF_ASLEEP);
    t.in.enabled = 0; t.tick(2);
    CHECK(t.last_state == SF_AWAKE && t.f.hero_mode == 0);
  }
}

static void t_wake_soon() {
  g_test = "wake soon";
  Sim s(SF_BK_USER, 5, HH(6, 40) * 60, HH(23, 30), HH(7, 0));
  s.tick(SF_GRACE_MS / 1000 + 5);
  CHECK(s.last_state == SF_ASLEEP && s.f.wake_soon == 0);
  s.tick(5 * 60);                                       // 06:55:xx
  CHECK(s.f.wake_soon == 1 && s.last_state == SF_ASLEEP);
  s.tick(5 * 60);
  CHECK(s.last_state == SF_AWAKE && s.f.wake_soon == 0);
}

static void t_wrap() {
  g_test = "millis wrap";
  // a whole week of nights straddling the 49.7 day wrap point at several offsets
  const uint32_t starts[] = {0xFFFFFFFFu - 3000u, 0xFFFFFFFFu - 40u * 3600u * 1000u, 0xFFFFFFFFu - 12u * 3600u * 1000u};
  for (uint32_t st : starts) {
    Sim s(SF_BK_POWER, st, HH(20, 0) * 60, HH(23, 30), HH(7, 0));
    for (int64_t t = 0; t < 3LL * 86400; t++) s.tick(1);
    CHECK(s.sleeps == 3 || s.sleeps == 2);
    CHECK(s.sleeps >= 2);
    CHECK(s.f.grace_over == 1);                         // the grace latch is not undone by the wrap
  }
  // grace after the wrap: a grace measured from start must not reopen 49.7 days later
  Sim g(SF_BK_USER, 1000, HH(12, 0) * 60, HH(23, 30), HH(7, 0));
  g.tick(700);
  CHECK(g.f.grace_over == 1);
  g.ms += 0xFFFFFF00u;                                  // jump the timer around the full range
  g.tick(5);
  CHECK(g.f.grace_over == 1);
  // force deadline across the wrap
  Sim f(SF_BK_USER, 0xFFFFFFFFu - 30000u, HH(12, 0) * 60, HH(13, 0), HH(14, 0));
  f.tick(SF_GRACE_MS / 1000 + 3);
  f.step(SF_CMD_SLEEP); f.ms += 1000; f.wall++;
  f.tick(130); CHECK(f.last_state == SF_ASLEEP);
}

int main() {
  t_window_table();
  t_week();
  t_clock();
  t_jumps();
  t_boot_grace();
  t_commands();
  t_blockers();
  t_wake_soon();
  t_wrap();
  printf("test_sleep: all passed\n");
  return 0;
}
