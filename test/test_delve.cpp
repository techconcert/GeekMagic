// R8 chunks 1 and 2 (docs/DESIGN-R8.md 8, tests 1-5 and the sanitize part of 8): level graph, rooms, monsters, save fields.
#include "gq_test.h"
#include "../lib/gamecore/src/world_priv.h"
#include "../lib/gamecore/src/delve.h"

extern unsigned g_dbg_reach;   // world.cpp: reach_ok() calls (host only)

static uint32_t sd = 0x2545F491u;
static uint32_t rnd32() { sd ^= sd << 13; sd ^= sd >> 17; sd ^= sd << 5; return sd; }

// ------------------------------------------------------------ 1. graph
static int bfs_count(const DelveGraph* g, int from) {
  uint8_t q[DELVE_MAX];
  uint32_t vis = 1u << from;
  int qh = 0, qt = 0;
  q[qt++] = (uint8_t)from;
  while (qh < qt) {
    int u = q[qh++];
    for (int d = 0; d < 4; d++)
      if ((g->link[u] >> d) & 1) {
        int v = delve_nb(u, d);
        CHECK(v >= 0);
        if (!((vis >> v) & 1)) { vis |= 1u << v; q[qt++] = (uint8_t)v; }
      }
  }
  return qt;
}

static void test_graph() {
  long chest_rooms = 0, total = 0;
  for (uint32_t seed = 1; seed <= 10000; seed++) {
    const DelveGraph* g = delve_gen(seed);
    CHECK(g->seed == seed && g->n >= 20 && g->n <= 30);
    CHECK(g->exit != 0 && g->exit < g->n);
    uint64_t occ = 0;
    for (int r = 0; r < g->n; r++) {
      CHECK((g->cell[r] & 7) < 8 && (g->cell[r] >> 3) < 8 && g->cell[r] < 64);
      CHECK(!((occ >> g->cell[r]) & 1));
      occ |= 1ull << g->cell[r];
    }
    for (int r = 0; r < g->n; r++)
      for (int d = 0; d < 4; d++) {
        int nb = delve_nb(r, d);
        if ((g->link[r] >> d) & 1) {
          CHECK(nb >= 0);
          CHECK((g->link[nb] >> (d ^ 2)) & 1);
        }
      }
    CHECK(bfs_count(g, 0) == g->n);
    int doors = 0;
    for (int d = 0; d < 4; d++) doors += (g->link[g->exit] >> d) & 1;
    CHECK(doors == 1);
    for (int r = 0; r < g->n; r++) {      // the exit labels lead to the exit in < n steps
      int c = r, steps = 0;
      while (c != g->exit && steps < g->n) {
        int d = (g->link[c] >> 4) & 3;
        CHECK((g->link[c] >> d) & 1);
        c = delve_nb(c, d);
        CHECK(c >= 0);
        steps++;
      }
      CHECK(c == g->exit);
      int k = delve_kind(r);
      if (r == g->exit) CHECK((k & DK_EXIT) && (k & DK_CHEST));
      if (r == 0) CHECK(k & DK_ENTRY);
      chest_rooms += (k & DK_CHEST) != 0;
    }
    total++;
    // delve_next: from every room with nothing seen the first step is a door; with everything seen it is the exit label
    for (int r = 0; r < g->n; r++) {
      int d = delve_next(r, 0, 0);
      CHECK(d >= 0 && d < 4 && ((g->link[r] >> d) & 1));
      d = delve_next(r, 1, 0);
      CHECK(d == ((g->link[r] >> 4) & 3));
      d = delve_next(r, 0, 0xFFFFFFFFu);
      CHECK(d == ((g->link[r] >> 4) & 3));
    }
  }
  CHECK(chest_rooms >= 4 * total);
  std::printf("graph: %ld levels ok, %.2f chest rooms per level\n", total, (double)chest_rooms / (double)total);

  // delve_next walks to the nearest unseen room: simulate a full tour and check it terminates at the exit quickly
  for (uint32_t seed = 1; seed <= 300; seed++) {
    const DelveGraph* g = delve_gen(seed);
    int cur = 0, moves = 0;
    uint32_t seen = 0;
    while (moves < 90) {
      seen |= 1u << cur;
      if (cur == g->exit) break;
      int d = delve_next(cur, 0, seen);
      cur = delve_nb(cur, d);
      CHECK(cur >= 0);
      moves++;
    }
    // everything but the exit is seen or we walked into the exit: both end with the exit label route
    int guard = 0;
    while (cur != g->exit && guard++ < 40) cur = delve_nb(cur, delve_next(cur, 1, seen));
    CHECK(cur == g->exit);
  }
}

