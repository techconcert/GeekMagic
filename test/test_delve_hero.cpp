// R8 chunks 3 and 4 (docs/DESIGN-R8.md 8, tests 6, 7, 9 and the save part of 8): the hero's deep mission.
// A simulated mission for every biome over many seeds always ends within the limits, the hero is never stuck in a
// room, one mission a day, loot stays within the caps, a mission survives export/import, and a game without a mission
// behaves as before.
#include "gq_test.h"
#include "../lib/gamecore/src/world_priv.h"
#include "../lib/gamecore/src/delve.h"

enum { BIOME_FOREST = 1, BIOME_CAVE = 2, BIOME_RUINS = 3 };

static int popcount32(uint32_t v) { int c = 0; for (; v; v &= v - 1) c++; return c; }
static int in_delve() { return world_area() == GQ_AREA_DELVE; }
static int hero_home_and_idle() { return G.s.dv_state == 0 && world_area() == GQ_AREA_VILLAGE && !world_transition_busy(); }

// a clean game at 10:00 on day 5 with the tools of a hero who may enter every biome
static void fresh(uint32_t seed, int minute = 10 * 60) {
  sim_start(seed, minute);
  gc_set_date(2026, 3, 3, 2, 1000);
  gc_set_option(GC_OPT_SLEEP_MODE, 0);
  G.s.day = 5;
  G.s.tool[GQ_TOOL_PICK] = 1; G.s.tool[GQ_TOOL_LANTERN] = 1;
  G.s.weather = GQ_W_CLEAR; G.s.weather_hours = 8;
  G.s.tool[GQ_TOOL_SWORD] = 1;
  G.s.hero_hearts = 4;
}

struct MStat {
  int ended, secs, moves, rooms, exit_reached, faints, max_room_s, presence_bad, mins, hearts_min, inv_over;
  long coins_found;
};

static void check_caps(MStat* m) {
  for (int i = 0; i < GQ_ITEM_COUNT; i++) if (G.s.inv[i] > econ_item_cap(i)) m->inv_over++;
  if (G.s.hero_hearts > hero_max_hearts() + 0) m->inv_over++;
  if (G.s.coins > econ_coin_cap()) m->inv_over++;
}

// Runs a mission started through GQ_DBG_DELVE until he is home again (or max_min minutes pass).
static MStat run_mission(int biome, uint32_t seed, int max_min = 66) {
  MStat m;
  std::memset(&m, 0, sizeof m);
  m.hearts_min = 99;
  fresh(seed * 3 + (uint32_t)biome);
  int faints0 = gc_query(GQ_Q_STAT_BASE + GQ_STAT_FAINTS);
  uint32_t lvl = ((seed * 2654435761u) & 0x3FFFFFFFu) | 1;
  gc_debug(GQ_DBG_DELVE, biome | (int)(lvl << 2));
  uint32_t level = G.s.dv_seed;
  CHECK(G.s.dv_state == biome && level != 0);
  int exit_room = delve_gen(level)->exit, last_room = -1, room_s = 0, started = 0;
  for (m.secs = 0; m.secs < max_min * 60; m.secs++) {
    for (int k = 0; k < 4; k++) sim_step();
    if (in_delve()) started = 1;
    if (gc_query(GQ_Q_HERO_PRESENCE) == GQ_PRES_NONE) m.presence_bad++;
    CHECK(G.s.dv_moves <= 90 && G.s.dv_min <= 60);
    check_caps(&m);
    if (G.s.hero_hearts < m.hearts_min) m.hearts_min = G.s.hero_hearts;
    if (in_delve() && G.s.dv_state) {
      if (G.s.dv_cur == last_room) room_s++; else { last_room = G.s.dv_cur; room_s = 0; }
      if (room_s > m.max_room_s) m.max_room_s = room_s;
      if (G.s.dv_cur == exit_room) m.exit_reached = 1;
    }
    if (started && hero_home_and_idle()) { m.ended = 1; break; }
  }
  m.moves = G.s.dv_moves;
  m.mins = G.s.dv_min;
  m.rooms = popcount32(G.s.dv_seen);
  m.faints = gc_query(GQ_Q_STAT_BASE + GQ_STAT_FAINTS) - faints0;
  return m;
}

