// Villagers with schedules, the cat, critters and monsters. (Split out of village.cpp; chunk 3.)
#include "world_priv.h"
#include "delve.h"

#define NXY(x, y) ((uint8_t)(((x) << 4) | (y)))

typedef struct { uint8_t home_xy, work_xy, work_from_h, work_to_h, area, head, remap; } NpcDef;   // 7 bytes
// npc ids 1..8 = Mira, Bram, Pip, Odda, Finn, Tess, Luma, Wren (head = id - 1)
static const NpcDef NPCS[8] GC_ROM_ATTR = {
  {NXY(8, 5), NXY(7, 5), 8, 18, GQ_AREA_SHOP, 0, 1},       // Mira: counter of the shop
  {NXY(8, 8), NXY(8, 8), 7, 21, GQ_AREA_VILLAGE, 1, 2},    // Bram: his bench
  {NXY(7, 9), NXY(7, 9), 7, 20, GQ_AREA_VILLAGE, 2, 3},    // Pip: runs around the plaza
  {NXY(9, 6), NXY(5, 4), 10, 22, GQ_AREA_TAVERN, 3, 4},    // Odda: behind the bar
  {NXY(9, 10), NXY(6, 9), 6, 18, GQ_AREA_SHORE, 4, 5},     // Finn: fishes at the shore
  {NXY(6, 7), NXY(8, 6), 8, 17, GQ_AREA_VILLAGE, 5, 6},    // Tess: builds on the plaza
  {NXY(8, 6), NXY(10, 5), 7, 19, GQ_AREA_FOREST, 6, 7},    // Luma: forest clearing
  {NXY(11, 7), NXY(11, 7), 9, 17, GQ_AREA_VILLAGE, 7, 8},  // Wren: market stall, two days a week
};
static const uint8_t NPC_RADIUS[8] GC_ROM_ATTR = {2, 1, 4, 2, 2, 2, 2, 1};
// festival: everybody gathers on the plaza
static const uint8_t FEST_POS[8] GC_ROM_ATTR = {NXY(6, 6), NXY(8, 7), NXY(9, 6), NXY(8, 6), NXY(6, 7), NXY(9, 7), NXY(5, 6), NXY(5, 8)};

int npc_where(int npc, int* area, int* tx, int* ty, int* working) {
  if (npc < 1 || npc > 8) return 0;
  int i = npc - 1;
  int h = cal_minute() / 60;
  if (h < 6 || h >= 22) return 0;
  if (npc == GQ_NPC_WREN) {
    int wd = G.s.day % 7;
    if (!econ_project_built(PRJ_STALL) || (wd != 2 && wd != 5) || h < 9 || h >= 17) return 0;
  }
  uint8_t xy;
  int work = 0, ar = GQ_AREA_VILLAGE;
  if (cal_festival() >= 0) {
    xy = GC_RD8(&FEST_POS[i]);
  } else if (h >= GC_RD8(&NPCS[i].work_from_h) && h < GC_RD8(&NPCS[i].work_to_h)) {
    xy = GC_RD8(&NPCS[i].work_xy);
    ar = GC_RD8(&NPCS[i].area);
    work = 1;
  } else {
    xy = GC_RD8(&NPCS[i].home_xy);
  }
  if (area) *area = ar;
  if (tx) *tx = xy >> 4;
  if (ty) *ty = xy & 15;
  if (working) *working = work;
  return 1;
}

int npc_actor(int npc) {
  for (int i = 1; i < A_COUNT; i++) if (A[i].kind == AK_VILLAGER && A[i].id == npc) return i;
  return -1;
}

void npc_set_talking(int slot, int on) {
  if (slot < 1 || slot >= A_COUNT) return;
  A[slot].talking = on ? 1 : 0;
  A[slot].timer = on ? 30000 : rnd_range(1500, 3000);   // the 30 s is a watchdog: nobody waits forever
}

int npc_monster_slot(int skip) {
  int start = rnd_range(1, A_COUNT - 1);
  for (int k = 0; k < A_COUNT - 1; k++) {
    int i = 1 + (start - 1 + k) % (A_COUNT - 1);
    if (i == skip) continue;
    if (A[i].kind == AK_MONSTER && A[i].visible && !A[i].talking && A[i].act == 0) return i;
  }
  return -1;
}

