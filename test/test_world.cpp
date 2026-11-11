// World and hero life (design 11.5): maps, actors, presence, liveness, watchdogs, activities.
#include "gq_test.h"
#include "../lib/gamecore/src/world_priv.h"

static int reach_all(int area, int season, int mask, int* spots_seen) {
  G.s.projects = (uint16_t)mask;
  G.s.day = (uint16_t)(season * 7);
  world_build_area(area);
  int sx, sy;
  if (!world_spot(SP_ENTRY, &sx, &sy)) return 0;
  uint8_t seen[GC_ROWS * GC_COLS] = {0}, q[GC_ROWS * GC_COLS];
  int qh = 0, qt = 0;
  q[qt++] = TI(sx, sy);
  seen[TI(sx, sy)] = 1;
  static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
  while (qh < qt) {
    int t = q[qh++];
    for (int d = 0; d < 4; d++) {
      int nx = TX(t) + dx[d], ny = TY(t) + dy[d];
      if (!walkable(nx, ny) || seen[TI(nx, ny)]) continue;
      seen[TI(nx, ny)] = 1;
      q[qt++] = TI(nx, ny);
    }
  }
  int ok = 1, n = 0;
  for (int s = 0; s < SP_COUNT; s++) {
    int x, y;
    if (!world_spot(s, &x, &y)) continue;
    n++;
    if (!walkable(x, y) || !seen[TI(x, y)]) { std::printf("area %d season %d mask %d: spot %d unreachable\n", area, season, mask, s); ok = 0; }
  }
  for (int target = 0; target < GQ_AREA_COUNT; target++) {
    int x, y, arg;
    if (!world_find_exit(target, &x, &y, &arg)) continue;
    n++;
    if (!walkable(x, y) || !seen[TI(x, y)]) { std::printf("area %d season %d mask %d: exit to %d unreachable\n", area, season, mask, target); ok = 0; }
  }
  if (spots_seen) *spots_seen = n;
  return ok;
}

static void test_maps() {
  gc_init(GC_SCENE_VILLAGE, 1, 0);
  int n;
  for (int area = 0; area < GQ_AREA_COUNT; area++)
    for (int season = 0; season < 4; season++) {
      CHECK(reach_all(area, season, 0, &n));
      CHECK(n >= 2);
    }
  for (int mask = 0; mask < 1024; mask++)
    for (int season = 0; season < 4; season++) CHECK(reach_all(GQ_AREA_VILLAGE, season, mask, 0));
  // the other areas do not depend on projects (the bridge only adds the south exit)
  for (int area = 1; area < GQ_AREA_COUNT; area++) CHECK(reach_all(area, 0, 0x3FF, 0));
  // round trip: village -> forest -> village leaves the map identical
  G.s.projects = 0x2A5; G.s.day = 15;
  world_build_area(GQ_AREA_VILLAGE);
  uint8_t g0[GC_ROWS][GC_COLS], o0[GC_ROWS][GC_COLS];
  std::memcpy(g0, g_ground, sizeof g0); std::memcpy(o0, g_obj, sizeof o0);
  world_build_area(GQ_AREA_FOREST);
  world_build_area(GQ_AREA_CAVE);
  world_build_area(GQ_AREA_VILLAGE);
  CHECK(!std::memcmp(g0, g_ground, sizeof g0) && !std::memcmp(o0, g_obj, sizeof o0));
  std::printf("maps ok\n");
}

// like sim_step, but never clears the dirty mask (the test does that itself once per second)
static void step_no_clear() {
  using namespace gqt;
  gc_tick(250);
  ms_in_minute += 250;
  if (ms_in_minute >= 60000) { ms_in_minute -= 60000; wall_min = (wall_min + 1) % 1440; feed_clock(); }
  if (++frame % 97 == 0) for (int s = 0; s < GC_ROWS; s++) render_strip_guarded(s);
}

