// World: areas and interiors built from scripts, walk grid, path finding, actor table,
// area changes through the curtain, dynamic objects, sprite list and dirty tracking.
// (design 11.5; hero.cpp holds the scheduler and the activities, npc.cpp the others.)
#include "world_priv.h"
#include "map_data.h"

Actor A[A_COUNT];

// villager clothes: the hero body through a palette remap (heads are final-colour overlays and are
// drawn WITHOUT a remap). palette: 0 . 1 k 2 g 3 G 4 l 5 b 6 B 7 s 8 w 9 W 10 f 11 p 12 r 13 o 14 a 15 Y
static const uint8_t NPC_REMAP[9][16] GC_ROM_ATTR = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},     // (hero)
  {0, 1, 2, 3, 4, 6, 15, 7, 8, 12, 10, 11, 12, 13, 14, 15},   // Mira: red dress, blonde
  {0, 1, 2, 3, 4, 14, 13, 7, 8, 2, 10, 11, 12, 13, 14, 15},   // Bram: green coat, white hair
  {0, 1, 2, 3, 4, 1, 5, 7, 8, 15, 10, 11, 12, 13, 14, 6},     // Pip: yellow shirt, dark hair
  {0, 1, 2, 3, 4, 13, 12, 7, 8, 1, 10, 11, 12, 13, 14, 15},   // Odda
  {0, 1, 2, 3, 4, 4, 3, 7, 8, 10, 10, 11, 12, 13, 14, 15},    // Finn
  {0, 1, 2, 3, 4, 1, 2, 7, 8, 12, 10, 11, 12, 13, 14, 15},    // Tess
  {0, 1, 2, 3, 4, 11, 3, 7, 8, 4, 10, 11, 12, 13, 14, 15},    // Luma
  {0, 1, 2, 3, 4, 15, 13, 7, 8, 14, 10, 11, 12, 13, 14, 15},  // Wren
};

// ---------------------------------------------------------------- world state
static uint8_t  s_walk[GC_ROWS][GC_COLS];
static uint8_t  s_area;
static uint8_t  s_spot[SP_COUNT];
static struct { uint8_t tile, area, spot; } s_exit[6];
static uint8_t  s_nexit;
static uint32_t s_rows_water, s_rows_fire, s_rows_bell;
static uint8_t  s_water_frame, s_fire_frame, s_bell_frame, s_zzz_frame;
static uint32_t s_water_acc, s_fire_acc, s_bell_acc, s_dyn_acc, s_idle_acc, s_zzz_acc;
static int32_t  s_bell_ms;
static uint8_t  s_resync, s_rebuild;

struct Dyn { uint8_t area, tile, now, orig; uint16_t timer; };
static Dyn s_dyn[16];

static struct { uint8_t st, area, spot, show; uint16_t t; } s_tr;
enum { TR_IDLE, TR_CLOSE, TR_OPEN };

// ---- sprite list (static, 48 entries: 8 actors x at most 6 each)
struct Op { int16_t x, y; uint8_t sheet, cell, flip, actor, top, remap; };
static Op s_ops[48];
static int s_nops;

// ------------------------------------------------------------------ helpers
int rnd_range(int lo, int hi) { return lo + (int)(gc_rnd() % (uint32_t)(hi - lo + 1)); }

static int ground_blocked(uint8_t g) {
  switch (g) {
    case T_WATER: case T_WATER2: case WT_VOID: case WT_WALL_IN: case WT_WALL_IN_WINDOW: case WT_CAVE_WALL:
    case WT_CAVE_WALL_TOP: case WT_RUIN_WALL: case WT_WAVE: case WT_WAVE2: return 1;
    default: return 0;
  }
}

static int obj_walkable(uint8_t o) {
  switch (o) {
    case T_DOCK: case T_BENCH: case WT_BRIDGE_H: case WT_BRIDGE_V: case T_WALL_DOOR: case WT_DOOR_MAT: case WT_BED_BOT:
    case WT_STOOL: case WT_SAND_SHELL: case WT_MUSHROOM: case WT_HERB: case WT_RUIN_DOOR: case WT_RUG: return 1;
    default: return 0;
  }
}

static int tile_walk(uint8_t g, uint8_t o) { return o == T_NONE ? !ground_blocked(g) : obj_walkable(o); }

void recompute_walk(void) {
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) s_walk[y][x] = (uint8_t)tile_walk(g_ground[y][x], g_obj[y][x]);
}

int walkable(int x, int y) { return x >= 0 && y >= 0 && x < GC_COLS && y < GC_ROWS && s_walk[y][x]; }

int aligned(const Actor* a) { return (a->x & 15) == 0 && (a->y & 15) == 0; }
int tile_x(const Actor* a) { return (a->x + 8) >> 4; }
int tile_y(const Actor* a) { return (a->y + 8) >> 4; }

