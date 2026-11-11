// Chunk 6: the day rule under clock trouble, seasons, weather statistics, festivals, daily events,
// particle budget and governor, look ranges, lightning, restore (design 5, 6.2, 6.4, 8.10).
#include "gq_test.h"
#include "gq_sanitize.h"
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
  CHECK(lk.tr >= 184 && lk.tg >= 150 && lk.tb >= 126);
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

// a restart starts a fresh game on day 1; damaged calendar fields are clamped by the test sanitizer
// and the module never trusts them
static void test_restore() {
  start(17 * 60);
  gc_debug(GQ_DBG_SET_DAY, 27);                  // day 7 of winter: festival
  feed(17 * 60);
  feed(17 * 60 + 1);
  CHECK(cal_festival() == GQ_WINTER);
  gc_reset_game();
  CHECK(gc_query(GQ_Q_DAY) == 0 && gc_query(GQ_Q_COINS) == 3);
  GqSave s = G.s;
  s.weather = 200; s.weather_hours = 200; s.event_today = 99; s.festival_flags = 0xFF; s.minute = 9999;
  gq_save_sanitize(&s);
  CHECK(s.weather < GQ_W_COUNT && s.weather_hours <= 8 && s.event_today <= 12 && (s.festival_flags & 0xF0) == 0 && s.minute <= 1439);
}


// ---- R3 live weather (design 2.7, 9.3)
extern uint16_t g_cal_wx_events;

static void test_live_basic() {
  start(12 * 60);
  gc_debug(GQ_DBG_SET_DAY, 1);                     // day 2 of spring, no festival
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_FOG);
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  feed(12 * 60);
  CHECK(!cal_live() && cal_weather_shown() == GQ_W_FOG && cal_weather() == GQ_W_FOG);
  for (int w = 0; w < GQ_W_COUNT; w++) {
    gc_set_live_weather(w, 100, 0);
    feed(12 * 60);
    CHECK(cal_live() && cal_weather_shown() == w && cal_weather() == w);
    GqLook lk;
    cal_look(&lk);
    CHECK(lk.season == (w == GQ_W_SNOW ? GQ_WINTER : cal_season()));   // live snow is drawn with the winter palette
  }
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  feed(12 * 60);
  CHECK(!cal_live() && cal_weather_shown() == GQ_W_FOG);
  int t;
  CHECK(cal_temp(&t, nullptr) == 0 && cal_temp_c(&t) == 0);
  std::printf("live basic ok\n");
}

// the simulated machine behaves identically with and without live data underneath
static void run_sim(bool live, uint8_t* out, int n) {
  start(0);
  for (int i = 0; i < n; i++) {
    int m = (i * 60 + 59) % 1440;
    if (live) gc_set_live_weather(i % GQ_W_COUNT, i * 7 % 500, i & 7);
    feed(m);
    feed((m + 1) % 1440);
    out[i] = (uint8_t)G.s.weather;
    out[n + i] = G.s.weather_hours;
  }
}
static void test_live_sim_identical() {
  const int N = 24 * 60;
  static uint8_t a[2 * 24 * 60], b[2 * 24 * 60];
  run_sim(false, a, N);
  run_sim(true, b, N);
  for (int i = 0; i < 2 * N; i++) if (a[i] != b[i]) { std::printf("first diff at %d: %d vs %d\n", i, a[i], b[i]); break; }
  CHECK(std::memcmp(a, b, sizeof(a)) == 0);
  std::printf("live sim identical ok\n");
}

static void test_live_storm_cap() {
  start(12 * 60);
  gc_debug(GQ_DBG_SET_DAY, 1);
  gc_set_live_weather(GQ_W_STORM, 150, 0);
  gc_set_clock(12, 0, 1);
  for (int i = 0; i < 4 * 60 * 119; i++) gc_tick(250);     // 119 minutes
  CHECK(cal_weather() == GQ_W_STORM && cal_weather_shown() == GQ_W_STORM);
  for (int i = 0; i < 4 * 60 * 2; i++) gc_tick(250);       // 121 minutes
  CHECK(cal_weather() == GQ_W_RAIN && cal_weather_shown() == GQ_W_STORM);
  CHECK(gc_query(GQ_Q_WEATHER) == GQ_W_RAIN);
  // a storm of another kind of day does not carry over: the allowance resets at the day change
  gc_set_clock(23, 59, 1); gc_tick(250);
  gc_set_clock(0, 0, 1); gc_tick(250);
  CHECK(cal_weather() == GQ_W_STORM && cal_weather_shown() == GQ_W_STORM);
  std::printf("live storm cap ok\n");
}