// Hero never idles in one phase for more than 10 minutes (sleep and festivals excepted); all the
// invariants of design 4.4 / 13.1, 20 seeds, 3 days each.
static void test_days() {
  const int SEEDS = 20;
  long worst_phase_ms = 0;
  long total_samples = 0;
  for (uint32_t seed = 1; seed <= (uint32_t)SEEDS; seed++) {
    sim_start(seed, 6 * 60);
    long phase_ms = 0, curtain_ticks = 0, quiet = 0, max_curtain = 0;
    int key = -1;
    int day_activities = 0;
    int last_day = 0;
    int areas_seen = 1;
    int32_t earned0 = 0;
    for (long tick = 0; tick < 3L * 86400 * 4; tick++) {
      step_no_clear();
      total_samples++;
      const Actor* h = &A[A_HERO];
      int k = (h->act << 8) | h->phase;
      if (k == key) phase_ms += 250; else { key = k; phase_ms = 0; }
      bool exempt = (h->act == GQ_ACT_SLEEP && h->phase == 2) || h->act == 34;
      if (!exempt && phase_ms > worst_phase_ms) worst_phase_ms = phase_ms;
      if (!exempt && phase_ms > 600000) {
        std::printf("seed %u: act %d phase %d stuck for %ld s\n", (unsigned)seed, h->act, h->phase, phase_ms / 1000);
        CHECK(false);
      }
      int p = gc_query(GQ_Q_HERO_PRESENCE);
      if (p == GQ_PRES_NONE) { std::printf("seed %u: presence none at tick %ld act %d phase %d\n", (unsigned)seed, tick, h->act, h->phase); CHECK(false); }
      if (p == GQ_PRES_CURTAIN) { if (++curtain_ticks > max_curtain) max_curtain = curtain_ticks; } else curtain_ticks = 0;
      CHECK(max_curtain <= 5);                        // 250 ms ticks: 1.25 s at the very most, ~0.7 s in real time
      if (tick % 4 == 3) {                            // once per simulated second
        int nact = 0;
        for (int i = 0; i < A_COUNT; i++) {
          const Actor* a = &A[i];
          if (a->kind == AK_NONE) continue;
          nact++;
          if (a->visible && !walkable(tile_x(a), tile_y(a))) {
            std::printf("seed %u: actor %d kind %d on blocked tile (%d,%d) area %d\n", (unsigned)seed, i, a->kind, tile_x(a), tile_y(a), world_area());
            CHECK(false);
          }
        }
        CHECK(nact <= 8);
        if (gc_dirty_mask() == 0) { if (++quiet > 2) { std::printf("seed %u: no dirty strip for 3 s at tick %ld act %d phase %d area %d minute %d\n", (unsigned)seed, tick, A[0].act, A[0].phase, world_area(), (int)gc_query(GQ_Q_MINUTE)); CHECK(false); } } else quiet = 0;
        gc_clear_dirty();
        int ar = gc_query(GQ_Q_HERO_AREA);
        if (ar != 0) areas_seen |= 1 << ar;
        int d = gc_query(GQ_Q_DAY);
        if (d != last_day) { last_day = d; }
        (void)day_activities; (void)earned0;
      }
    }
    CHECK(areas_seen != 1);
  }
  std::printf("days ok: %d seeds x 3 days, %ld ticks, worst non-sleep phase %ld s\n", SEEDS, total_samples, worst_phase_ms / 1000);
}

static void test_forced() {
  for (int rich = 0; rich < 2; rich++) {
    for (int act = 1; act < GQ_ACT_COUNT; act++) {
      if (act == GQ_ACT_SLEEP) continue;
      for (int hour = 9; hour <= 15; hour += 3) {
        sim_start(100 + act, hour * 60);
        if (rich) {   // every area open, every project built, tools bought, items in the pack
          G.s.projects = 0x3FF; G.s.tool[GQ_TOOL_PICK] = 2; G.s.tool[GQ_TOOL_LANTERN] = 1; G.s.tool[GQ_TOOL_SWORD] = 2;
          G.s.coins = 200; G.s.hero_hearts = 5;
          gc_debug(GQ_DBG_ADD_ITEM, GQ_ITEM_FLOWER << 8 | 3);
          gc_debug(GQ_DBG_ADD_ITEM, GQ_ITEM_WOOD << 8 | 9);
          gc_debug(GQ_DBG_NEXT_DAY, 0);   // rebuilds the village with the new projects
        }
        sim_minutes(1);
        gc_debug(GQ_DBG_FORCE_ACT, act);
        int ms = 0;
        while (gc_query(GQ_Q_HERO_ACT) == act && ms < 400000) { sim_step(); ms += 250; }
        if (ms > 250000) std::printf("activity %d (rich %d, hour %d) took %d s\n", act, rich, hour, ms / 1000);
        CHECK(ms <= 250000);
      }
    }
  }
  std::printf("forced activities ok\n");
}

static void test_variety() {
  long sum = 0;
  int days = 5;
  for (uint32_t seed = 1; seed <= 3; seed++) {
    sim_start(seed, 6 * 60);
    unsigned seen_total = 0;
    long distinct = 0;
    for (int d = 0; d < days; d++) {
      unsigned seen = 0;
      sim_seconds_cb(86400, [&]() {
        int a = gc_query(GQ_Q_HERO_ACT);
        if (a > 0 && a < 32) seen |= 1u << a;
      });
      int c = 0;
      for (unsigned s = seen; s; s >>= 1) c += s & 1;
      distinct += c;
      seen_total |= seen;
    }
    std::printf("seed %u: %ld distinct activities per day on average\n", (unsigned)seed, distinct / days);
    sum += distinct;
    CHECK(distinct >= 6L * days);
  }
}

int main() {
  test_maps();
  test_forced();
  test_variety();
  test_days();
  std::printf("PASS\n");
  return 0;
}