// BFS on the 15x15 grid (uniform cost: same result as A*). A path is at most 48 tiles.
int find_path(Actor* a, int gx, int gy) {
  a->plen = a->pidx = 0;
  if (!walkable(gx, gy)) return 0;
  int sx = tile_x(a), sy = tile_y(a);
  if (sx < 0 || sy < 0 || sx >= GC_COLS || sy >= GC_ROWS) return 0;
  if (sx == gx && sy == gy) return 1;
  static uint8_t prev[GC_ROWS * GC_COLS];
  static uint8_t queue[GC_ROWS * GC_COLS];
  for (int i = 0; i < GC_ROWS * GC_COLS; i++) prev[i] = 0xFF;
  int qh = 0, qt = 0;
  uint8_t start = TI(sx, sy), goal = TI(gx, gy);
  queue[qt++] = start;
  prev[start] = start;
  static const int8_t DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  while (qh < qt) {
    uint8_t t = queue[qh++];
    if (t == goal) break;
    int x = TX(t), y = TY(t);
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (!walkable(nx, ny)) continue;
      uint8_t n = TI(nx, ny);
      if (prev[n] != 0xFF) continue;
      prev[n] = t;
      queue[qt++] = n;
    }
  }
  if (prev[goal] == 0xFF) return 0;
  int len = 0;
  for (uint8_t t = goal; t != start && len <= 48; t = prev[t]) len++;
  if (len > 48) return 0;
  int i = len;
  for (uint8_t t = goal; t != start; t = prev[t]) a->path[--i] = t;
  a->plen = (uint8_t)len;
  return 1;
}

int arrived(const Actor* a) { return a->pidx >= a->plen && aligned(a); }

void face(Actor* a, int tx, int ty) {
  int dx = tx - tile_x(a), dy = ty - tile_y(a);
  if (dx > 0) a->dir = DIR_RIGHT;
  else if (dx < 0) a->dir = DIR_LEFT;
  else if (dy > 0) a->dir = DIR_DOWN;
  else if (dy < 0) a->dir = DIR_UP;
}

void place(Actor* a, int tx, int ty) {
  a->x = (int16_t)(tx * 16);
  a->y = (int16_t)(ty * 16);
  a->plen = a->pidx = 0;
  a->move_acc = 0;
}

void actor_clear(Actor* a) {
  uint8_t* p = (uint8_t*)a;
  for (unsigned i = 0; i < sizeof(Actor); i++) p[i] = 0;
  a->item = a->emote = I_NONE;
  a->ex = a->ey = -1;
  a->dir = DIR_DOWN;
  a->ms_per_px = 34;
}

Actor* actor_alloc(void) {
  for (int i = 1; i < A_COUNT; i++)
    if (A[i].kind == AK_NONE) { actor_clear(&A[i]); return &A[i]; }
  return 0;
}

static void move_actor(Actor* a, uint32_t dt) {
  a->move_acc = (uint16_t)(a->move_acc + dt);
  int guard = 0;
  while (a->move_acc >= a->ms_per_px && guard++ < 64) {
    a->move_acc = (uint16_t)(a->move_acc - a->ms_per_px);
    int moving = 0;
    if (a->pidx < a->plen) {
      uint8_t t = a->path[a->pidx];
      int tx = TX(t) * 16, ty = TY(t) * 16;
      if (aligned(a) && !walkable(TX(t), TY(t))) {   // the map changed under the path: stop here
        a->plen = a->pidx;
      } else {
        if (a->x != tx) {
          int d = tx > a->x ? 1 : -1;
          a->x = (int16_t)(a->x + d);
          a->dir = d > 0 ? DIR_RIGHT : DIR_LEFT;
        } else if (a->y != ty) {
          int d = ty > a->y ? 1 : -1;
          a->y = (int16_t)(a->y + d);
          a->dir = d > 0 ? DIR_DOWN : DIR_UP;
        }
        if (a->x == tx && a->y == ty) a->pidx++;
        moving = 1;
      }
    }
    if (moving) {
      if (++a->moved >= 6) { a->moved = 0; a->step = (uint8_t)((a->step + 1) & 3); }
    } else {
      a->step = 0;
      a->move_acc = 0;
      break;
    }
  }
}

// --------------------------------------------------------------- map build
static uint32_t s_rs;
static uint32_t scat_rnd(void) { s_rs ^= s_rs << 13; s_rs ^= s_rs >> 17; s_rs ^= s_rs << 5; return s_rs; }

// every spot and exit still reachable from the entry?
static int reach_ok(void) {
  if (s_spot[SP_ENTRY] == 0xFF) return 1;
  static uint8_t seen[GC_ROWS * GC_COLS], q[GC_ROWS * GC_COLS];
  for (int i = 0; i < GC_ROWS * GC_COLS; i++) seen[i] = 0;
  int qh = 0, qt = 0;
  q[qt++] = s_spot[SP_ENTRY];
  seen[s_spot[SP_ENTRY]] = 1;
  static const int8_t DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  while (qh < qt) {
    uint8_t t = q[qh++];
    for (int d = 0; d < 4; d++) {
      int nx = TX(t) + DX[d], ny = TY(t) + DY[d];
      if (!walkable(nx, ny) || seen[TI(nx, ny)]) continue;
      seen[TI(nx, ny)] = 1;
      q[qt++] = TI(nx, ny);
    }
  }
  for (int i = 0; i < SP_COUNT; i++) if (s_spot[i] != 0xFF && !seen[s_spot[i]]) return 0;
  for (int i = 0; i < s_nexit; i++) if (!seen[s_exit[i].tile]) return 0;
  return 1;
}