static void test_live_event() {
  start(12 * 60);
  gc_debug(GQ_DBG_SET_DAY, 1);
  gc_set_live_weather(GQ_W_CLEAR, 0, 0);
  feed(12 * 60);
  uint16_t e0 = g_cal_wx_events;
  feed(12 * 60);
  CHECK(g_cal_wx_events == e0);                           // no change, no event
  gc_set_live_weather(GQ_W_RAIN, 0, 0); feed(12 * 60);
  CHECK(g_cal_wx_events == (uint16_t)(e0 + 1));
  feed(12 * 60); feed(12 * 60);
  CHECK(g_cal_wx_events == (uint16_t)(e0 + 1));
  gc_set_live_weather(GQ_W_RAIN, 90, GC_WXF_STALE); feed(12 * 60);
  CHECK(g_cal_wx_events == (uint16_t)(e0 + 1));          // same state, only the temperature moved
  gc_set_live_weather(GQ_W_SNOW, 90, 0); feed(12 * 60);
  CHECK(g_cal_wx_events == (uint16_t)(e0 + 2));
  gc_set_live_weather(GC_WX_NONE, 0, 0); feed(12 * 60);   // back to the simulated state: one more event if it differs
  CHECK(g_cal_wx_events == (uint16_t)(e0 + 2 + (cal_weather() != GQ_W_SNOW ? 1 : 0)));
  std::printf("live event ok\n");
}

static void test_live_conversion() {
  start(12 * 60);
  struct { int c10, c, f; } T[] = {
    {-400, -40, -40}, {0, 0, 32}, {378, 4, 100}, {5, 1, 32}, {-5, -1, 31}, {4, 0, 32}, {-4, 0, 32},
    {15, 2, 35}, {-15, -2, 29}, {1000, 100, 212}, {-999, -100, -146}, {999, 100, 212}, {230, 23, 73}, {-120, -12, 10}, {400, 40, 104}};
  for (auto& t : T) {
    gc_set_live_weather(GQ_W_CLEAR, t.c10, 0);
    int c = 99, f = 99, isf = 9;
    CHECK(cal_temp(&c, &isf) == 1 && isf == 0);
    // degrees C: round half away from zero
    int want_c = t.c10 < 0 ? -((-t.c10 + 5) / 10) : (t.c10 + 5) / 10;
    CHECK(c == want_c);
    int cc = 99;
    CHECK(cal_temp_c(&cc) == 1 && cc == want_c);
    gc_set_live_weather(GQ_W_CLEAR, t.c10, GC_WXF_FAHRENHEIT);
    CHECK(cal_temp(&f, &isf) == 1 && isf == 1);
    // reference in tenths of F, exact
    int f50 = (t.c10 > 999 ? 999 : t.c10) * 9 + 1600;
    int want_f = f50 < 0 ? -((-f50 + 25) / 50) : (f50 + 25) / 50;
    CHECK(f == want_f);
  }
  // spot values from the design
  int v, isf;
  gc_set_live_weather(0, -400, GC_WXF_FAHRENHEIT); cal_temp(&v, &isf); CHECK(v == -40);
  gc_set_live_weather(0, 0, GC_WXF_FAHRENHEIT);    cal_temp(&v, &isf); CHECK(v == 32);
  gc_set_live_weather(0, 378, GC_WXF_FAHRENHEIT);  cal_temp(&v, &isf); CHECK(v == 100);
  // absurd inputs are clamped to +-99.9 C
  gc_set_live_weather(0, 2000000000, 0);  cal_temp(&v, &isf); CHECK(v == 100);
  gc_set_live_weather(0, -2000000000, 0); cal_temp(&v, &isf); CHECK(v == -100);
  std::printf("live conversion ok\n");
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
  test_live_basic();
  test_live_sim_identical();
  test_live_storm_cap();
  test_live_event();
  test_live_conversion();
  std::printf("test_calendar OK\n");
  return 0;
}