// ------------------------------------------------------------------ spawning
static int free_tile(int cx, int cy, int r, int* ox, int* oy) {
  for (int t = 0; t < 30; t++) {
    int x = cx + rnd_range(-r, r), y = cy + rnd_range(-r, r);
    if (!walkable(x, y)) continue;
    int hx = tile_x(&A[A_HERO]), hy = tile_y(&A[A_HERO]);
    if (A[A_HERO].visible && (x > hx ? x - hx : hx - x) + (y > hy ? y - hy : hy - y) < 3) continue;
    int taken = 0;
    for (int i = 1; i < A_COUNT; i++) if (A[i].kind != AK_NONE && tile_x(&A[i]) == x && tile_y(&A[i]) == y) taken = 1;
    if (taken) continue;
    *ox = x; *oy = y;
    return 1;
  }
  return 0;
}

static Actor* spawn(int kind, int id, int tx, int ty, int ms) {
  Actor* a = actor_alloc();
  if (!a) return 0;
  a->kind = (uint8_t)kind; a->id = (uint8_t)id; a->visible = 1; a->ms_per_px = (uint8_t)ms;
  place(a, tx, ty);
  a->home = TI(tx, ty);
  a->timer = rnd_range(500, 3000);
  a->anim = (uint8_t)rnd_range(0, 3);
  return a;
}

static void spawn_villager(int npc, int tx, int ty) {
  if (!walkable(tx, ty) && !free_tile(tx, ty, 2, &tx, &ty)) return;
  spawn(AK_VILLAGER, npc, tx, ty, 34);
}

// R8 monsters of a deep room: three per biome, id | palette row << 4 (rows 9..11 are the recolours in world.cpp)
static const uint8_t DV_MON[3][3] GC_ROM_ATTR = {
  {GQ_MON_SLIME, GQ_MON_BAT, GQ_MON_CRAB | 9 << 4}, {GQ_MON_BAT, GQ_MON_SLIME | 10 << 4, GQ_MON_CRAB | 10 << 4},
  {GQ_MON_GHOST, GQ_MON_SLIME | 11 << 4, GQ_MON_BAT | 11 << 4}};

static int mon_ms(int id) { return id == GQ_MON_BAT ? 45 : (id == GQ_MON_GHOST ? 100 : (id == GQ_MON_BOSS ? 110 : 70)); }

static void delve_populate(void) {
  uint32_t seed = G.s.dv_seed ? G.s.dv_seed : 1;
  const DelveGraph* dg = delve_gen(seed);
  int r = G.s.dv_cur < dg->n ? G.s.dv_cur : 0, b = G.s.dv_state - 1, k = delve_kind(r);
  if (b < 0 || b > 2) b = 0;
  if (r == 0 || ((G.s.dv_seen >> r) & 1)) return;          // the entry room and rooms already left are quiet
  uint32_t h = dvh(seed, (uint32_t)r, 0x30B);
  int n = (k & DK_EXIT) ? 3 : ((k & DK_DEN) ? 4 : 2 + (int)((h >> 8) % 2)), cx = 7, cy = 7;
  if (k & DK_EXIT) world_spot(SP_DOOR, &cx, &cy);
  for (int i = 0; i < n; i++) {
    int e = GC_RD8(&DV_MON[b][(h >> (i * 3)) % 3]), id = e & 15, x, y;
    if (b == 2 && (k & DK_EXIT)) { id = i ? GQ_MON_GHOST : GQ_MON_BOSS; e = id; }   // boss and two ghosts
    if (!free_tile(cx, cy, (k & DK_EXIT) ? 4 : 6, &x, &y)) break;
    Actor* a = spawn(AK_MONSTER, id, x, y, mon_ms(id));
    if (a) a->bob = (uint8_t)(e >> 4);
  }
}