static int scatter_ground(uint8_t g) {
  return g == T_GRASS || g == T_TUFT || g == T_FLOWER || g == WT_SAND || g == WT_CAVE_FLOOR || g == WT_RUIN_FLOOR;
}

static int is_special(uint8_t t) {
  for (int i = 0; i < SP_COUNT; i++) if (s_spot[i] == t) return 1;
  for (int i = 0; i < s_nexit; i++) if (s_exit[i].tile == t) return 1;
  return 0;
}

static void scatter(int x0, int y0, int w, int h, uint8_t tile, int count, int opi) {
  s_rs = (uint32_t)(0x9E3779B1u * (uint32_t)(s_area + 1)) ^ ((uint32_t)(opi + 1) * 0x85EBCA6Bu);
  if (!s_rs) s_rs = 1;
  recompute_walk();
  int placed = 0;
  for (int tries = 0; tries < count * 6 && placed < count; tries++) {
    int x = x0 + (int)(scat_rnd() % (uint32_t)w), y = y0 + (int)(scat_rnd() % (uint32_t)h);
    if (x < 0 || y < 0 || x >= GC_COLS || y >= GC_ROWS) continue;
    if (g_obj[y][x] != T_NONE || !scatter_ground(g_ground[y][x]) || is_special(TI(x, y))) continue;
    if (!obj_walkable(tile)) {
      s_walk[y][x] = 0;
      if (!reach_ok()) { s_walk[y][x] = 1; continue; }
    }
    g_obj[y][x] = tile;
    placed++;
  }
}

static void fill(int type, int xy, int wh, uint8_t tile) {
  int x0 = xy >> 4, y0 = xy & 15, w = wh >> 4, h = wh & 15;
  if (w == 0) w = 16;
  if (h == 0) h = 16;
  for (int y = y0; y < y0 + h && y < GC_ROWS; y++)
    for (int x = x0; x < x0 + w && x < GC_COLS; x++) {
      if (type == MO_GROUND) g_ground[y][x] = tile;
      else g_obj[y][x] = tile;
    }
}

static void run_script(const MapOp* p, int season) {
  int skip = 0;
  for (int i = 0; i < 100; i++) {
    uint8_t op = GC_RD8(&p[i].op), xy = GC_RD8(&p[i].xy), wh = GC_RD8(&p[i].wh), tile = GC_RD8(&p[i].tile),
            arg = GC_RD8(&p[i].arg);
    if (op == MO_END) break;
    if (skip) { skip = 0; continue; }
    switch (op) {
      case MO_GROUND: case MO_OBJ: fill(op, xy, wh, tile); break;
      case MO_SCATTER: scatter(xy >> 4, xy & 15, (wh >> 4) ? (wh >> 4) : 16, (wh & 15) ? (wh & 15) : 16, tile, arg, i); break;
      case MO_HOUSE: {
        int x = xy >> 4, y = xy & 15;
        if (x + 2 < GC_COLS && y + 1 < GC_ROWS) {
          for (int k = 0; k < 3; k++) g_obj[y][x + k] = (uint8_t)(tile + k);
          g_obj[y + 1][x] = T_WALL_WIN_L;
          g_obj[y + 1][x + 1] = T_WALL_DOOR;
          g_obj[y + 1][x + 2] = T_WALL_WIN_R;
        }
        break;
      }
      case MO_SPOT: if (tile < SP_COUNT && (xy >> 4) < GC_COLS && (xy & 15) < GC_ROWS) s_spot[tile] = TI(xy >> 4, xy & 15); break;
      case MO_EXIT:
        if (s_nexit < 6 && (xy >> 4) < GC_COLS && (xy & 15) < GC_ROWS) {
          s_exit[s_nexit].tile = TI(xy >> 4, xy & 15); s_exit[s_nexit].area = tile; s_exit[s_nexit].spot = arg; s_nexit++;
        }
        break;
      case MO_IF_PROJECT: if (!econ_project_built(arg)) skip = 1; break;
      case MO_IF_SEASON: if (!((arg >> season) & 1)) skip = 1; break;
      default: break;
    }
  }
}

static void apply_dyn(void) {
  for (int i = 0; i < 16; i++)
    if (s_dyn[i].timer && s_dyn[i].area == s_area) g_obj[TY(s_dyn[i].tile)][TX(s_dyn[i].tile)] = s_dyn[i].now;
}

static uint8_t anim_tile(uint8_t t);