// ----------------------------------------------------- 2. determinism
static uint32_t fnv(uint32_t h, uint8_t b) { return (h ^ b) * 16777619u; }
static uint32_t map_hash() {
  uint32_t h = 2166136261u;
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) { h = fnv(h, g_ground[y][x]); h = fnv(h, g_obj[y][x]); }
  for (int s = 0; s < SP_COUNT; s++) { int x = 0, y = 0; h = fnv(h, (uint8_t)world_spot(s, &x, &y)); h = fnv(h, (uint8_t)(x * 16 + y)); }
  return h;
}
static void set_room(uint32_t seed, int biome, int room, uint32_t seen, int day) {
  G.s.dv_state = (uint8_t)biome; G.s.dv_seed = seed; G.s.dv_cur = (uint8_t)room; G.s.dv_seen = seen; G.s.day = (uint16_t)day;
}
static uint32_t build_hash(uint32_t seed, int biome, int room, uint32_t seen = 0, int day = 0) {
  set_room(seed, biome, room, seen, day);
  world_build_area(GQ_AREA_DELVE);
  return map_hash();
}

static void test_determinism() {
  for (uint32_t seed = 1; seed <= 200; seed++) {
    DelveGraph a = *delve_gen(seed);
    delve_gen(seed + 1000);   // another level in between
    const DelveGraph* b = delve_gen(seed);
    CHECK(a.seed == b->seed && a.n == b->n && a.exit == b->exit);
    CHECK(std::memcmp(a.cell, b->cell, a.n) == 0 && std::memcmp(a.link, b->link, a.n) == 0);
  }
  uint32_t rng0 = gc_rng_get();
  delve_gen(777);
  CHECK(gc_rng_get() == rng0);
  for (int biome = 1; biome <= 3; biome++)
    for (uint32_t seed = 1; seed <= 60; seed++) {
      int room = (int)(seed % delve_gen(seed)->n);
      uint32_t h1 = build_hash(seed, biome, room, 0, (int)seed);
      world_build_area(GQ_AREA_VILLAGE);
      build_hash(seed + 5, biome, 0);
      world_build_area(GQ_AREA_CAVE);
      CHECK(build_hash(seed, biome, room, 0, (int)seed) == h1);
      CHECK(gc_rng_get() == rng0);
    }
  // golden hashes: a saved mission's level must not change silently
  uint32_t g1 = build_hash(1, 1, 0), g2 = build_hash(4242, 2, 7, 0, 7), g3 = build_hash(99999, 3, delve_gen(99999)->exit, 0, 15);
  std::printf("golden %08x %08x %08x\n", g1, g2, g3);
  static const uint32_t GOLD[3] = {0xe64168deu, 0x51099ac5u, 0x943b188eu};
  CHECK(g1 == GOLD[0] && g2 == GOLD[1] && g3 == GOLD[2]);
}

// ------------------------------------------------- 3 and 4. rooms
static uint8_t s_seen[GC_ROWS * GC_COLS];
static void flood(int sx, int sy) {
  uint8_t q[GC_ROWS * GC_COLS];
  std::memset(s_seen, 0, sizeof s_seen);
  int qh = 0, qt = 0;
  q[qt++] = TI(sx, sy);
  s_seen[TI(sx, sy)] = 1;
  static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
  while (qh < qt) {
    int t = q[qh++];
    for (int d = 0; d < 4; d++) {
      int nx = TX(t) + dx[d], ny = TY(t) + dy[d];
      if (!walkable(nx, ny) || s_seen[TI(nx, ny)]) continue;
      s_seen[TI(nx, ny)] = 1;
      q[qt++] = TI(nx, ny);
    }
  }
}
static bool reach(int x, int y) { return walkable(x, y) && s_seen[TI(x, y)]; }

