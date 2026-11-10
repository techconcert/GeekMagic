// The village: a self-playing day in the life of a small adventurer.
// The hero picks activities by time of day (chop wood, fish, trade, chat,
// sit by the fire, go questing, sleep); villagers wander and chat back.
#include "gc_internal.h"

// ------------------------------------------------------------------ map
// Row/column reference (15x15 tiles of 16 px):
//   row 2-3  : home (red roof, c1-3), shop (blue, c6-8), tavern (brown, c11-13)
//   row 4    : main street, exit east at c14
//   rows 6-8 : plaza with campfire and benches
//   bottom-left forest, bottom-right pond with a dock
#define TI(x, y) ((uint8_t)((y) * GC_COLS + (x)))
#define TX(t) ((t) % GC_COLS)
#define TY(t) ((t) / GC_COLS)

static const uint8_t HOME_DOOR_X = 2, SHOP_DOOR_X = 7, TAVERN_DOOR_X = 12, DOOR_Y = 3;
static const uint8_t STREET_Y = 4;
static const uint8_t PILE_FRONT_X = 0;          // stand at (0,4) facing up to the woodpile at (0,3)
static const uint8_t DOCK_X = 9, DOCK_Y = 11;   // fishing spot, facing right
static const uint8_t BENCH_A_X = 6, BENCH_B_X = 8, BENCH_Y = 8;
static const uint8_t EXIT_X = 14;

// ---------------------------------------------------------------- actors
enum { A_HERO = 0, A_MIRA, A_BRAM, A_PIP, A_COUNT };

struct Actor {
  int16_t x, y;            // top-left pixel
  uint8_t dir, step, moved, visible;
  uint8_t ms_per_px;
  uint16_t move_acc;
  uint8_t path[64];
  uint8_t plen, pidx;
  int8_t item;             // I_* drawn in the hands, or I_NONE
  int8_t emote;            // I_* drawn above the head, or I_NONE
  int16_t ex, ey;          // emote position override (e.g. at a door), ex<0 = above head
  uint8_t bob;             // bobber offset while fishing
  int8_t manual_dx;        // walking off/onto the screen edge
  // ai
  uint8_t act, phase, talking, home;
  int32_t timer;           // ms
  uint8_t counter;
  // dirty tracking
  uint32_t sig;
  int16_t box_y, box_h;
};

static Actor A[A_COUNT];

// palette remaps: hero sprite recoloured for villagers
// palette: 0 . 1 k 2 g 3 G 4 l 5 b 6 B 7 s 8 w 9 W 10 f 11 p 12 r 13 o 14 a 15 Y
static const uint8_t REMAP[A_COUNT][16] = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},     // hero: blue tunic, brown hair
  {0, 1, 2, 3, 4, 6, 15, 7, 8, 12, 10, 11, 12, 13, 14, 15},   // Mira: red dress, blonde
  {0, 1, 2, 3, 4, 14, 13, 7, 8, 2, 10, 11, 12, 13, 14, 15},   // Bram: green coat, white hair
  {0, 1, 2, 3, 4, 1, 5, 7, 8, 15, 10, 11, 12, 13, 14, 6},     // Pip: yellow shirt, dark hair
};
static const char* const NAMES[A_COUNT] = {"HERO", "MIRA", "BRAM", "PIP"};

// ---------------------------------------------------------------- world state
static uint8_t  g_walk[GC_ROWS][GC_COLS];
static uint32_t g_anim_rows_water, g_anim_rows_fire;
static uint8_t  g_water_frame, g_fire_frame;
static uint32_t g_water_acc, g_fire_acc;
static int      g_wood, g_gold, g_fish;
static int      g_minute = 9 * 60;      // minute of day used by the village
static uint32_t g_minute_acc;
static int      g_tint_key = -1;
static uint32_t g_regrow_acc;
static uint32_t g_since_init;
static int      g_status_band;          // 1 = bottom band visible
static uint8_t  g_status_idx;
static uint32_t g_status_acc;

// dialogue box (bottom three strips)
static struct {
  uint8_t active;
  char name[8];
  char text[40];
  int len, shown;
  uint32_t acc;
  int32_t hold;
} D;