void world_build_area(int area) {
  if (area < 0 || area >= GQ_AREA_COUNT) area = GQ_AREA_VILLAGE;
  s_area = (uint8_t)area;
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) { g_ground[y][x] = T_GRASS; g_obj[y][x] = T_NONE; }
  for (int i = 0; i < SP_COUNT; i++) s_spot[i] = 0xFF;
  s_nexit = 0;
  int season = cal_season() & 3;
  switch (area) {
    case GQ_AREA_VILLAGE: run_script(MAP_VILLAGE, season); run_script(MAP_VILLAGE2, season); break;
    case GQ_AREA_FOREST: run_script(MAP_FOREST, season); break;
    case GQ_AREA_SHORE: run_script(MAP_SHORE, season); break;
    case GQ_AREA_CAVE: run_script(MAP_CAVE, season); break;
    case GQ_AREA_RUINS: run_script(MAP_RUINS, season); break;
    case GQ_AREA_HOME: run_script(MAP_HOME, season); break;
    case GQ_AREA_SHOP: run_script(MAP_SHOP, season); break;
    default: run_script(MAP_TAVERN, season); break;
  }
  if (area == GQ_AREA_VILLAGE && cal_festival() >= 0) {   // festival decoration (non-blocking spots only)
    static const uint8_t F[4] = {TI(6, 5), TI(8, 5), TI(10, 8), TI(11, 8)};
    for (int i = 0; i < 4; i++) {
      uint8_t t = F[i];
      if (g_obj[TY(t)][TX(t)] == T_NONE) g_obj[TY(t)][TX(t)] = (uint8_t)(i < 2 ? WT_FEST_LIGHTS : WT_FEST_TABLE);
    }
  }
  // decoration of plain grass: depends on the area and the season only
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      if (g_ground[y][x] != T_GRASS) continue;
      uint32_t h = ((uint32_t)(x * 73 + y * 151 + area * 31) * 2654435761u) >> 24;
      int r = (int)(h & 15);
      int fl = season == GQ_SPRING ? 4 : (season == GQ_SUMMER ? 2 : 0);
      int tu = season == GQ_SPRING ? 8 : (season == GQ_SUMMER ? 6 : 5);
      if (r < fl) g_ground[y][x] = T_FLOWER;
      else if (r < tu) g_ground[y][x] = T_TUFT;
    }
  apply_dyn();
  recompute_walk();
  s_rows_water = s_rows_fire = s_rows_bell = 0;
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      uint8_t g = g_ground[y][x], o = g_obj[y][x];
      if (g == T_WATER || g == WT_WAVE || o == WT_FOUNTAIN) s_rows_water |= 1u << y;
      if (o == T_FIRE || o == WT_HEARTH || o == WT_TORCH) s_rows_fire |= 1u << y;
      if (o == WT_BELL_TOP) s_rows_bell |= 1u << y;
    }
}

int world_spot(int spot, int* tx, int* ty) {
  if (spot < 0 || spot >= SP_COUNT || s_spot[spot] == 0xFF) return 0;
  if (tx) *tx = TX(s_spot[spot]);
  if (ty) *ty = TY(s_spot[spot]);
  return 1;
}

int world_find_exit(int target_area, int* tx, int* ty, int* arg) {
  for (int i = 0; i < s_nexit; i++)
    if (s_exit[i].area == target_area) {
      if (tx) *tx = TX(s_exit[i].tile);
      if (ty) *ty = TY(s_exit[i].tile);
      if (arg) *arg = s_exit[i].spot;
      return 1;
    }
  return 0;
}

int world_area(void) { return s_area; }

int world_area_open(int area) {
  switch (area) {
    case GQ_AREA_SHORE: return econ_project_built(PRJ_BRIDGE);
    case GQ_AREA_CAVE: return econ_tool(GQ_TOOL_PICK) >= 1;
    case GQ_AREA_RUINS: return econ_tool(GQ_TOOL_LANTERN) >= 1;
    default: return area >= 0 && area < GQ_AREA_COUNT;
  }
}

// first step of the shortest route between two areas (BFS over the 8-node graph)
int world_next_hop(int from, int to) {
  static const uint8_t ADJ[GQ_AREA_COUNT] = {
    0xEE /*village: forest shore cave home shop tavern*/, 0x11 /*forest: village ruins*/, 0x01, 0x01, 0x02, 0x01, 0x01, 0x01 };
  // ADJ bits: bit n = area n. village: 1,2,3,5,6,7 = 0xEE. forest: 0,4 = 0x11. ruins: forest = 0x02.
  if (from == to || from < 0 || to < 0 || from >= GQ_AREA_COUNT || to >= GQ_AREA_COUNT) return -1;
  int prev[GQ_AREA_COUNT], q[GQ_AREA_COUNT], qh = 0, qt = 0;
  for (int i = 0; i < GQ_AREA_COUNT; i++) prev[i] = -1;
  prev[from] = from;
  q[qt++] = from;
  while (qh < qt) {
    int a = q[qh++];
    for (int b = 0; b < GQ_AREA_COUNT; b++)
      if ((ADJ[a] >> b) & 1 && prev[b] < 0) { prev[b] = a; q[qt++] = b; }
  }
  if (prev[to] < 0) return -1;
  int c = to;
  for (int i = 0; i < GQ_AREA_COUNT && prev[c] != from; i++) c = prev[c];
  return c;
}