// offset along the edge of the door on side d, or -1
static int door_offset(int d) {
  int x, y;
  if (!world_spot(SP_TARGET_A + d, &x, &y)) return -1;
  return (d & 1) ? y : x;
}

// builds one room and checks everything the design promises; returns the number of reach_ok calls
static unsigned check_room(uint32_t seed, int biome, int room, int season, uint32_t seen) {
  set_room(seed, biome, room, seen, season * 7);
  g_dbg_reach = 0;
  world_build_area(GQ_AREA_DELVE);
  unsigned work = g_dbg_reach;
  const DelveGraph* g = delve_gen(seed);
  int ex, ey;
  CHECK(world_spot(SP_ENTRY, &ex, &ey));
  CHECK(walkable(ex, ey));
  flood(ex, ey);
  int k = delve_kind(room);
  for (int d = 0; d < 4; d++) {
    int bit = (g->link[room] >> d) & 1, x, y;
    CHECK(world_spot(SP_TARGET_A + d, &x, &y) == bit);
    if (!bit) continue;
    CHECK(reach(x, y));
    // walk outward to the wall: the opening must be walkable and connected
    int ox = d == 1 ? 1 : (d == 3 ? -1 : 0), oy = d == 2 ? 1 : (d == 0 ? -1 : 0);
    while (x + ox >= 0 && x + ox < GC_COLS && y + oy >= 0 && y + oy < GC_ROWS) {
      x += ox; y += oy;
      CHECK(reach(x, y));
    }
    CHECK(x == 0 || y == 0 || x == 14 || y == 14);
  }
  int sx, sy;
  if (k & DK_CHEST) { CHECK(world_spot(SP_SITE, &sx, &sy)); CHECK(reach(sx, sy)); CHECK(g_obj[sy - 1][sx] == WT_CHEST || g_obj[sy - 1][sx] == WT_CHEST_OPEN); }
  else CHECK(!world_spot(SP_SITE, &sx, &sy));
  if (k & DK_EXIT) { CHECK(world_spot(SP_DOOR, &sx, &sy)); CHECK(reach(sx, sy)); }
  else CHECK(!world_spot(SP_DOOR, &sx, &sy));
  int objs = 0;
  for (int y = 1; y < 14; y++) for (int x = 1; x < 14; x++) objs += g_obj[y][x] != T_NONE;
  CHECK(objs <= 30);
  return work;
}

static void test_rooms() {
  unsigned worst = 0;
  long builds = 0;
  for (uint32_t seed = 1; seed <= 10000; seed++) {
    const DelveGraph* g = delve_gen(seed);
    int rooms[3] = {0, g->exit, (int)(dv_hash(seed) % g->n)};
    for (int i = 0; i < 3; i++) {
      unsigned w = check_room(seed, 1 + (int)((seed + (uint32_t)i) % 3), rooms[i], (int)((seed >> 2) & 3), (seed & 1) ? 0 : 0x5555u);
      if (w > worst) worst = w;
      builds++;
    }
  }
  for (uint32_t seed = 1; seed <= 300; seed++) {
    const DelveGraph* g = delve_gen(seed);
    for (int biome = 1; biome <= 3; biome++)
      for (int r = 0; r < g->n; r++) {
        unsigned w = check_room(seed, biome, r, (int)((seed + (uint32_t)r) & 3), (r & 1) ? 0xFFFFFFFFu : 0);
        if (w > worst) worst = w;
        builds++;
        if (!(g->link[r] & 15)) CHECK(0);
        // both sides of every door agree on the position
        for (int d = 0; d < 4; d++) {
          if (!((g->link[r] >> d) & 1)) continue;
          set_room(seed, biome, r, 0, 0);
          world_build_area(GQ_AREA_DELVE);
          int a = door_offset(d), nb = delve_nb(r, d);
          set_room(seed, biome, nb, 0, 0);
          world_build_area(GQ_AREA_DELVE);
          CHECK(door_offset(d ^ 2) == a && a >= 4 && a <= 10);
        }
      }
  }
  std::printf("rooms: %ld builds ok, worst reach_ok calls %u\n", builds, worst);
  CHECK(worst <= 188);
}

