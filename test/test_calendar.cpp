// Chunk 6: the day rule under clock trouble, seasons, weather statistics, festivals, daily events,
// particle budget and governor, look ranges, lightning, restore (design 5, 6.2, 6.4, 8.10).
#include "gq_test.h"
#include "../lib/gamecore/src/gq_mod.h"
#include "../lib/gamecore/src/gq_types.h"

static uint32_t rs = 0xC0FFEE11u;
static uint32_t rnd32() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

static void feed(int minute) { gc_set_clock(minute / 60, minute % 60, 1); gc_tick(250); }
static int day() { return (int)gc_query(GQ_Q_DAY); }
static int minute() { return (int)gc_query(GQ_Q_MINUTE); }
static int popcount(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

static void start(int start_minute) {
  gc_init(GC_SCENE_VILLAGE, 7, 0);
  gc_set_clock(start_minute / 60, start_minute % 60, 1);
  gc_init(GC_SCENE_VILLAGE, 7, 0);   // cal_init takes the wall clock
}

// 400 days of minute-by-minute wall clock: exactly 400 day increments, minute follows the clock
static void test_day_count() {
  start(8 * 60);
  int m = 8 * 60, d0 = day();
  for (long i = 0; i < 400L * 1440; i++) {
    m = (m + 1) % 1440;
    feed(m);
    CHECK(minute() == m);
  }
  CHECK(day() - d0 == 400);
  // the game's own clock (invalid wall clock): 60 s per minute, 12 minutes from 23:50 cross midnight once
  start(23 * 60 + 50);
  gc_set_clock(0, 0, 0);
  d0 = day();
  for (int i = 0; i < 12 * 240; i++) gc_tick(250);
  CHECK(day() - d0 == 1);
  CHECK(minute() >= 1 && minute() <= 3);
}

// jumps of -720..+720 minutes at random moments never change the day, except a natural crossing
static void test_jumps() {
  start(12 * 60);
  int m = 12 * 60;
  for (int n = 0; n < 1000; n++) {
    int steps = (int)(rnd32() % 400);
    for (int i = 0; i < steps; i++) { m = (m + 1) % 1440; feed(m); }
    int jump = (int)(rnd32() % 1441) - 720;
    int nm = ((m + jump) % 1440 + 1440) % 1440;
    int d = day();
    int prev = minute();
    CHECK(prev == m);
    feed(nm);
    bool natural = prev >= 1435 && nm <= 5;
    if (!natural) CHECK(day() == d);      // a jump never changes the day
    CHECK(day() - d <= 1);                // a crossing counts once
    CHECK(minute() == nm);
    m = nm;
  }
  // a clock flapping around midnight counts at most one day
  start(23 * 60 + 50);
  int d = day();
  for (int i = 0; i < 400; i++) { feed(1439); feed(2); }
  CHECK(day() - d <= 1);
  // a huge dt with an invalid wall clock advances at most one minute (no catch-up loop)
  start(10 * 60);
  gc_set_clock(0, 0, 0);
  int m0 = minute();
  cal_tick(4000000000u);
  CHECK(((minute() - m0 + 1440) % 1440) <= 1);
  cal_tick(4000000000u);
  CHECK(((minute() - m0 + 1440) % 1440) <= 2);
}

// clock invalid, valid, invalid: no day change, minute continuous
static void test_validity() {
  start(10 * 60);
  int d = day();
  gc_set_clock(0, 0, 0);
  for (int i = 0; i < 400; i++) gc_tick(250);
  int m = minute();
  CHECK(m == 10 * 60 + 1 || m == 10 * 60);
  feed(10 * 60 + 5);
  CHECK(minute() == 10 * 60 + 5);
  gc_set_clock(0, 0, 0);
  for (int i = 0; i < 100; i++) { gc_tick(250); CHECK(minute() >= 10 * 60 + 5 && minute() <= 10 * 60 + 6); }
  gc_set_clock(25, 0, 1);            // out of range counts as invalid
  gc_tick(250);
  CHECK(day() == d);
  CHECK(minute() <= 10 * 60 + 6);
}

static void test_seasons() {
  start(8 * 60);
  for (int dd = 0; dd < 120; dd++) {
    gc_debug(GQ_DBG_SET_DAY, dd);
    CHECK(gc_query(GQ_Q_SEASON) == (dd / 7) % 4);
    CHECK(cal_season() == (dd / 7) % 4);
    CHECK(cal_day_of_season() == dd % 7 + 1);
    CHECK(cal_year() == dd / 28 + 1);
  }
  gc_debug(GQ_DBG_SET_DAY, 65535);
  CHECK(day() == 65535);
  gc_debug(GQ_DBG_NEXT_DAY, 0);
  CHECK(day() == 65535);
}

// weather statistics: share of hours per state within 3 sigma of the odds; snow only in winter
static const int ODDS[4][7] = {
  {32, 20, 26, 4, 8, 0, 10}, {44, 14, 12, 14, 3, 0, 13}, {22, 22, 24, 6, 16, 0, 10}, {26, 24, 4, 2, 10, 28, 6}};

static void test_weather_stats() {
  start(0);
  static long cnt[4][7];
  static long tot[4];
  const int DAYS = 2000;
  for (int dd = 0; dd < DAYS; dd++) {
    for (int h = 0; h < 24; h++) {
      feed(h * 60 + 59);
      feed(((h + 1) % 24) * 60);
      int s = cal_season();
      int w = cal_weather();
      CHECK(w >= 0 && w < GQ_W_COUNT);
      CHECK(gc_query(GQ_Q_WEATHER) == w);
      CHECK(G.s.weather_hours <= 8);
      if (w == GQ_W_SNOW) CHECK(s == GQ_WINTER);
      if (cal_festival() >= 0 || (cal_day_of_season() == 7 && minute() < 22 * 60)) CHECK(w == GQ_W_CLEAR);
      cnt[s][w]++;
      tot[s]++;
    }
  }
  for (int s = 0; s < 4; s++) {
    CHECK(tot[s] > 10000);
    for (int w = 0; w < 7; w++) {
      if (ODDS[s][w] == 0) { CHECK(cnt[s][w] == 0); continue; }
      CHECK(cnt[s][w] > 0);
      double p = ODDS[s][w] / 100.0;
      double n = tot[s] / 8.0;                       // states last 2..8 hours: at most n/8 independent draws
      double sd = __builtin_sqrt(p * (1 - p) / n);
      double got = (double)cnt[s][w] / (double)tot[s];
      // festival days (1 in 7, clear until 22:00) push the clear share up
      double fest = (s == 0 || 1) ? (1.0 / 7.0) * (22.0 / 24.0) : 0;
      double expect = w == GQ_W_CLEAR ? p * (1 - fest) + fest : p * (1 - fest);
      if (!(got > expect - 3 * sd - 0.01 && got < expect + 3 * sd + 0.01)) {
        std::printf("season %d weather %d got %.4f expect %.4f sd %.4f\n", s, w, got, expect, sd);
        CHECK(false);
      }
    }
  }
}

// festival flag exactly on day 7 of a season between 16:00 and 22:00; start and end are announced once
static void test_festival() {
  start(0);
  for (int dd = 0; dd < 56; dd++) {
    feed(600);                       // away from midnight, so that setting the day is not a crossing
    gc_debug(GQ_DBG_SET_DAY, dd);
    for (int m = 0; m < 1440; m += 1) {
      feed(m);
      int season = (dd / 7) % 4;
      bool expect = dd % 7 == 6 && m >= 16 * 60 && m < 22 * 60;
      CHECK(cal_festival() == (expect ? season : -1));
      CHECK(gc_query(GQ_Q_FESTIVAL) == (expect ? season : -1));
      CHECK((G.s.festival_flags & 0x0F) == (expect ? (1 << season) : 0));
      if (dd % 7 == 6 && m < 22 * 60) CHECK(cal_weather() == GQ_W_CLEAR);
    }
  }
  // festival lighting never goes dark, and the look stays valid
  gc_debug(GQ_DBG_SET_DAY, 20);   // autumn day 7
  feed(21 * 60 + 30);
  GqLook lk;
  cal_look(&lk);
  CHECK(lk.tr >= 200 && lk.tg >= 170 && lk.tb >= 140);
}

// daily event drawn at 06:00: about 60% of days have one, all 12 kinds occur, never on festival days
static void test_events() {
  start(5 * 60 + 58);
  int seen[13] = {0};
  int none = 0, days = 0;
  int m = 5 * 60 + 58;
  for (int dd = 0; dd < 2000; dd++) {
    // run from 05:58 to 06:02 (natural minutes), then jump to the next 05:58 through midnight
    for (int k = 0; k < 4; k++) { feed(m); m = (m + 1) % 1440; }
    int e = cal_event();
    CHECK(e >= 0 && e <= 12);
    CHECK(gc_query(GQ_Q_EVENT) == e);
    if (day() % 7 == 6) CHECK(e == 0);
    else { seen[e]++; none += e == 0; days++; }
    // on to midnight in natural hourly steps, then to 05:57
    for (int h = 7; h <= 24; h++) { feed(h * 60 - 1); feed(h * 60 % 1440); }
    for (int h = 1; h <= 5; h++) { feed(h * 60 - 1); feed(h * 60); }
    feed(5 * 60 + 57);
    m = 5 * 60 + 58;
  }
  for (int e = 1; e <= 12; e++) CHECK(seen[e] > 0);
  double p = (double)none / days;
  CHECK(p > 0.40 - 0.05 && p < 0.40 + 0.05);
}

// particles: at most 64 strip redraws per second, none when stopped, none indoors
static long redraws_per_second(int fps_x10, int* worst) {
  gc_clear_dirty();
  long total = 0;
  *worst = 0;
  for (int sec = 0; sec < 30; sec++) {
    int n = 0;
    for (int i = 0; i < 25; i++) {            // 25 x 40 ms
      if (fps_x10) weather_set_fps(fps_x10);
      uint32_t before = gc_dirty_mask();
      (void)before;
      gc_clear_dirty();
      weather_tick(40);
      n += popcount(gc_dirty_mask());
    }
    if (n > *worst) *worst = n;
    total += n;
  }
  return total / 30;
}

static void test_particles() {
  for (int w = 0; w < GQ_W_COUNT; w++) {
    start(12 * 60);
    gc_debug(GQ_DBG_SET_DAY, 21);      // winter-ish day: anything may be set by debug
    gc_debug(GQ_DBG_SET_WEATHER, w);
    weather_set_fps(250);
    weather_tick(40);                  // first tick after a change redraws everything once
    int worst;
    long avg = redraws_per_second(0, &worst);
    std::printf("weather %d: %ld strips/s average, worst second %d\n", w, avg, worst);
    CHECK(worst <= 64);
    bool particles = w == GQ_W_RAIN || w == GQ_W_STORM || w == GQ_W_SNOW || w == GQ_W_WINDY;
    CHECK(particles ? avg > 20 : avg == 0);
    // governor: 70 (7 fps) stops them
    weather_set_fps(70);
    weather_tick(40);
    gc_clear_dirty();
    int n = 0;
    for (int i = 0; i < 100; i++) { weather_tick(40); n += popcount(gc_dirty_mask()); gc_clear_dirty(); }
    CHECK(n == 0);
    static uint16_t buf[GC_STRIP_PIXELS], ref[GC_STRIP_PIXELS];
    for (int s = 0; s < GC_ROWS; s++) {
      for (int i = 0; i < GC_STRIP_PIXELS; i++) buf[i] = ref[i] = (uint16_t)(i * 31);
      weather_render(s, buf);
      for (int i = 0; i < GC_STRIP_PIXELS; i++) CHECK(buf[i] == ref[i]);
    }
    // restored above 18 fps
    weather_set_fps(200);
    weather_tick(40);
    CHECK(particles ? gc_dirty_mask() != 0 : true);
    long avg2 = redraws_per_second(0, &worst);
    CHECK(particles ? avg2 > 20 : avg2 == 0);
    // under 12 fps for 5 s: half rate
    gc_clear_dirty();
    for (int i = 0; i < 200; i++) { weather_set_fps(100); weather_tick(40); }   // 8 s
    long avg3 = redraws_per_second(100, &worst);
    CHECK(worst <= 64);
    if (particles) CHECK(avg3 > 5 && avg3 < avg2);
    weather_set_fps(250);
  }
  // particles draw something, inside the strip, with an overrun guard, at most 24 per frame
  start(12 * 60);
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_STORM);
  weather_set_fps(250);
  weather_tick(40);
  static uint16_t guard[GC_STRIP_PIXELS + 64];
  long painted = 0;
  for (int ph = 0; ph < 40; ph++) {
    for (int s = 0; s < GC_ROWS; s++) {
      for (int i = 0; i < GC_STRIP_PIXELS + 64; i++) guard[i] = 0xBEEF;
      weather_render(s, guard);
      for (int i = GC_STRIP_PIXELS; i < GC_STRIP_PIXELS + 64; i++) CHECK(guard[i] == 0xBEEF);
      if (s == 0 || s >= 14) for (int i = 0; i < GC_STRIP_PIXELS; i++) CHECK(guard[i] == 0xBEEF);
      for (int i = 0; i < GC_STRIP_PIXELS; i++) painted += guard[i] != 0xBEEF;
    }
    for (int k = 0; k < 3; k++) weather_tick(40);
  }
  CHECK(painted > 100);
  CHECK(painted < 24 * 6 * 40 * 15);
}