// ------------------------------------------------------- dynamic objects
void world_obj_change(int tx, int ty, int now, int regrow_s) {
  if (tx < 0 || ty < 0 || tx >= GC_COLS || ty >= GC_ROWS) return;
  uint8_t t = TI(tx, ty);
  int slot = -1, lowest = 0;
  for (int i = 0; i < 16; i++) {
    if (s_dyn[i].timer && s_dyn[i].area == s_area && s_dyn[i].tile == t) { slot = i; break; }
  }
  uint8_t orig = g_obj[ty][tx];
  if (slot < 0) {
    for (int i = 0; i < 16; i++) if (!s_dyn[i].timer) { slot = i; break; }
    if (slot < 0) {   // table full: the entry that is closest to growing back grows back now
      for (int i = 1; i < 16; i++) if (s_dyn[i].timer < s_dyn[lowest].timer) lowest = i;
      if (s_dyn[lowest].area == s_area) g_obj[TY(s_dyn[lowest].tile)][TX(s_dyn[lowest].tile)] = s_dyn[lowest].orig;
      slot = lowest;
    }
    s_dyn[slot].orig = orig;
  }
  s_dyn[slot].area = s_area; s_dyn[slot].tile = t; s_dyn[slot].now = (uint8_t)now;
  s_dyn[slot].timer = (uint16_t)(regrow_s < 1 ? 1 : (regrow_s > 60000 ? 60000 : regrow_s));
  g_obj[ty][tx] = (uint8_t)now;
  recompute_walk();
  mark_rows(ty * 16, 16);
}

static int occupied(int x, int y) {
  for (int i = 0; i < A_COUNT; i++)
    if (A[i].kind != AK_NONE && A[i].visible && tile_x(&A[i]) == x && tile_y(&A[i]) == y) return 1;
  return 0;
}

static void dyn_tick(void) {
  for (int i = 0; i < 16; i++) {
    Dyn* d = &s_dyn[i];
    if (!d->timer || --d->timer) continue;
    int x = TX(d->tile), y = TY(d->tile);
    uint8_t next = d->orig;
    int again = 0;
    if (d->now == WT_CROP_0) { next = WT_CROP_1; again = 1; }
    if (d->area == s_area && !tile_walk(g_ground[y][x], (uint8_t)next) && occupied(x, y)) { d->timer = 5; continue; }
    d->now = next;
    if (again) d->timer = 300;
    if (d->area == s_area) { g_obj[y][x] = next; recompute_walk(); mark_rows(y * 16, 16); }
  }
}

// make sure nobody stands on a tile that is blocked now (map rebuilt, project built)
static void fix_actors(void) {
  for (int i = 0; i < A_COUNT; i++) {
    Actor* a = &A[i];
    if (a->kind == AK_NONE || !a->visible) continue;
    int x = tile_x(a), y = tile_y(a);
    if (walkable(x, y) && aligned(a)) continue;
    if (walkable(x, y)) continue;
    int done = 0;
    for (int r = 1; r < GC_COLS && !done; r++)
      for (int dy = -r; dy <= r && !done; dy++)
        for (int dx = -r; dx <= r && !done; dx++) {
          if (dx != -r && dx != r && dy != -r && dy != r) continue;
          if (walkable(x + dx, y + dy)) { place(a, x + dx, y + dy); done = 1; }
        }
    if (!done) a->visible = 0;
  }
}

// --------------------------------------------------------------- area change
static void enter_area(int area, int spot, int show) {
  world_build_area(area);
  for (int i = 1; i < A_COUNT; i++) actor_clear(&A[i]);
  Actor* h = &A[A_HERO];
  int tx = 0, ty = 0;
  if (!world_spot(spot, &tx, &ty) && !world_spot(SP_ENTRY, &tx, &ty)) {
    for (int y = 0; y < GC_ROWS; y++) for (int x = 0; x < GC_COLS; x++) if (walkable(x, y)) { tx = x; ty = y; y = GC_ROWS; break; }
  }
  place(h, tx, ty);
  h->dir = DIR_DOWN;
  if (show) {
    if (!h->visible) { h->visible = 1; h->emote = I_NONE; h->ex = h->ey = -1; }
  } else {   // night camera: the hero sleeps indoors, a Zzz sits above his door
    int dx = 2, dy = 3;
    world_find_exit(GQ_AREA_HOME, &dx, &dy, 0);
    h->visible = 0;
    h->emote = I_ZZZ;
    h->ex = (int16_t)(dx * 16);
    h->ey = (int16_t)(dy * 16 - 14);
  }
  npc_populate();
  gc_mark_all_dirty();
}

void world_instant_area(int area, int spot) {
  s_tr.st = TR_IDLE;
  enter_area(area, spot, 1);
}

