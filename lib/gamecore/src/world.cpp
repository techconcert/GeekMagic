// World: map, walk grid, path finding, actors, sprite list, dirty tracking.
// (Split out of village.cpp; chunk 3 grows this into areas and interiors.)
#include "world_priv.h"

Actor A[A_COUNT];

// palette remaps: hero sprite recoloured for villagers
// palette: 0 . 1 k 2 g 3 G 4 l 5 b 6 B 7 s 8 w 9 W 10 f 11 p 12 r 13 o 14 a 15 Y
static const uint8_t REMAP[A_COUNT][16] = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},     // hero: blue tunic, brown hair
  {0, 1, 2, 3, 4, 6, 15, 7, 8, 12, 10, 11, 12, 13, 14, 15},   // Mira: red dress, blonde
  {0, 1, 2, 3, 4, 14, 13, 7, 8, 2, 10, 11, 12, 13, 14, 15},   // Bram: green coat, white hair
  {0, 1, 2, 3, 4, 1, 5, 7, 8, 15, 10, 11, 12, 13, 14, 6},     // Pip: yellow shirt, dark hair
};

// ---------------------------------------------------------------- world state
static uint8_t  g_walk[GC_ROWS][GC_COLS];
static uint32_t g_anim_rows_water, g_anim_rows_fire;
static uint8_t  g_water_frame, g_fire_frame;
static uint32_t g_water_acc, g_fire_acc;
static uint32_t g_regrow_acc;

// ------------------------------------------------------------------ helpers
int rnd_range(int lo, int hi) { return lo + (int)(gc_rnd() % (uint32_t)(hi - lo + 1)); }

void recompute_walk(void) {
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      uint8_t o = g_obj[y][x];
      int w = g_ground[y][x] != T_WATER;
      if (o == T_DOCK) w = 1;
      else if (o == T_BENCH) w = 1;
      else if (o != T_NONE) w = 0;
      g_walk[y][x] = (uint8_t)w;
    }
}

int walkable(int x, int y) {
  return x >= 0 && y >= 0 && x < GC_COLS && y < GC_ROWS && g_walk[y][x];
}

int aligned(const Actor* a) { return (a->x & 15) == 0 && (a->y & 15) == 0; }
int tile_x(const Actor* a) { return (a->x + 8) >> 4; }
int tile_y(const Actor* a) { return (a->y + 8) >> 4; }

// BFS on the 15x15 grid (uniform cost: same result as A*).
int find_path(Actor* a, int gx, int gy) {
  a->plen = a->pidx = 0;
  if (!walkable(gx, gy)) return 0;
  int sx = tile_x(a), sy = tile_y(a);
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
  for (uint8_t t = goal; t != start; t = prev[t]) len++;
  if (len > 64) return 0;
  int i = len;
  for (uint8_t t = goal; t != start; t = prev[t]) a->path[--i] = t;
  a->plen = (uint8_t)len;
  return 1;
}

int arrived(const Actor* a) { return a->pidx >= a->plen && aligned(a) && !a->manual_dx; }

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
}