// look values stay within the ranges of 6.2 for every minute, weather, season and festival
static void test_look() {
  start(0);
  for (int w = 0; w < GQ_W_COUNT; w++)
    for (int dd = 0; dd < 28; dd += 3)
      for (int m = 0; m < 1440; m += 7) {
        gc_debug(GQ_DBG_SET_DAY, dd);
        gc_debug(GQ_DBG_SET_WEATHER, w);
        G.s.minute = (uint16_t)m;
        GqLook lk;
        cal_look(&lk);
        CHECK(lk.season == (dd / 7) % 4);
        CHECK(lk.tr >= 0 && lk.tr <= 320 && lk.tg >= 0 && lk.tg <= 320 && lk.tb >= 0 && lk.tb <= 320);
        CHECK(lk.wash_k <= 255);
        if (lk.wash_k == 0) CHECK(lk.wash_rgb565 == 0);
      }
  // clear noon is the identity, clear midnight is dark
  start(12 * 60);
  gc_debug(GQ_DBG_SET_DAY, 0);
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_CLEAR);
  GqLook lk;
  G.s.minute = 12 * 60; cal_look(&lk);
  CHECK(lk.tr == 256 && lk.tg == 256 && lk.tb == 256 && lk.wash_k == 0);
  G.s.minute = 0; cal_look(&lk);
  CHECK(lk.tr == 90 && lk.tb == 180);
  // corrupt minute never indexes outside the keyframes
  G.s.minute = 60000; cal_look(&lk);
  CHECK(lk.tr >= 0 && lk.tr <= 320);
}