int world_change_area(int area, int spot, int show_hero) {
  if (s_tr.st != TR_IDLE || area < 0 || area >= GQ_AREA_COUNT) return 0;
  s_tr.st = TR_CLOSE; s_tr.area = (uint8_t)area; s_tr.spot = (uint8_t)spot; s_tr.show = (uint8_t)show_hero; s_tr.t = 0;
  ui_curtain(1);
  return 1;
}

int world_transition_busy(void) { return s_tr.st != TR_IDLE; }

static void transition_tick(uint32_t dt) {
  if (s_tr.st == TR_IDLE) return;
  s_tr.t = (uint16_t)(s_tr.t + (dt > 1000 ? 1000 : dt));
  if (s_tr.st == TR_CLOSE) {
    if (ui_curtain_closed() || s_tr.t >= 450) {     // the timer is for a curtain that never reports closed
      enter_area(s_tr.area, s_tr.spot, s_tr.show);
      ui_curtain(0);
      s_tr.st = TR_OPEN;
      s_tr.t = 0;
    }
  } else if (s_tr.t >= 330) {
    s_tr.st = TR_IDLE;
    gq_emit(GQ_EV_AREA, s_area, 0);
  }
}

void world_chime(void) { s_bell_ms = 6000; }

// ------------------------------------------------------ sprite composition
static int actor_ops(int i, Op* ops) {
  const Actor* a = &A[i];
  int n = 0;
  static const uint8_t cycle[4] = {0, 1, 0, 2};
  if (a->kind == AK_NONE) return 0;
  if (a->visible) {
    int row = a->dir == DIR_RIGHT ? (int)DIR_LEFT : (int)a->dir;
    int flip = a->dir == DIR_RIGHT;
    if (a->kind == AK_HERO || a->kind == AK_VILLAGER) {
      int cell = HERO_CELL_BASE[row] + cycle[a->step];
      switch (a->pose) {
        case POSE_BED: cell = WC_BED; break;
        case POSE_SIT: cell = WC_SIT; break;
        case POSE_SWING: cell = a->dir == DIR_DOWN ? WC_SWING_DOWN : (a->dir == DIR_UP ? WC_SWING_UP : WC_SWING_LEFT); break;
        case POSE_HURT: cell = WC_HURT; break;
        case POSE_CHEER: cell = WC_CHEER; break;
        default: break;
      }
      int item_behind = a->dir == DIR_UP && a->pose == POSE_NORMAL;
      Op body = {a->x, a->y, SH_CHARS, (uint8_t)cell, (uint8_t)flip, (uint8_t)i, 0, (uint8_t)(a->kind == AK_VILLAGER ? a->id : 0)};
      Op item = {0, 0, SH_ITEMS, 0, 0, (uint8_t)i, 0, 0};
      int has_item = a->item != I_NONE;
      if (has_item) {
        item.cell = (uint8_t)a->item;
        if (a->item == I_LOG) { item.x = a->x; item.y = (int16_t)(a->y - 9); item_behind = 0; }
        else if (a->item == I_ROD) {
          item.flip = (uint8_t)(a->dir == DIR_LEFT);
          item.x = (int16_t)(a->x + (a->dir == DIR_LEFT ? -10 : 10));
          item.y = (int16_t)(a->y - 6);
        } else {  // axe, pick, hammer
          int dx = a->dir == DIR_LEFT ? -9 : (a->dir == DIR_RIGHT ? 9 : 6);
          item.flip = (uint8_t)(a->dir == DIR_LEFT);
          item.x = (int16_t)(a->x + dx);
          item.y = (int16_t)(a->y + (a->dir == DIR_DOWN ? 2 : -2));
        }
      }
      if (has_item && item_behind) ops[n++] = item;
      ops[n++] = body;
      if (a->kind == AK_VILLAGER && a->id >= 1 && a->id <= 8) {
        int hd = a->dir == DIR_DOWN ? 0 : (a->dir == DIR_UP ? 1 : 2);
        Op head = {a->x, a->y, SH_CHARS, (uint8_t)(WC_HEAD + (a->id - 1) * 3 + hd), (uint8_t)flip, (uint8_t)i, 0, 0};
        ops[n++] = head;
      }
      if (has_item && !item_behind) ops[n++] = item;
      if (a->item == I_ROD) {
        Op bob = {(int16_t)(a->x + 26), (int16_t)(a->y + 6 + a->bob), SH_ITEMS, I_BOBBER, 0, (uint8_t)i, 0, 0};
        ops[n++] = bob;
      }
    } else {
      int cell = 0, fl = flip;
      if (a->kind == AK_CRITTER) {
        static const uint8_t base[5] = {WC_CRIT, WC_CRIT + 2, WC_CRIT + 4, WC_CRIT + 6, WC_CRIT + 7};
        int t = a->id < 5 ? a->id : 0;
        cell = base[t] + (t < 3 ? (a->anim & 1) : 0);
      } else if (a->kind == AK_CAT) {
        cell = a->pose == POSE_BED ? WC_CAT + 3 : (a->dir == DIR_DOWN || a->dir == DIR_UP ? WC_CAT : WC_CAT + 1 + (a->step & 1));
      } else {   // monster
        if (a->id == GQ_MON_BOSS) cell = a->pose == POSE_HURT ? WC_BOSS + 2 : WC_BOSS + (a->anim & 1);
        else cell = WC_MON + (a->id < 4 ? a->id : 0) * 2 + (a->anim & 1);
        fl = (a->anim >> 1) & 1;   // monsters face either way, for life
      }
      Op body = {a->x, a->y, SH_CHARS, (uint8_t)cell, (uint8_t)fl, (uint8_t)i, 0, 0};
      ops[n++] = body;
    }
  }
  if (a->kind == AK_HERO && a->visible && a->pose == POSE_BED) {
    // R5: "Zzz Zzz Zzz" drifting up from the pillow while the hero sleeps with the screen on (3 copies, each starts again below the head)
    for (int k = 0; k < 3; k++) {
      int f = (s_zzz_frame + k * 2) % 6;
      Op z = {(int16_t)(a->x + 4 + k * 9), (int16_t)(a->y - 6 - f * 3), SH_ITEMS, (uint8_t)I_ZZZ, 0, (uint8_t)i, 1, 0};
      ops[n++] = z;
    }
    return n;
  }
  if (a->emote != I_NONE) {
    Op e = {a->ex >= 0 ? a->ex : a->x, a->ex >= 0 ? a->ey : (int16_t)(a->y - 16), SH_ITEMS, (uint8_t)a->emote, 0, (uint8_t)i, 1, 0};
    ops[n++] = e;
  }
  return n;
}

