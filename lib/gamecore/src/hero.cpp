// Hero: scheduler and activities. (Split out of village.cpp.)
#include "world_priv.h"

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


// ------------------------------------------------------------ hero activities
enum {
  ACT_NONE = 0, ACT_WANDER, ACT_CHOP, ACT_FISH, ACT_SHOP, ACT_TAVERN, ACT_SIT,
  ACT_CHAT, ACT_QUEST, ACT_SLEEP
};

static void hero_choose(Actor* h) {
  h->phase = 0;
  h->timer = 0;
  h->counter = 0;
  h->item = I_NONE;
  if (cal_is_night()) { h->act = ACT_SLEEP; return; }
  int hr = cal_minute() / 60;
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

void hero_tick(Actor* h, uint32_t dt) {
  h->timer -= (int32_t)dt;
  switch (h->act) {
    case ACT_NONE:
      if (h->timer <= 0 && ui_dialogue_done()) hero_choose(h);
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
        ui_say(s_chat_npc, line, 2500);
        h->phase = 3;
      } else if (h->phase == 3 && ui_dialogue_done()) {
        ui_say(A_HERO, REPLIES[gc_rnd() % 5], 2000);
        h->phase = 4;
      } else if (h->phase == 4 && ui_dialogue_done()) {
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
        ui_say(A_HERO, msg, 3000);
        h->emote = I_HEART;
        h->phase = 5;
      } else if (h->phase == 5 && ui_dialogue_done()) {
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
      } else if (h->phase == 2 && !cal_is_night()) {
        h->visible = 1;
        h->emote = I_NONE;
        h->ex = -1;
        h->dir = DIR_DOWN;
        finish(h);
      }
      break;
  }
}