static void move_actor(Actor* a, uint32_t dt) {
  a->move_acc = (uint16_t)(a->move_acc + dt);
  while (a->move_acc >= a->ms_per_px) {
    a->move_acc = (uint16_t)(a->move_acc - a->ms_per_px);
    int moving = 0;
    if (a->manual_dx) {
      a->x = (int16_t)(a->x + a->manual_dx);
      a->dir = a->manual_dx > 0 ? DIR_RIGHT : DIR_LEFT;
      moving = 1;
    } else if (a->pidx < a->plen) {
      uint8_t t = a->path[a->pidx];
      int tx = TX(t) * 16, ty = TY(t) * 16;
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
static void put_house(int x, int roof) {
  for (int i = 0; i < 3; i++) g_obj[2][x + i] = (uint8_t)(roof + i);
  g_obj[3][x] = T_WALL_WIN_L;
  g_obj[3][x + 1] = T_WALL_DOOR;
  g_obj[3][x + 2] = T_WALL_WIN_R;
}

static void build_village() {
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      uint32_t r = gc_rnd() % 12;
      g_ground[y][x] = r < 7 ? T_GRASS : (r < 10 ? T_TUFT : T_FLOWER);
      g_obj[y][x] = T_NONE;
    }
  // border trees along the top and bottom
  for (int x = 0; x < GC_COLS; x++) {
    if (x != 4 && x != 9) g_obj[0][x] = T_TREE;
    if (x < 5 || x > 13) g_obj[14][x] = T_TREE;
  }
  put_house(1, T_ROOF_RED);
  put_house(6, T_ROOF_BLUE);
  put_house(11, T_ROOF_BROWN);
  g_obj[3][9] = T_SIGN_SHOP;
  g_obj[3][10] = T_SIGN_TAVERN;
  g_obj[3][0] = T_LOGPILE;
  g_obj[1][5] = T_TREE;
  // main street + exit
  for (int x = 0; x < GC_COLS; x++) g_ground[STREET_Y][x] = T_PATH;
  // lane down to the plaza and the dock
  for (int y = 5; y <= 9; y++) g_ground[y][7] = T_PATH;
  for (int y = 6; y <= 8; y++)
    for (int x = 5; x <= 9; x++) g_ground[y][x] = T_PATH;
  for (int x = 7; x <= 9; x++) g_ground[9][x] = T_PATH;
  g_ground[10][9] = T_PATH;
  g_obj[7][7] = T_FIRE;
  g_obj[BENCH_Y][BENCH_A_X] = T_BENCH;
  g_obj[BENCH_Y][BENCH_B_X] = T_BENCH;
  // garden fence
  for (int x = 0; x <= 3; x++) g_obj[6][x] = T_FENCE;
  // forest, bottom left
  static const uint8_t trees[][2] = {
    {9, 0}, {9, 3}, {11, 1}, {11, 4}, {12, 0}, {13, 2}, {13, 4}, {10, 2}, {7, 12}, {6, 13}, {1, 9}, {1, 12}
  };
  for (unsigned i = 0; i < sizeof(trees) / sizeof(trees[0]); i++) g_obj[trees[i][0]][trees[i][1]] = T_TREE;
  g_obj[12][6] = T_ROCK;
  g_obj[8][12] = T_ROCK;
  // pond, bottom right, with a dock at column 9
  for (int y = 11; y <= 13; y++)
    for (int x = 9; x <= 13; x++) g_ground[y][x] = T_WATER;
  for (int x = 10; x <= 13; x++) g_ground[10][x] = T_WATER;
  g_obj[DOCK_Y][DOCK_X] = T_DOCK;
  g_obj[12][9] = T_DOCK;

  g_anim_rows_water = g_anim_rows_fire = 0;
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      if (g_ground[y][x] == T_WATER) g_anim_rows_water |= 1u << y;
      if (g_obj[y][x] == T_FIRE) g_anim_rows_fire |= 1u << y;
    }
  recompute_walk();
}

// ------------------------------------------------------ sprite composition
struct Op { int16_t x, y; uint8_t sheet, cell, flip, actor, top; };

static int actor_ops(int i, Op* ops) {
  const Actor* a = &A[i];
  int n = 0;
  static const uint8_t cycle[4] = {0, 1, 0, 2};
  if (a->visible) {
    int row = a->dir == DIR_RIGHT ? (int)DIR_LEFT : (int)a->dir;
    int flip = a->dir == DIR_RIGHT;
    int item_behind = a->dir == DIR_UP;
    Op body = {a->x, a->y, SH_CHARS, (uint8_t)(HERO_CELL_BASE[row] + cycle[a->step]), (uint8_t)flip, (uint8_t)i, 0};
    Op item = {0, 0, SH_ITEMS, 0, 0, (uint8_t)i, 0};
    int has_item = a->item != I_NONE;
    if (has_item) {
      item.cell = (uint8_t)a->item;
      if (a->item == I_LOG) { item.x = a->x; item.y = (int16_t)(a->y - 9); item_behind = 0; }
      else if (a->item == I_ROD) {
        item.flip = (uint8_t)(a->dir == DIR_LEFT);
        item.x = (int16_t)(a->x + (a->dir == DIR_LEFT ? -10 : 10));
        item.y = (int16_t)(a->y - 6);
      } else {  // axe
        int dx = a->dir == DIR_LEFT ? -9 : (a->dir == DIR_RIGHT ? 9 : 6);
        item.flip = (uint8_t)(a->dir == DIR_LEFT);
        item.x = (int16_t)(a->x + dx);
        item.y = (int16_t)(a->y + (a->dir == DIR_DOWN ? 2 : -2));
      }
    }
    if (has_item && item_behind) ops[n++] = item;
    ops[n++] = body;
    if (has_item && !item_behind) ops[n++] = item;
    if (a->item == I_ROD) {
      Op bob = {(int16_t)(a->x + 26), (int16_t)(a->y + 6 + a->bob), SH_ITEMS, I_BOBBER, 0, (uint8_t)i, 0};
      ops[n++] = bob;
    }
  }
  if (a->emote != I_NONE) {
    Op e = {a->ex >= 0 ? a->ex : a->x, a->ex >= 0 ? a->ey : (int16_t)(a->y - 16), SH_ITEMS, (uint8_t)a->emote, 0, (uint8_t)i, 1};
    ops[n++] = e;
  }
  return n;
}

static void track_dirty() {
  Op ops[6];
  for (int i = 0; i < A_COUNT; i++) {
    int n = actor_ops(i, ops);
    uint32_t h = 2166136261u;
    int miny = 9999, maxy = -9999;
    for (int k = 0; k < n; k++) {
      h = (h ^ (uint32_t)(uint16_t)ops[k].x) * 16777619u;
      h = (h ^ (uint32_t)(uint16_t)ops[k].y) * 16777619u;
      h = (h ^ (uint32_t)(ops[k].sheet << 16 | ops[k].cell << 8 | ops[k].flip)) * 16777619u;
      if (ops[k].y < miny) miny = ops[k].y;
      if (ops[k].y + 16 > maxy) maxy = ops[k].y + 16;
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
}

void world_init(void) {
  build_village();
  for (int i = 0; i < A_COUNT; i++) {
    Actor* a = &A[i];
    *a = Actor();
    a->visible = 1;
    a->item = a->emote = I_NONE;
    a->ex = a->ey = -1;
    a->dir = DIR_DOWN;
    a->ms_per_px = i == A_HERO ? 24 : 34;
    a->sig = 0;
  }
  place(&A[A_HERO], HOME_DOOR_X, STREET_Y);
  place(&A[A_MIRA], 8, 5);
  place(&A[A_BRAM], BENCH_B_X, BENCH_Y);
  place(&A[A_PIP], 6, 9);
  A[A_HERO].timer = 1500;
  for (int i = 1; i < A_COUNT; i++) A[i].timer = rnd_range(500, 3000);
  g_water_frame = g_fire_frame = 0;
  g_water_acc = g_fire_acc = g_regrow_acc = 0;
}

void world_tick(uint32_t dt) {
  // animated tiles
  g_water_acc += dt;
  if (g_water_acc >= 650) { g_water_acc -= 650; g_water_frame ^= 1; g_dirty |= g_anim_rows_water; }
  g_fire_acc += dt;
  if (g_fire_acc >= 220) { g_fire_acc -= 220; g_fire_frame ^= 1; g_dirty |= g_anim_rows_fire; }

  // stumps grow back (one every ~2 minutes)
  g_regrow_acc += dt;
  if (g_regrow_acc >= 120000) {
    g_regrow_acc = 0;
    for (int t = 0; t < 30; t++) {
      int x = rnd_range(0, GC_COLS - 1), y = rnd_range(5, GC_ROWS - 1);
      if (g_obj[y][x] != T_STUMP) continue;
      int occupied = 0;
      for (int i = 0; i < A_COUNT; i++) if (tile_x(&A[i]) == x && tile_y(&A[i]) == y) occupied = 1;
      if (occupied) continue;
      g_obj[y][x] = T_TREE;
      recompute_walk();
      g_dirty |= 1u << y;
      break;
    }
  }

  hero_tick(&A[A_HERO], dt);
  for (int i = 1; i < A_COUNT; i++) npc_tick(i, dt);
  for (int i = 0; i < A_COUNT; i++) move_actor(&A[i], dt);
  track_dirty();
}

void world_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  render_ground_row(out, s, g_water_frame, g_fire_frame);

  // sprites, sorted: body layer by y, emotes on top
  Op ops[A_COUNT * 6];
  int n = 0;
  for (int i = 0; i < A_COUNT; i++) n += actor_ops(i, ops + n);
  for (int i = 1; i < n; i++) {          // insertion sort by (top, y)
    Op k = ops[i];
    int j = i - 1;
    while (j >= 0 && (ops[j].top > k.top || (ops[j].top == k.top && ops[j].y > k.y))) { ops[j + 1] = ops[j]; j--; }
    ops[j + 1] = k;
  }
  for (int i = 0; i < n; i++) {
    const Op& o = ops[i];
    if (o.y >= y0 + GC_STRIP_H || o.y + 16 <= y0) continue;
    const uint8_t* remap = (o.sheet == SH_CHARS && o.actor != A_HERO) ? REMAP[o.actor] : 0;
    blit_sprite(out, y0, o.x, o.y, sheet_cell(o.sheet, o.cell), o.flip, remap);
  }
}