// ---------------------------------------------------- 5. actors
static void check_actors(int where) {
  int n = 0;
  for (int i = 0; i < A_COUNT; i++) {
    const Actor* a = &A[i];
    if (a->kind == AK_NONE) continue;
    n++;
    if (!a->visible) continue;
    CHECK(walkable(tile_x(a), tile_y(a)) || (where && !aligned(a)));
    CHECK(a->kind == AK_HERO || a->kind == AK_MONSTER);
    if (a->kind == AK_MONSTER) CHECK(a->bob == 0 || (a->bob >= 9 && a->bob <= 11));
    for (int j = i + 1; j < A_COUNT; j++) {
      const Actor* b = &A[j];
      if (b->kind == AK_NONE || !b->visible) continue;
      if (!where && aligned(a) && aligned(b)) CHECK(tile_x(a) != tile_x(b) || tile_y(a) != tile_y(b));
    }
  }
  CHECK(n <= 5);
}

static void test_actors() {
  sim_start(7);
  gc_set_date(2026, 3, 3, 2, 1000);
  long rooms = 0;
  int monsters = 0, biome_seen[3] = {0, 0, 0}, rows_seen[16] = {0};
  for (int i = 0; i < 2000; i++) {
    uint32_t seed = 1 + (uint32_t)i * 7919u;
    int biome = 1 + i % 3;
    gc_debug(GQ_DBG_DELVE, biome | (int)(seed << 2));
    CHECK(world_area() == GQ_AREA_DELVE && G.s.dv_state == biome && G.s.dv_cur == 0);
    // room 0 is quiet; then jump through rooms directly
    const DelveGraph* g = delve_gen(seed);
    int r = i % g->n;
    G.s.dv_cur = (uint8_t)r;
    G.s.dv_seen = (i & 4) ? 0xAAAAAAAu : 0;
    world_instant_area(GQ_AREA_DELVE, SP_TARGET_A + (int)(dv_hash((uint32_t)i) & 3));
    CHECK(world_area() == GQ_AREA_DELVE);
    check_actors(0);
    for (int k = 1; k < A_COUNT; k++)
      if (A[k].kind == AK_MONSTER) {
        monsters++; biome_seen[biome - 1]++; rows_seen[A[k].bob]++;
        if (r == 0 || ((G.s.dv_seen >> r) & 1)) CHECK(0);
      }
    if ((i & 15) == 0) {
      for (int t = 0; t < 200; t++) { sim_step(); if (world_area() != GQ_AREA_DELVE) break; check_actors(1); }
      if (world_area() != GQ_AREA_DELVE) { gc_debug(GQ_DBG_DELVE, 1 | (int)(1u << 2)); }
    }
    rooms++;
  }
  CHECK(monsters > 2000 && biome_seen[0] && biome_seen[1] && biome_seen[2] && rows_seen[9] && rows_seen[10] && rows_seen[11] && rows_seen[0]);
  std::printf("actors: %ld rooms, %d monsters, recolour rows 9/10/11: %d/%d/%d\n", rooms, monsters, rows_seen[9], rows_seen[10], rows_seen[11]);
  // render one room of each biome (guards against overruns in the remapped sprite path)
  for (int b = 1; b <= 3; b++) {
    gc_debug(GQ_DBG_DELVE, b | (int)(12345u << 2));
    G.s.dv_cur = (uint8_t)delve_gen(12345)->exit;
    world_instant_area(GQ_AREA_DELVE, SP_ENTRY);
    for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
  }
  // an object change in the delve area is final and leaves nothing behind in the next room
  gc_debug(GQ_DBG_DELVE, 2 | (int)(5u << 2));
  world_obj_change(7, 7, WT_CHEST_OPEN, 100);
  CHECK(g_obj[7][7] == WT_CHEST_OPEN);
  uint32_t h = build_hash(5, 2, 1);
  G.s.dv_cur = 1;
  world_instant_area(GQ_AREA_DELVE, SP_ENTRY);
  CHECK(map_hash() == h);
}