static const char* const LINES_MIRA[] = {
  "FRESH APPLES TODAY|WANT TO TRADE?", "WOOD SELLS WELL|THIS WEEK.",
  "THE ROAD EAST IS|FULL OF SLIMES!", "BRING ME FISH, I|PAY IN GOLD.",
};
static const char* const LINES_BRAM[] = {
  "IN MY DAY WE HAD|NO FANCY SWORDS.", "THE CAVES HUM AT|NIGHT. CAREFUL.",
  "SIT. REST YOUR|BONES A WHILE.", "THAT FIRE NEEDS|MORE LOGS, KID.",
};
static const char* const LINES_PIP[] = {
  "TEACH ME TO USE|A SWORD!", "I SAW A FROG AS|BIG AS A DOG!",
  "RACE YOU TO THE|POND!", "ARE YOU A REAL|HERO?",
};
static const char* const REPLIES[] = {
  "HA! MAYBE LATER.", "I'LL KEEP WATCH.", "SOUNDS GOOD!", "SURE THING.", "TELL ME MORE!",
};

// ------------------------------------------------------------------ helpers
static int rnd_range(int lo, int hi) { return lo + (int)(gc_rnd() % (uint32_t)(hi - lo + 1)); }

static int is_night() { return g_minute >= 22 * 60 || g_minute < 6 * 60; }