// ---------------------------------------------------------------- 6. missions end
static void test_missions() {
  const int SEEDS = 60;
  int total = 0, ended = 0, reached = 0;
  long rooms = 0, secs_sum = 0, room_s_worst = 0, moves_worst = 0, min_worst = 0, secs_worst = 0;
  for (int biome = 1; biome <= 3; biome++) {
    int b_total = 0, b_exit = 0;
    long b_rooms = 0, b_secs = 0;
    for (int i = 1; i <= SEEDS; i++) {
      MStat m = run_mission(biome, (uint32_t)i * 97u + (uint32_t)biome);
      total++; b_total++;
      CHECK(m.ended);                       // home within 66 simulated minutes (60 of them in the deep)
      CHECK(m.presence_bad == 0);
      CHECK(m.faints == 0);
      CHECK(m.inv_over == 0);
      CHECK(m.moves <= 90 && m.mins <= 60);
      CHECK(m.max_room_s <= 300);           // never stuck in a room
      ended += m.ended; reached += m.exit_reached; b_exit += m.exit_reached;
      rooms += m.rooms; b_rooms += m.rooms; secs_sum += m.secs; b_secs += m.secs;
      if (m.max_room_s > room_s_worst) room_s_worst = m.max_room_s;
      if (m.moves > moves_worst) moves_worst = m.moves;
      if (m.mins > min_worst) min_worst = m.mins;
      if (m.secs > secs_worst) secs_worst = m.secs;
    }
    std::printf("biome %d: %d missions, exit reached in %d, %.1f rooms left, %.1f min\n", biome, b_total, b_exit,
                (double)b_rooms / b_total, (double)b_secs / b_total / 60.0);
  }
  std::printf("missions: %d ended %d, exit %d, avg rooms %.1f, avg %.1f min, worst: room %lds moves %ld min %ld total %lds\n",
              total, ended, reached, (double)rooms / total, (double)secs_sum / total / 60.0, room_s_worst, moves_worst, min_worst, secs_worst);
  CHECK(ended == total);
  CHECK(reached * 100 >= total * 80);       // at least 80 % reach the exit
  CHECK(rooms >= 15L * total);              // rooms seen >= 15 on average
}

// ---------------------------------------------------------------- 7. abort paths
static int minutes_until_home(int max_min) {
  for (int s = 0; s < max_min * 60; s++) {
    for (int k = 0; k < 4; k++) sim_step();
    if (hero_home_and_idle() && gc_query(GQ_Q_HERO_PRESENCE) == GQ_PRES_SPRITE) return s;
  }
  return -1;
}

static void run_until_room(int rooms_left) {   // into the deep and some rooms deep
  for (int s = 0; s < 40 * 60; s++) {
    for (int k = 0; k < 4; k++) sim_step();
    if (in_delve() && popcount32(G.s.dv_seen) >= rooms_left && !world_transition_busy()) return;
  }
  CHECK(0);
}

static void test_aborts() {
  // bed time (sleep mode 2): home within 2 minutes, mission over
  fresh(21);
  gc_debug(GQ_DBG_DELVE, BIOME_CAVE | (int)(5u << 2));
  run_until_room(2);
  gc_set_option(GC_OPT_SLEEP_MODE, 2);
  int s = minutes_until_home(3);
  CHECK(s >= 0 && s <= 150);
  CHECK(G.s.dv_state == 0);
  std::printf("abort: bed time home after %d s\n", s);
  // dv_min at 59: the hard limit ends it (one more minute, then the walk out)
  fresh(22);
  gc_debug(GQ_DBG_DELVE, BIOME_FOREST | (int)(6u << 2));
  run_until_room(1);
  G.s.dv_min = 59;
  s = minutes_until_home(4);
  CHECK(s >= 0 && G.s.dv_state == 0);
  std::printf("abort: dv_min 59 home after %d s\n", s);
  // 90 room changes
  fresh(23);
  gc_debug(GQ_DBG_DELVE, BIOME_RUINS | (int)(7u << 2));
  run_until_room(1);
  G.s.dv_moves = 90;
  s = minutes_until_home(3);
  CHECK(s >= 0 && G.s.dv_state == 0);
  std::printf("abort: 90 moves home after %d s\n", s);
  // a festival (from 16:00 on a festival day): the mission started at 15:40 ends within 3 minutes of it
  {
    int found = 0;
    for (int day = 3; day < 400 && !found; day++) {
      fresh(24, 15 * 60 + 40);
      gc_debug(GQ_DBG_SET_DAY, day);
      gc_debug(GQ_DBG_DELVE, BIOME_FOREST | (int)(8u << 2));
      for (int k = 0; k < 4 * 60 * 30; k++) {
        sim_step();
        if (gc_query(GQ_Q_FESTIVAL) >= 0) { found = 1; break; }
      }
      if (!found) continue;
      CHECK(G.s.dv_state != 0 || world_area() == GQ_AREA_VILLAGE);
      for (int k = 0; k < 4 * 60 * 3; k++) sim_step();
      CHECK(G.s.dv_state == 0 && gc_query(GQ_Q_FESTIVAL) >= 0);
      CHECK(world_area() == GQ_AREA_VILLAGE);
      std::printf("abort: festival on day %d ends the mission\n", day);
    }
    CHECK(found);
  }
  // a forced activity in the middle: the mission resumes afterwards
  fresh(25);
  gc_debug(GQ_DBG_DELVE, BIOME_FOREST | (int)(9u << 2));
  run_until_room(1);
  uint32_t seed = G.s.dv_seed;
  gc_debug(GQ_DBG_FORCE_ACT, GQ_ACT_FISH);
  CHECK(world_area() == GQ_AREA_VILLAGE && G.s.dv_state == BIOME_FOREST);
  int back = 0;
  for (int k = 0; k < 4 * 60 * 20 && !back; k++) { sim_step(); if (in_delve()) back = 1; }
  CHECK(back && G.s.dv_seed == seed);
}