static void rebuild_ops(void) {
  Op tmp[6];
  s_nops = 0;
  for (int i = 0; i < A_COUNT; i++) {
    int n = actor_ops(i, tmp);
    uint32_t h = 2166136261u;
    int miny = 9999, maxy = -9999;
    for (int k = 0; k < n; k++) {
      h = (h ^ (uint32_t)(uint16_t)tmp[k].x) * 16777619u;
      h = (h ^ (uint32_t)(uint16_t)tmp[k].y) * 16777619u;
      h = (h ^ (uint32_t)(tmp[k].sheet << 16 | tmp[k].cell << 8 | tmp[k].flip)) * 16777619u;
      h = (h ^ tmp[k].remap) * 16777619u;
      if (tmp[k].y < miny) miny = tmp[k].y;
      if (tmp[k].y + 16 > maxy) maxy = tmp[k].y + 16;
      if (s_nops < 48) s_ops[s_nops++] = tmp[k];
    }
    h ^= (uint32_t)n;
    if (h != A[i].sig) {
      mark_rows(A[i].box_y, A[i].box_h);
      if (n) mark_rows(miny, maxy - miny);
      A[i].sig = h;
      A[i].box_y = (int16_t)(n ? miny : 0);
      A[i].box_h = (int16_t)(n ? maxy - miny : 0);
    }
  }
  for (int i = 1; i < s_nops; i++) {          // stable insertion sort by (top, y)
    Op k = s_ops[i];
    int j = i - 1;
    while (j >= 0 && (s_ops[j].top > k.top || (s_ops[j].top == k.top && s_ops[j].y > k.y))) { s_ops[j + 1] = s_ops[j]; j--; }
    s_ops[j + 1] = k;
  }
}

// ------------------------------------------------------------------ render
static uint8_t anim_tile(uint8_t t) {
  switch (t) {
    case T_WATER: return s_water_frame ? (uint8_t)T_WATER2 : t;
    case WT_WAVE: return s_water_frame ? (uint8_t)WT_WAVE2 : t;
    case WT_FOUNTAIN: return s_water_frame ? (uint8_t)WT_FOUNTAIN2 : t;
    case T_FIRE: return s_fire_frame ? (uint8_t)T_FIRE2 : t;
    case WT_HEARTH: return s_fire_frame ? (uint8_t)WT_HEARTH2 : t;
    case WT_TORCH: return s_fire_frame ? (uint8_t)WT_TORCH2 : t;
    case WT_BELL_TOP: return s_bell_frame ? (uint8_t)WT_BELL_TOP2 : t;
    case WT_LANTERN_POST: return cal_is_night() ? (uint8_t)WT_LANTERN_LIT : t;
    case WT_WALL_IN_WINDOW: return (s_area == GQ_AREA_HOME && cal_is_night()) ? (uint8_t)WT_WINDOW_MOON : t;
    default: return t;
  }
}

static void render_row(uint16_t* out, int s) {
  for (int tx = 0; tx < GC_COLS; tx++) {
    uint8_t g = g_ground[s][tx];
    if (g == WT_VOID) {
      for (int r = 0; r < 16; r++) for (int c = 0; c < 16; c++) out[r * GC_W + tx * 16 + c] = 0;
      continue;
    }
    blit_tile(out, tx * GC_TILE, GC_TILES(anim_tile(g)), 0);
    uint8_t o = g_obj[s][tx];
    if (o != T_NONE) blit_tile(out, tx * GC_TILE, GC_TILES(anim_tile(o)), 1);
  }
}