void npc_populate(void) {
  int area = world_area();
  if (area == GQ_AREA_DELVE) { delve_populate(); return; }
  for (int n = 1; n <= 8; n++) {
    int ar, x, y, w;
    if (npc_where(n, &ar, &x, &y, &w) && ar == area) spawn_villager(n, x, y);
  }
  int h = cal_minute() / 60, night = cal_is_night(), season = cal_season(), wx = cal_weather();
  int cx, cy;
  // Miso: in the village by day, asleep on the rug at night
  if (area == GQ_AREA_VILLAGE && !night && free_tile(3, 5, 2, &cx, &cy)) spawn(AK_CAT, GQ_NPC_MISO, cx, cy, 60);
  if (area == GQ_AREA_HOME && night && world_spot(SP_TARGET_A, &cx, &cy)) {
    Actor* c = spawn(AK_CAT, GQ_NPC_MISO, cx, cy, 60);
    if (c) c->pose = POSE_BED;
  }
  // monsters
  int mon = -1, nm = 0;
  switch (area) {
    case GQ_AREA_FOREST: mon = GQ_MON_SLIME; nm = 3; break;
    case GQ_AREA_SHORE: mon = GQ_MON_CRAB; nm = 2; break;
    case GQ_AREA_CAVE: mon = GQ_MON_BAT; nm = 3; break;
    case GQ_AREA_RUINS: mon = GQ_MON_GHOST; nm = 2; break;
    default: break;
  }
  for (int k = 0; k < nm; k++) {
    if (!free_tile(7, 7, 6, &cx, &cy)) break;
    spawn(AK_MONSTER, mon, cx, cy, mon_ms(mon));
  }
  if (area == GQ_AREA_RUINS && G.s.chapter == 4 && free_tile(7, 7, 4, &cx, &cy)) spawn(AK_MONSTER, GQ_MON_BOSS, cx, cy, mon_ms(GQ_MON_BOSS));
  // critters: only while there is a free slot
  int day = h >= 6 && h < 20;
  int fair = wx == GQ_W_CLEAR || wx == GQ_W_CLOUDY || wx == GQ_W_WINDY;
  struct { uint8_t type; uint8_t n; uint8_t ms; } want[4];
  int nw = 0;
  if (area == GQ_AREA_VILLAGE) {
    if (day) { want[nw].type = CR_CHICKEN; want[nw].n = 2; want[nw++].ms = 90; }
    if (day && fair && season <= GQ_SUMMER) { want[nw].type = CR_BUTTERFLY; want[nw].n = 1; want[nw++].ms = 55; }
    if (day && season >= GQ_AUTUMN) { want[nw].type = CR_BIRD; want[nw].n = 1; want[nw++].ms = 80; }
    if (night && season == GQ_SUMMER) { want[nw].type = CR_FIREFLY; want[nw].n = 2; want[nw++].ms = 100; }
  } else if (area == GQ_AREA_FOREST) {
    if (season == GQ_SPRING || wx == GQ_W_RAIN) { want[nw].type = CR_FROG; want[nw].n = 1; want[nw++].ms = 120; }
    if (day && fair && season <= GQ_SUMMER) { want[nw].type = CR_BUTTERFLY; want[nw].n = 1; want[nw++].ms = 55; }
    if (night && season == GQ_SUMMER) { want[nw].type = CR_FIREFLY; want[nw].n = 2; want[nw++].ms = 100; }
    if (day) { want[nw].type = CR_BIRD; want[nw].n = 1; want[nw++].ms = 80; }
  } else if (area == GQ_AREA_SHORE && day) {
    want[nw].type = CR_BIRD; want[nw].n = 2; want[nw++].ms = 80;
  }
  for (int k = 0; k < nw; k++)
    for (int j = 0; j < want[k].n; j++) {
      if (!free_tile(7, 7, 6, &cx, &cy)) break;
      if (!spawn(AK_CRITTER, want[k].type, cx, cy, want[k].ms)) return;
    }
}

void npc_sync(void) {
  int area = world_area();
  int ex = 0, ey = 0;
  if (!world_spot(SP_ENTRY, &ex, &ey)) return;
  for (int n = 1; n <= 8; n++) {
    int ar, x, y, w;
    int want = npc_where(n, &ar, &x, &y, &w) && ar == area;
    int slot = npc_actor(n);
    if (want && slot < 0) {
      Actor* a = spawn(AK_VILLAGER, n, ex, ey, 34);   // walks in through the entrance
      if (a) a->timer = 0;
    } else if (!want && slot >= 0 && A[slot].act != 9) {
      Actor* a = &A[slot];
      a->act = 9;                                     // leaving
      if (a->talking) { a->talking = 0; }
      if (!find_path(a, ex, ey)) a->visible = 0;
    }
  }
}