// ------------------------------------------------- once a day, and no mission when nothing triggers it
static void test_daily() {
  fresh(31, 8 * 60);
  G.s.day = 2;
  int started = 0, days_with = 0, per_day = 0, last_day = -1, max_per_day = 0;
  uint8_t prev = 0;
  for (long step = 0; step < 240L * 1440 * 12; step++) {   // twelve days
    sim_step();
    if (G.s.dv_state && !prev) {
      started++;
      if ((int)G.s.day != last_day) { last_day = G.s.day; per_day = 0; days_with++; }
      per_day++;
      if (per_day > max_per_day) max_per_day = per_day;
    }
    prev = G.s.dv_state;
    // a mission only runs in daylight hours
    if (G.s.dv_state && in_delve()) CHECK(G.s.minute >= 9 * 60 + 30 && G.s.minute < 18 * 60 + 30 + 100);
  }
  std::printf("daily: %d missions in 12 days (%d days), at most %d a day\n", started, days_with, max_per_day);
  CHECK(max_per_day == 1);
  CHECK(started >= 6);
}

static void test_no_mission() {
  // before day 2, outside 09:30..18:30, or without hearts, the hero never starts a mission
  for (int variant = 0; variant < 3; variant++) {
    fresh(40 + (uint32_t)variant, variant == 1 ? 19 * 60 : 6 * 60);
    G.s.day = variant == 0 ? 0 : 6;
    if (variant == 2) G.s.hero_hearts = 1;
    int saw = 0;
    long steps = variant == 0 ? 240L * 1440 * 2 : (variant == 1 ? 240L * 60 * 10 : 240L * 60 * 6);   // two days / 19:00..05:00 / 06:00..12:00
    for (long k = 0; k < steps; k++) {
      if (variant == 2 && k % 4000 == 0) G.s.hero_hearts = 1;
      sim_step();
      if (G.s.dv_state || in_delve()) saw = 1;
    }
    CHECK(!saw);
  }
}

// ------------------------------------------------- 8. save and restore in the middle of a mission
static uint32_t room_hash() {
  uint32_t h = 2166136261u;
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) { h = (h ^ g_ground[y][x]) * 16777619u; h = (h ^ g_obj[y][x]) * 16777619u; }
  return h;
}