void world_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  render_row(out, s);
  for (int i = 0; i < s_nops; i++) {
    const Op& o = s_ops[i];
    if (o.y >= y0 + GC_STRIP_H || o.y + 16 <= y0) continue;
    uint8_t rm[16];
    const uint8_t* remap = 0;
    if (o.remap && o.remap < 9) {
      for (int k = 0; k < 16; k++) rm[k] = GC_RD8(&NPC_REMAP[o.remap][k]);
      remap = rm;
    }
    blit_sprite(out, y0, o.x, o.y, sheet_cell(o.sheet, o.cell), o.flip, remap, g_act);
  }
}

// ------------------------------------------------------------ init and tick
void world_init(void) {
  for (int i = 0; i < 16; i++) s_dyn[i].timer = 0;
  s_tr.st = TR_IDLE;
  s_resync = s_rebuild = 0;
  s_bell_ms = 0;
  s_water_frame = s_fire_frame = s_bell_frame = 0;
  s_water_acc = s_fire_acc = s_bell_acc = s_dyn_acc = s_idle_acc = 0;
  for (int i = 0; i < A_COUNT; i++) { actor_clear(&A[i]); }
  Actor* h = &A[A_HERO];
  h->kind = AK_HERO;
  h->visible = 1;
  h->ms_per_px = 24;
  enter_area(GQ_AREA_VILLAGE, SP_ENTRY, 1);
  hero_reset();
  rebuild_ops();
}

static void rebuild_current(void) {
  world_build_area(s_area);
  fix_actors();
  gc_mark_all_dirty();
}

void world_tick(uint32_t dt) {
  transition_tick(dt);

  // animated tiles
  s_water_acc += dt;
  if (s_water_acc >= 650) { s_water_acc -= 650; s_water_frame ^= 1; g_dirty |= s_rows_water; if (s_rows_water) s_idle_acc = 0; }
  s_fire_acc += dt;
  if (s_fire_acc >= 220) { s_fire_acc -= 220; s_fire_frame ^= 1; g_dirty |= s_rows_fire; if (s_rows_fire) s_idle_acc = 0; }
  if (s_bell_ms > 0) {
    s_bell_ms -= (int32_t)dt;
    s_bell_acc += dt;
    if (s_bell_acc >= 300) { s_bell_acc = 0; s_bell_frame ^= 1; g_dirty |= s_rows_bell; }
    if (s_bell_ms <= 0 && s_bell_frame) { s_bell_frame = 0; g_dirty |= s_rows_bell; }
  }
  // R5: the hero's Zzz while he lies in bed
  if (A[A_HERO].visible && A[A_HERO].pose == POSE_BED) {
    s_zzz_acc += dt;
    if (s_zzz_acc >= 450) { s_zzz_acc -= 450; s_zzz_frame = (uint8_t)((s_zzz_frame + 1) % 6); s_idle_acc = 0; }
  } else s_zzz_acc = 0;
  // objects that grow back (once a second)
  s_dyn_acc += dt;
  if (s_dyn_acc >= 1000) { s_dyn_acc -= 1000; if (s_dyn_acc > 1000) s_dyn_acc = 0; dyn_tick(); }
  // liveness: something on screen changes at least every 1.5 s even in a still room
  s_idle_acc += dt;
  if (s_idle_acc >= 1500) { s_idle_acc = 0; mark_rows(A[A_HERO].y, 16); }

  if (s_rebuild) { s_rebuild = 0; rebuild_current(); }
  if (s_resync && s_tr.st == TR_IDLE) { s_resync = 0; npc_sync(); }

  A[A_HERO].ms_per_px = econ_tool(GQ_TOOL_BOOTS) >= 1 ? 18 : 24;
  hero_tick(&A[A_HERO], dt);
  for (int i = 1; i < A_COUNT; i++) if (A[i].kind != AK_NONE) npc_tick(i, dt);
  for (int i = 0; i < A_COUNT; i++) if (A[i].kind != AK_NONE) move_actor(&A[i], dt);
  rebuild_ops();
  hero_update_presence();
}

void world_on_event(int ev, int a, int b) {
  (void)b;
  switch (ev) {
    case GQ_EV_HOUR:
      s_resync = 1;
      if (econ_project_built(PRJ_BELL)) world_chime();
      break;
    case GQ_EV_NEW_DAY: case GQ_EV_BUILT: case GQ_EV_FESTIVAL: s_rebuild = 1; s_resync = 1; break;
    default: (void)a; break;
  }
}

int world_actor_count(void) {
  int n = 0;
  for (int i = 0; i < A_COUNT; i++)
    if (A[i].kind != AK_NONE && A[i].visible && A[i].x > -16 && A[i].x < GC_W) n++;
  return n;
}