// ------------------------------------------------------------------ behaviour
static void freeze(Actor* n) {
  if (aligned(n)) n->plen = n->pidx;
  else if (n->pidx < n->plen) n->plen = (uint8_t)(n->pidx + 1);
}

static void wander(Actor* n, int radius, int dmin, int dmax) {
  int hx = TX(n->home), hy = TY(n->home);
  for (int t = 0; t < 12; t++) {
    int x = hx + rnd_range(-radius, radius), y = hy + rnd_range(-radius, radius);
    if (find_path(n, x, y) && n->plen <= 10) break;
    n->plen = n->pidx = 0;
  }
  n->timer = rnd_range(dmin, dmax);
}

void npc_tick(int i, uint32_t dt) {
  Actor* n = &A[i];
  n->timer -= (int32_t)dt;
  if (n->kind != AK_VILLAGER) {
    n->anim_acc = (uint16_t)(n->anim_acc + dt);
    if (n->anim_acc >= 400) { n->anim_acc = (uint16_t)(n->anim_acc - 400); n->anim++; }
  }
  if (n->kind == AK_MONSTER && n->act == 1) {            // slain: back after a while
    if (n->timer <= 0 && !n->visible) {
      int x, y;
      if (free_tile(TX(n->home), TY(n->home), 3, &x, &y)) { place(n, x, y); n->visible = 1; n->act = 0; n->pose = POSE_NORMAL; n->emote = I_NONE; }
      else n->timer = 5000;
    }
    return;
  }
  if (n->talking) {
    if (n->timer <= 0) { n->talking = 0; n->timer = 1000; }
    else { freeze(n); return; }
  }
  switch (n->kind) {
    case AK_VILLAGER: {
      if (n->act == 9) {                                  // leaving through the entrance
        if (arrived(n)) { n->visible = 0; n->kind = AK_NONE; }
        return;
      }
      int ar, x, y, work;
      if (!npc_where(n->id, &ar, &x, &y, &work) || ar != world_area()) {   // schedule changed under us
        n->act = 9;
        int ex = 0, ey = 0;
        world_spot(SP_ENTRY, &ex, &ey);
        if (!find_path(n, ex, ey)) { n->visible = 0; n->kind = AK_NONE; }
        return;
      }
      n->home = TI(x, y);
      n->item = I_NONE;
      if (!arrived(n) || n->timer > 0) return;
      if (work) {
        if (tile_x(n) != x || tile_y(n) != y) {
          if (!find_path(n, x, y)) n->timer = 3000;
          return;
        }
        n->dir = n->id == GQ_NPC_FINN ? DIR_RIGHT : DIR_DOWN;
        if (n->id == GQ_NPC_FINN) { n->item = I_ROD; n->bob = (uint8_t)((n->anim_acc / 500) & 1); }
        if (n->id == GQ_NPC_TESS) n->item = (n->anim_acc / 400) & 1 ? (int8_t)WI_HAMMER : (int8_t)I_NONE;
        n->anim_acc = (uint16_t)(n->anim_acc + 100);
        n->timer = 400;
        return;
      }
      wander(n, GC_RD8(&NPC_RADIUS[n->id - 1]), 2500, 8000);
      return;
    }
    case AK_CAT:
      if (n->pose == POSE_BED || !arrived(n) || n->timer > 0) return;
      wander(n, 2, 2500, 7000);
      return;
    case AK_CRITTER:
      if (!arrived(n) || n->timer > 0) return;
      if (n->id == CR_BUTTERFLY || n->id == CR_FIREFLY) wander(n, 3, 300, 900);
      else wander(n, 2, 1500, 4000);
      return;
    case AK_MONSTER:
      if (!arrived(n) || n->timer > 0) return;
      wander(n, 2, 2000, 5000);
      return;
    default: return;
  }
}