static void test_restore() {
  for (int biome = 1; biome <= 3; biome++) {
    fresh(50 + (uint32_t)biome);
    gc_debug(GQ_DBG_DELVE, biome | (int)((100u + (uint32_t)biome) << 2));
    run_until_room(3);
    // wait for a quiet moment in a room, then save
    GqSave before = G.s;
    uint8_t buf[GC_SAVE_BYTES];
    CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
    before = G.s;
    // a different game, then restore
    sim_start(900 + (uint32_t)biome, 10 * 60);
    delve_gen(777);   // another level is the current one
    CHECK(G.s.dv_state == 0);
    CHECK(gc_save_import(buf, sizeof buf) == 1);
    CHECK(G.s.dv_state == before.dv_state && G.s.dv_seed == before.dv_seed && G.s.dv_cur == before.dv_cur &&
          G.s.dv_seen == before.dv_seen && G.s.dv_min == before.dv_min && G.s.dv_moves == before.dv_moves);
    CHECK(world_area() == GQ_AREA_VILLAGE);
    int arrived = 0;
    for (int k = 0; k < 4 * 60 * 6 && !arrived; k++) {
      sim_step();
      if (in_delve() && !world_transition_busy()) arrived = 1;
    }
    CHECK(arrived);
    CHECK(G.s.dv_state == before.dv_state && G.s.dv_seed == before.dv_seed && G.s.dv_cur == before.dv_cur);
    // the room is the same level and the same room: the live tiles equal a clean rebuild
    uint32_t live = room_hash();
    uint8_t cur = G.s.dv_cur;
    CHECK(cur < delve_gen(G.s.dv_seed)->n && delve_gen(G.s.dv_seed)->seed == before.dv_seed);
    world_build_area(GQ_AREA_DELVE);
    CHECK(room_hash() == live);
    // and the mission goes on to the end
    world_instant_area(GQ_AREA_DELVE, SP_ENTRY);
    int home = 0;
    for (int s = 0; s < 62 * 60 && !home; s++) { for (int k = 0; k < 4; k++) sim_step(); CHECK(G.s.dv_min <= 60 && G.s.dv_moves <= 90); if (hero_home_and_idle()) home = 1; }
    CHECK(home);
  }
  // a room index beyond the graph drops the mission
  {
    fresh(60);
    G.s.day = 1;   // no natural mission can start in the next minutes
    uint32_t seed = 0;
    for (uint32_t s = 1; s < 1000; s++) if (delve_gen(s)->n <= 29) { seed = s; break; }
    CHECK(seed);
    G.s.dv_state = BIOME_FOREST; G.s.dv_seed = seed; G.s.dv_cur = 29; G.s.dv_seen = 0; G.s.dv_min = 3; G.s.dv_moves = 4;
    int entered = 0;
    for (int k = 0; k < 4 * 60 * 5; k++) { sim_step(); if (in_delve()) entered = 1; }
    CHECK(!entered && G.s.dv_state == 0);
  }
  // an R7 record (zeros) means no mission: the game just carries on
  {
    fresh(61);
    uint8_t buf[GC_SAVE_BYTES];
    CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
    sim_start(62, 10 * 60);
    CHECK(gc_save_import(buf, sizeof buf) == 1 && G.s.dv_state == 0);
  }
}

// ------------------------------------------------- loot and economy
static void test_loot() {
  // a mission day: finds are capped (10 coins), the errand pays at most 14, goods respect the caps
  int days = 0;
  long worst = 0;
  for (int i = 0; i < 12; i++) {
    int biome = 1 + i % 3;
    fresh(70 + (uint32_t)i);
    G.s.coins = 0;
    uint32_t earned0 = G.s.earned_total;
    uint8_t errands0 = G.s.errands_today;
    gc_debug(GQ_DBG_DELVE, biome | (int)((300u + (uint32_t)i) << 2));
    for (int s = 0; s < 66 * 60; s++) {
      for (int k = 0; k < 4; k++) sim_step();
      for (int it = 0; it < GQ_ITEM_COUNT; it++) CHECK(G.s.inv[it] <= econ_item_cap(it));
      if (G.s.dv_state == 0 && hero_home_and_idle() && s > 120) break;
    }
    econ_sell_all();   // and the whole haul goes over the counter (the till limits what the shop pays in a day)
    long gain = (long)(G.s.earned_total - earned0);
    // the errand (if paid) is 6..14 and finds at most 10; the rest is the sale of the haul
    CHECK(G.s.errands_today - errands0 <= 1);
    if (gain > worst) worst = gain;
    days++;
  }
  std::printf("loot: %d missions, most coins earned by one mission day (errand, finds and selling the whole haul): %ld\n", days, worst);
  CHECK(worst <= 132);                       // the daily income limit of test_econ
}

int main(int argc, char** argv) {
  const char* only = argc > 1 ? argv[1] : "";   // optional: run one part (missions, aborts, daily, none, restore, loot)
  auto want = [&](const char* n) { return !*only || std::strcmp(only, n) == 0; };
  if (want("missions")) test_missions();
  if (want("aborts")) test_aborts();
  if (want("daily")) test_daily();
  if (want("none")) test_no_mission();
  if (want("restore")) test_restore();
  if (want("loot")) test_loot();
  std::printf("test_delve_hero OK\n");
  return 0;
}