// ---------------------------------------------------------- 8. save
static void test_save_fields() {
  CHECK(sizeof(GqSave) == 192 && GQ_SAVE_VERSION == 1 && GQ_AREA_COUNT == 8 && GQ_AREA_DELVE == 8);
  static GqSave s;
  for (int i = 0; i < 50000; i++) {
    uint8_t* p = (uint8_t*)&s;
    for (unsigned k = 0; k < sizeof s; k++) p[k] = (uint8_t)rnd32();
    if (i & 1) { s.dv_state = (uint8_t)(rnd32() & 3); s.dv_seed = (i & 2) ? rnd32() : 0; }
    gq_save_sanitize(&s);
    CHECK(s.dv_state <= 3 && s.dv_cur <= 29 && s.dv_min <= 60 && s.dv_moves <= 90 && (s.dv_seen >> 30) == 0);
    if (s.dv_state == 0) CHECK(s.dv_cur == 0 && s.dv_seed == 0 && s.dv_seen == 0 && s.dv_min == 0 && s.dv_moves == 0);
    else CHECK(s.dv_seed != 0);
    for (int k = 0; k < 24; k++) CHECK(s.reserved[k] == 0);
    CHECK(s.version == GQ_SAVE_VERSION && s.size == GQ_SAVE_SIZE);
  }
  // an R7 record (zeros in the old reserved bytes) means no mission
  std::memset(&s, 0, sizeof s);
  s.rng = 5;
  gq_save_sanitize(&s);
  CHECK(s.dv_state == 0 && s.dv_seed == 0);
  // export, import into a fresh core: same fields, and the level is rebuilt from them
  sim_start(11);
  gc_debug(GQ_DBG_DELVE, 3 | (int)(31337u << 2));
  G.s.dv_cur = 5; G.s.dv_seen = 0x1F; G.s.dv_min = 17; G.s.dv_moves = 9; G.s.dv_day = 3; G.s.dv_cnt = 1;
  uint8_t buf[GC_SAVE_BYTES];
  CHECK(gc_save_export(buf, sizeof buf) == GC_SAVE_BYTES);
  CHECK(gq_save_validate(buf, GC_SAVE_BYTES) == GC_SAVE_OK);
  GqSave before = G.s;
  sim_start(12);
  CHECK(G.s.dv_state == 0);
  CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 1);
  CHECK(G.s.dv_state == 3 && G.s.dv_cur == 5 && G.s.dv_seed == 31337u && G.s.dv_seen == 0x1F && G.s.dv_min == 17 &&
        G.s.dv_moves == 9 && G.s.dv_day == 3 && G.s.dv_cnt == 1);
  (void)before;
  uint32_t h1 = build_hash(31337u, 3, 5, 0x1F);
  CHECK(h1 == build_hash(G.s.dv_seed, G.s.dv_state, G.s.dv_cur, G.s.dv_seen));
  // a room index beyond the graph is built as room 0 (the mission is dropped later, chunk 3/4)
  CHECK(build_hash(31337u, 3, 29, 0) == build_hash(31337u, 3, delve_gen(31337u)->n <= 29 ? 0 : 29, 0));
}

int main() {
  gc_init(GC_SCENE_VILLAGE, 1, 0);
  test_graph();
  gc_init(GC_SCENE_VILLAGE, 1, 0);
  test_determinism();
  test_rooms();
  test_actors();
  test_save_fields();
  std::printf("test_delve OK\n");
  return 0;
}