static void recompute_walk() {
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

static int walkable(int x, int y) {
  return x >= 0 && y >= 0 && x < GC_COLS && y < GC_ROWS && g_walk[y][x];
}

static int aligned(const Actor* a) { return (a->x & 15) == 0 && (a->y & 15) == 0; }
static int tile_x(const Actor* a) { return (a->x + 8) >> 4; }
static int tile_y(const Actor* a) { return (a->y + 8) >> 4; }

// BFS on the 15x15 grid (uniform cost: same result as A*).
static int find_path(Actor* a, int gx, int gy) {
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

static int arrived(const Actor* a) { return a->pidx >= a->plen && aligned(a) && !a->manual_dx; }

static void face(Actor* a, int tx, int ty) {
  int dx = tx - tile_x(a), dy = ty - tile_y(a);
  if (dx > 0) a->dir = DIR_RIGHT;
  else if (dx < 0) a->dir = DIR_LEFT;
  else if (dy > 0) a->dir = DIR_DOWN;
  else if (dy < 0) a->dir = DIR_UP;
}

static void place(Actor* a, int tx, int ty) {
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

static void say(int who, const char* text, int32_t hold_ms) {
  D.active = 1;
  str_copy(D.name, NAMES[who], 8);
  str_copy(D.text, text, 40);
  D.len = str_len(D.text);
  D.shown = 0;
  D.acc = 0;
  D.hold = hold_ms;
  mark_rows(192, 48);
}

static int dialogue_done() { return !D.active; }

static void dialogue_tick(uint32_t dt) {
  if (!D.active) return;
  if (D.shown < D.len) {
    D.acc += dt;
    while (D.acc >= 45 && D.shown < D.len) { D.acc -= 45; D.shown++; mark_rows(192, 48); }
    return;
  }
  D.hold -= (int32_t)dt;
  if (D.hold <= 0) { D.active = 0; mark_rows(192, 48); }
}

static void itoa_into(char* p, int v) {
  char tmp[8];
  int n = 0;
  if (v < 0) v = 0;
  do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 7);
  while (n) *p++ = tmp[--n];
  *p = 0;
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

// ------------------------------------------------------------ hero activities
enum {
  ACT_NONE = 0, ACT_WANDER, ACT_CHOP, ACT_FISH, ACT_SHOP, ACT_TAVERN, ACT_SIT,
  ACT_CHAT, ACT_QUEST, ACT_SLEEP
};

static void hide_at_door(Actor* a, int door_x) {
  a->visible = 0;
  place(a, door_x, STREET_Y);
}

static void hero_choose(Actor* h) {
  h->phase = 0;
  h->timer = 0;
  h->counter = 0;
  h->item = I_NONE;
  if (is_night()) { h->act = ACT_SLEEP; return; }
  int hr = g_minute / 60;
  // weights: wander, chop, fish, shop, tavern, sit, chat, quest
  int w[8] = {2, 0, 0, 0, 0, 0, 1, 0};
  int goods = g_wood + g_fish;
  if (hr < 10)      { w[1] = 5; w[3] = goods ? 2 : 0; w[6] = 3; }
  else if (hr < 14) { w[2] = 3; w[3] = goods > 2 ? 3 : 0; w[7] = 3; w[1] = 1; }
  else if (hr < 18) { w[2] = 4; w[1] = 3; w[7] = 2; w[3] = goods > 3 ? 2 : 0; }
  else              { w[4] = 3; w[5] = 4; w[6] = 2; w[3] = goods ? 1 : 0; }
  int total = 0;
  for (int i = 0; i < 8; i++) total += w[i];
  int r = (int)(gc_rnd() % (uint32_t)total);
  static const uint8_t acts[8] = {ACT_WANDER, ACT_CHOP, ACT_FISH, ACT_SHOP, ACT_TAVERN, ACT_SIT, ACT_CHAT, ACT_QUEST};
  for (int i = 0; i < 8; i++) {
    if (r < w[i]) { h->act = acts[i]; return; }
    r -= w[i];
  }
  h->act = ACT_WANDER;
}

static int pick_tree(int* sx, int* sy, int* tx, int* ty) {
  static const int8_t DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  for (int tries = 0; tries < 40; tries++) {
    int x = rnd_range(0, GC_COLS - 1), y = rnd_range(5, GC_ROWS - 2);
    if (g_obj[y][x] != T_TREE) continue;
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (walkable(nx, ny) && g_obj[ny][nx] == T_NONE) { *sx = nx; *sy = ny; *tx = x; *ty = y; return 1; }
    }
  }
  return 0;
}

static int pick_npc() {
  int start = rnd_range(1, A_COUNT - 1);
  for (int i = 0; i < A_COUNT - 1; i++) {
    int n = 1 + (start - 1 + i) % (A_COUNT - 1);
    if (A[n].visible && !A[n].talking) return n;
  }
  return 0;
}

// tile next to the NPC that the hero can reach, closest to the hero
static int chat_spot(int n, int* ox, int* oy) {
  static const int8_t DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  int nx = tile_x(&A[n]), ny = tile_y(&A[n]);
  int best = 999;
  for (int d = 0; d < 4; d++) {
    int x = nx + DX[d], y = ny + DY[d];
    if (!walkable(x, y)) continue;
    int dist = (x > tile_x(&A[0]) ? x - tile_x(&A[0]) : tile_x(&A[0]) - x) +
               (y > tile_y(&A[0]) ? y - tile_y(&A[0]) : tile_y(&A[0]) - y);
    if (dist < best) { best = dist; *ox = x; *oy = y; }
  }
  return best < 999;
}

static int s_chat_npc, s_tree_x, s_tree_y;

static void finish(Actor* h) { h->act = ACT_NONE; h->item = I_NONE; h->timer = rnd_range(800, 2500); }

static void hero_tick(Actor* h, uint32_t dt) {
  h->timer -= (int32_t)dt;
  switch (h->act) {
    case ACT_NONE:
      if (h->timer <= 0 && dialogue_done()) hero_choose(h);
      break;

    case ACT_WANDER:
      if (h->phase == 0) {
        for (int i = 0; i < 20; i++) {
          int x = tile_x(h) + rnd_range(-3, 3), y = tile_y(h) + rnd_range(-3, 3);
          if (y >= 1 && y <= 13 && find_path(h, x, y)) break;
        }
        h->phase = 1;
      } else if (arrived(h)) finish(h);
      break;

    case ACT_CHOP:
      if (h->phase == 0) {
        int sx, sy;
        if (!pick_tree(&sx, &sy, &s_tree_x, &s_tree_y) || !find_path(h, sx, sy)) { finish(h); break; }
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        if (g_obj[s_tree_y][s_tree_x] != T_TREE) { finish(h); break; }
        face(h, s_tree_x, s_tree_y);
        h->item = I_AXE_UP;
        h->counter = 0;
        h->timer = 380;
        h->phase = 2;
      } else if (h->phase == 2 && h->timer <= 0) {
        h->item = h->item == I_AXE_UP ? I_AXE_DOWN : I_AXE_UP;
        h->timer = 380;
        if (++h->counter >= 10) {
          g_obj[s_tree_y][s_tree_x] = T_STUMP;
          recompute_walk();
          g_dirty |= 1u << s_tree_y;
          h->item = I_LOG;
          if (!find_path(h, PILE_FRONT_X, STREET_Y)) { h->item = I_NONE; g_wood++; finish(h); break; }
          h->phase = 3;
        }
      } else if (h->phase == 3 && arrived(h)) {
        h->dir = DIR_UP;
        h->item = I_NONE;
        g_wood++;
        g_dirty |= 1u;
        h->emote = I_WOOD;
        h->timer = 1200;
        h->phase = 4;
      } else if (h->phase == 4 && h->timer <= 0) {
        h->emote = I_NONE;
        finish(h);
      }
      break;

    case ACT_FISH:
      if (h->phase == 0) {
        if (!find_path(h, DOCK_X, DOCK_Y)) { finish(h); break; }
        h->counter = (uint8_t)rnd_range(2, 3);
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        h->dir = DIR_RIGHT;
        h->item = I_ROD;
        h->timer = rnd_range(3500, 8000);
        h->phase = 2;
      } else if (h->phase == 2) {
        h->bob = (uint8_t)(((uint32_t)h->timer / 500) & 1);
        if (h->timer <= 0) {
          if (gc_rnd() % 10 < 6) { h->emote = I_FISH; g_fish++; }
          else h->emote = I_BUBBLE;
          h->timer = 1400;
          h->phase = 3;
        }
      } else if (h->phase == 3 && h->timer <= 0) {
        h->emote = I_NONE;
        if (--h->counter > 0) { h->timer = rnd_range(3000, 7000); h->phase = 2; }
        else finish(h);
      }
      break;

    case ACT_SHOP:
      if (h->phase == 0) {
        if (!find_path(h, SHOP_DOOR_X, STREET_Y)) { finish(h); break; }
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        h->dir = DIR_UP;
        h->visible = 0;
        h->timer = 3000;
        h->phase = 2;
      } else if (h->phase == 2 && h->timer <= 0) {
        h->visible = 1;
        h->dir = DIR_DOWN;
        g_gold += g_wood + g_fish * 3;
        g_wood = g_fish = 0;
        g_dirty |= 1u;
        h->emote = I_COIN;
        h->timer = 1500;
        h->phase = 3;
      } else if (h->phase == 3 && h->timer <= 0) {
        h->emote = I_NONE;
        finish(h);
      }
      break;

    case ACT_TAVERN:
      if (h->phase == 0) {
        if (!find_path(h, TAVERN_DOOR_X, STREET_Y)) { finish(h); break; }
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        h->dir = DIR_UP;
        h->visible = 0;
        h->timer = rnd_range(15000, 30000);
        h->phase = 2;
      } else if (h->phase == 2 && h->timer <= 0) {
        h->visible = 1;
        h->dir = DIR_DOWN;
        if (g_gold > 0) { g_gold--; g_dirty |= 1u; }
        h->emote = I_HEART;
        h->timer = 1500;
        h->phase = 3;
      } else if (h->phase == 3 && h->timer <= 0) {
        h->emote = I_NONE;
        finish(h);
      }
      break;

    case ACT_SIT:
      if (h->phase == 0) {
        int bramOnB = A[A_BRAM].visible && tile_x(&A[A_BRAM]) == BENCH_B_X && tile_y(&A[A_BRAM]) == BENCH_Y;
        int bx = (bramOnB || (gc_rnd() & 1)) ? BENCH_A_X : BENCH_B_X;
        if (!find_path(h, bx, BENCH_Y)) { finish(h); break; }
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        h->dir = DIR_UP;
        h->timer = rnd_range(15000, 25000);
        h->phase = 2;
      } else if (h->phase == 2 && h->timer <= 0) {
        finish(h);
      }
      break;

    case ACT_CHAT:
      if (h->phase == 0) {
        s_chat_npc = pick_npc();
        if (!s_chat_npc) { finish(h); break; }
        A[s_chat_npc].talking = 1;
        h->phase = 1;
      } else if (h->phase == 1) {
        Actor* n = &A[s_chat_npc];
        if (!arrived(n)) break;  // let the villager finish their step
        int ox, oy;
        if (!n->visible || !chat_spot(s_chat_npc, &ox, &oy) || !find_path(h, ox, oy)) {
          n->talking = 0; finish(h); break;
        }
        h->phase = 2;
      } else if (h->phase == 2 && arrived(h)) {
        Actor* n = &A[s_chat_npc];
        face(h, tile_x(n), tile_y(n));
        face(n, tile_x(h), tile_y(h));
        const char* line;
        uint32_t k = gc_rnd() % 4;
        if (s_chat_npc == A_MIRA) line = LINES_MIRA[k];
        else if (s_chat_npc == A_BRAM) line = LINES_BRAM[k];
        else line = LINES_PIP[k];
        say(s_chat_npc, line, 2500);
        h->phase = 3;
      } else if (h->phase == 3 && dialogue_done()) {
        say(A_HERO, REPLIES[gc_rnd() % 5], 2000);
        h->phase = 4;
      } else if (h->phase == 4 && dialogue_done()) {
        A[s_chat_npc].talking = 0;
        if (gc_rnd() % 3 == 0) { h->emote = I_HEART; h->timer = 1200; h->phase = 5; }
        else finish(h);
      } else if (h->phase == 5 && h->timer <= 0) {
        h->emote = I_NONE;
        finish(h);
      }
      break;

    case ACT_QUEST:
      if (h->phase == 0) {
        if (!find_path(h, EXIT_X, STREET_Y)) { finish(h); break; }
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        h->manual_dx = 1;                   // walk off the east edge
        h->phase = 2;
      } else if (h->phase == 2 && h->x >= GC_W + 2) {
        h->manual_dx = 0;
        h->visible = 0;
        h->timer = rnd_range(40000, 90000);
        h->phase = 3;
      } else if (h->phase == 3 && h->timer <= 0) {
        h->visible = 1;
        h->manual_dx = -1;                  // walk back in
        h->phase = 4;
      } else if (h->phase == 4 && h->x <= EXIT_X * 16) {
        h->x = (int16_t)(EXIT_X * 16);
        h->manual_dx = 0;
        int reward = rnd_range(5, 20);
        g_gold += reward;
        g_dirty |= 1u;
        static const char* const PLACES[] = {"CAVES", "RUINS", "SWAMP", "TOWER"};
        char msg[40] = "BACK FROM THE|";
        int n = str_len(msg);
        const char* p = PLACES[gc_rnd() % 4];
        while (*p) msg[n++] = *p++;
        msg[n++] = '!'; msg[n++] = ' '; msg[n++] = '+';
        itoa_into(msg + n, reward);
        n = str_len(msg);
        str_copy(msg + n, " GOLD", 40 - n);
        h->dir = DIR_DOWN;
        say(A_HERO, msg, 3000);
        h->emote = I_HEART;
        h->phase = 5;
      } else if (h->phase == 5 && dialogue_done()) {
        h->emote = I_NONE;
        finish(h);
      }
      break;

    case ACT_SLEEP:
      if (h->phase == 0) {
        if (!find_path(h, HOME_DOOR_X, STREET_Y)) { finish(h); break; }
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        h->dir = DIR_UP;
        h->visible = 0;
        h->emote = I_ZZZ;
        h->ex = (int16_t)(HOME_DOOR_X * 16 + 4);
        h->ey = (int16_t)(DOOR_Y * 16 - 14);
        h->phase = 2;
      } else if (h->phase == 2 && !is_night()) {
        h->visible = 1;
        h->emote = I_NONE;
        h->ex = -1;
        h->dir = DIR_DOWN;
        finish(h);
      }
      break;
  }
}

// ----------------------------------------------------------- villager ai
static const uint8_t NPC_HOME[A_COUNT][2] = {{0, 0}, {8, 5}, {BENCH_B_X, BENCH_Y}, {7, 9}};
static const uint8_t NPC_DOOR[A_COUNT] = {0, SHOP_DOOR_X, TAVERN_DOOR_X, SHOP_DOOR_X};
static const uint8_t NPC_RADIUS[A_COUNT] = {0, 2, 1, 4};

static void npc_tick(int i, uint32_t dt) {
  Actor* n = &A[i];
  n->timer -= (int32_t)dt;
  if (n->talking) {   // stop at the next tile boundary
    if (aligned(n)) n->plen = n->pidx;
    else if (n->pidx < n->plen) n->plen = (uint8_t)(n->pidx + 1);
    return;
  }
  if (is_night()) {
    if (n->visible) {
      if (n->act != 9) { find_path(n, NPC_DOOR[i], STREET_Y); n->act = 9; }
      else if (arrived(n)) hide_at_door(n, NPC_DOOR[i]);
    }
    return;
  }
  if (!n->visible) {   // morning: come out
    place(n, NPC_DOOR[i], STREET_Y);
    n->visible = 1;
    n->dir = DIR_DOWN;
    n->act = 0;
    n->timer = rnd_range(1000, 4000);
    return;
  }
  if (n->act == 9) n->act = 0;
  if (!arrived(n) || n->timer > 0) return;
  // wander near home, or sit still (Bram likes his bench)
  if (i == A_BRAM && gc_rnd() % 3) {
    find_path(n, NPC_HOME[i][0], NPC_HOME[i][1]);
    n->timer = rnd_range(6000, 15000);
    return;
  }
  for (int t = 0; t < 12; t++) {
    int x = NPC_HOME[i][0] + rnd_range(-NPC_RADIUS[i], NPC_RADIUS[i]);
    int y = NPC_HOME[i][1] + rnd_range(-NPC_RADIUS[i], NPC_RADIUS[i]);
    if (y >= 4 && y <= 13 && find_path(n, x, y)) break;
  }
  n->timer = rnd_range(2500, 8000);
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
    Op body = {a->x, a->y, SH_HERO, (uint8_t)(row * GC_HERO_COLS + cycle[a->step]), (uint8_t)flip, (uint8_t)i, 0};
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

// ------------------------------------------------------------- day/night
static void update_tint() {
  // brightness keyframes (minute, r, g, b) - linear in between
  static const int16_t K[][4] = {
    {0, 90, 105, 180}, {300, 90, 105, 180}, {390, 220, 190, 200}, {450, 256, 256, 256},
    {1050, 256, 256, 256}, {1110, 256, 200, 160}, {1170, 170, 140, 170}, {1230, 90, 105, 180},
    {1440, 90, 105, 180},
  };
  int m = g_minute, i = 0;
  while (K[i + 1][0] < m) i++;
  int span = K[i + 1][0] - K[i][0];
  int f = span ? (m - K[i][0]) * 256 / span : 0;
  int r = K[i][1] + ((K[i + 1][1] - K[i][1]) * f >> 8);
  int g = K[i][2] + ((K[i + 1][2] - K[i][2]) * f >> 8);
  int b = K[i][3] + ((K[i + 1][3] - K[i][3]) * f >> 8);
  int key = (r >> 3) << 12 | (g >> 3) << 6 | (b >> 3);   // ignore tiny changes
  if (key == g_tint_key) return;
  g_tint_key = key;
  set_tint(r, g, b);
  gc_mark_all_dirty();
}

// ------------------------------------------------------------------ API
void village_status_changed() {
  if (g_status_band) g_dirty |= 1u << (GC_ROWS - 1);
}

void village_init(uint32_t seed) {
  (void)seed;
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
  g_wood = g_fish = 0;
  g_gold = 3;
  g_water_frame = g_fire_frame = 0;
  g_water_acc = g_fire_acc = g_minute_acc = g_regrow_acc = 0;
  g_since_init = 0;
  g_status_band = 1;
  g_status_idx = 0;
  g_status_acc = 0;
  g_tint_key = -1;
  D.active = 0;
  if (g_clock_valid) g_minute = g_hour * 60 + g_min;
  update_tint();
}

void village_tick(uint32_t dt) {
  g_since_init += dt;

  // time of day: wall clock when known, otherwise our own clock
  if (g_clock_valid) g_minute = g_hour * 60 + g_min;
  else {
    g_minute_acc += dt;
    while (g_minute_acc >= 60000) { g_minute_acc -= 60000; g_minute = (g_minute + 1) % 1440; g_dirty |= 1u; }
  }
  update_tint();

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
  dialogue_tick(dt);
  track_dirty();

  // bottom status band: first 90 s after boot, and whenever WiFi is down
  int want = g_since_init < 90000 || (g_status[0][0] == 'W');
  if (want != g_status_band) { g_status_band = want; g_dirty |= 1u << (GC_ROWS - 1); }
  if (g_status_band) {
    g_status_acc += dt;
    if (g_status_acc >= 3000) {
      g_status_acc = 0;
      for (int k = 0; k < 4; k++) {
        g_status_idx = (uint8_t)((g_status_idx + 1) % 4);
        if (g_status[g_status_idx][0]) break;
      }
      g_dirty |= 1u << (GC_ROWS - 1);
    }
  }
}

static void draw_hud(uint16_t* out, int y0) {
  darken_rect(out, y0, 0, 0, GC_W, 16);
  char buf[12];
  blit_sprite(out, y0, 0, 0, GC_ITEMS[I_WOOD], 0, 0);
  itoa_into(buf, g_wood);
  draw_text(out, y0, 17, 1, buf, g_ui[P_WHITE], 2, 0);
  blit_sprite(out, y0, 58, 0, GC_ITEMS[I_COIN], 0, 0);
  itoa_into(buf, g_gold);
  draw_text(out, y0, 74, 1, buf, g_ui[P_YELLOW], 2, 0);
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) {
    t[0] = (char)('0' + g_hour / 10); t[1] = (char)('0' + g_hour % 10);
    t[3] = (char)('0' + g_min / 10); t[4] = (char)('0' + g_min % 10);
  }
  draw_text(out, y0, GC_W - 5 * 12 - 2, 1, t, g_ui[P_WHITE], 2, 0);
}

static void draw_dialogue(uint16_t* out, int y0) {
  const int bx = 3, by = 193, bw = GC_W - 6, bh = 45;
  fill_rect(out, y0, bx, by, bw, bh, g_ui[P_WHITE]);
  fill_rect(out, y0, bx + 2, by + 2, bw - 4, bh - 4, g_ui[P_OUTLINE]);
  draw_text(out, y0, bx + 6, by + 4, D.name, g_ui[P_YELLOW], 1, 0);
  // text: up to two lines split at '|'
  int split = -1;
  for (int i = 0; i < D.len; i++) if (D.text[i] == '|') { split = i; break; }
  int l1 = split < 0 ? D.len : split;
  int shown1 = D.shown < l1 ? D.shown : l1;
  draw_text_n(out, y0, bx + 6, by + 13, D.text, shown1, g_ui[P_WHITE], 2);
  if (split >= 0 && D.shown > split + 1)
    draw_text_n(out, y0, bx + 6, by + 29, D.text + split + 1, D.shown - split - 1, g_ui[P_WHITE], 2);
}

static void draw_status_band(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, 16, g_ui[P_OUTLINE]);
  const char* s = g_status[g_status_idx];
  int x = (GC_W - str_len(s) * 12) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + 1, s, g_ui[P_WHITE], 2, 0);
}

void village_render(int s, uint16_t* out) {
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
    const uint8_t* remap = (o.sheet == SH_HERO && o.actor != A_HERO) ? REMAP[o.actor] : 0;
    blit_sprite(out, y0, o.x, o.y, sheet_cell(o.sheet, o.cell), o.flip, remap);
  }

  if (s == 0) draw_hud(out, y0);
  if (D.active && s >= 12) draw_dialogue(out, y0);
  else if (g_status_band && s == GC_ROWS - 1) draw_status_band(out, y0);
}
