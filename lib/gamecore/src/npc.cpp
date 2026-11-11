// Villagers. (Split out of village.cpp.)
#include "world_priv.h"

static void hide_at_door(Actor* a, int door_x) {
  a->visible = 0;
  place(a, door_x, STREET_Y);
}

// ----------------------------------------------------------- villager ai
static const uint8_t NPC_HOME[A_COUNT][2] = {{0, 0}, {8, 5}, {BENCH_B_X, BENCH_Y}, {7, 9}};
static const uint8_t NPC_DOOR[A_COUNT] = {0, SHOP_DOOR_X, TAVERN_DOOR_X, SHOP_DOOR_X};
static const uint8_t NPC_RADIUS[A_COUNT] = {0, 2, 1, 4};

void npc_tick(int i, uint32_t dt) {
  Actor* n = &A[i];
  n->timer -= (int32_t)dt;
  if (n->talking) {   // stop at the next tile boundary
    if (aligned(n)) n->plen = n->pidx;
    else if (n->pidx < n->plen) n->plen = (uint8_t)(n->pidx + 1);
    return;
  }
  if (cal_is_night()) {
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