// lightning: only in storms, every 20..60 s, two redraws per flash
static void test_lightning() {
  start(12 * 60);
  gc_debug(GQ_DBG_SET_DAY, 1);
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_STORM);
  G.s.weather_hours = 8;
  long t = 0, last = -1;
  int flashes = 0;
  bool on = false;
  for (long i = 0; i < 25 * 60 * 10; i++) {   // 10 min in 40 ms steps
    gc_set_clock(12, 0, 1);
    G.s.weather_hours = 8;
    gc_tick(40);
    t += 40;
    GqLook lk;
    cal_look(&lk);
    bool f = lk.tr == 320;
    if (f && !on) {
      flashes++;
      if (last >= 0) CHECK(t - last >= 19000 && t - last <= 61000);
      last = t;
    }
    on = f;
  }
  CHECK(flashes >= 9 && flashes <= 31);
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_RAIN);
  for (int i = 0; i < 25 * 90; i++) {
    G.s.weather_hours = 8;
    gc_tick(40);
    GqLook lk;
    cal_look(&lk);
    CHECK(lk.tr != 320);
  }
}

// save and restore: weather, hours and flags survive; a restore never changes the day
static void test_restore() {
  start(17 * 60);
  gc_debug(GQ_DBG_SET_DAY, 27);                  // day 7 of winter: festival
  feed(17 * 60);
  feed(17 * 60 + 1);
  CHECK(cal_festival() == GQ_WINTER);
  uint8_t buf[GC_SAVE_BYTES];
  CHECK(gc_save_export(buf, GC_SAVE_BYTES) == GC_SAVE_BYTES);
  int d = day(), w = cal_weather();
  uint8_t hours = G.s.weather_hours, flags = G.s.festival_flags;
  gc_set_clock(23, 59, 1);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 1);
  CHECK(day() == d);                             // restore at 23:59 does not advance the day
  gc_set_clock(17, 2, 1);
  gc_init(GC_SCENE_VILLAGE, 3, 0);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 1);
  CHECK(day() == d && cal_weather() == w && G.s.weather_hours == hours && G.s.festival_flags == flags);
  // damaged calendar fields are clamped by the sanitizer and the module never trusts them
  GqSave s = G.s;
  s.weather = 200; s.weather_hours = 200; s.event_today = 99; s.festival_flags = 0xFF; s.minute = 9999;
  gq_save_sanitize(&s);
  CHECK(s.weather < GQ_W_COUNT && s.weather_hours <= 8 && s.event_today <= 12 && (s.festival_flags & 0xF0) == 0 && s.minute <= 1439);
  // snow outside winter is replaced on load
  start(10 * 60);
  gc_debug(GQ_DBG_SET_DAY, 3);
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_SNOW);
  CHECK(gc_save_export(buf, GC_SAVE_BYTES) == GC_SAVE_BYTES);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 1);
  CHECK(cal_weather() != GQ_W_SNOW);
}

int main() {
  test_day_count();
  test_jumps();
  test_validity();
  test_seasons();
  test_weather_stats();
  test_festival();
  test_events();
  test_particles();
  test_look();
  test_lightning();
  test_restore();
  std::printf("test_calendar OK\n");
  return 0;
}
