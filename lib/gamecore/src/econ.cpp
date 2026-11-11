// Economy: wallet cap, items, shop till, goals. Chunk 0 ships the stopgap that fixes
// "2831 coins and 0 wood after a day" (design 3, 11.2 step 6); chunk 4 extends it with the
// full goal table, more goods, achievements and friendship.
#include "world_priv.h"

#define TILL_PER_DAY 60
#define WOOD_RESERVE 3

static const uint16_t COIN_CAP[4] GC_ROM_ATTR = {99, 250, 500, 999};
static const uint8_t ITEM_CAP[GQ_ITEM_COUNT] GC_ROM_ATTR = {
  30, 30, 20, 20, 20, 20, 20, 20, 9, 20, 9, 20, 20, 0, 0, 0,    // goods 0..15
  1, 1, 4, 1, 1, 1, 9, 0                                        // key items 16..23
};
static const uint8_t SELL_PRICE[GQ_ITEM_COUNT] GC_ROM_ATTR = {
  1, 2, 3, 2, 2, 1, 2, 5, 20, 4, 0, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

// Stopgap goal table: the three purses. Chunk 4 replaces it with GOALS[22].
#define GOAL_COUNT 3
static const uint16_t GOAL_PRICE[GOAL_COUNT] GC_ROM_ATTR = {60, 200, 450};

static void sat16(uint16_t* p) { if (*p < 65535) (*p)++; }

void econ_init(void) {}

int econ_coin_cap(void) {
  int t = G.s.tool[GQ_TOOL_PURSE];
  if (t > 3) t = 3;
  return GC_RD16(&COIN_CAP[t]);
}

void econ_earn(int coins, int source) {
  (void)source;
  if (coins <= 0) return;
  uint32_t n = (uint32_t)coins;
  int room = econ_coin_cap() - (int)G.s.coins;
  if (room < 0) room = 0;
  uint32_t in = n < (uint32_t)room ? n : (uint32_t)room;
  G.s.coins = (uint16_t)(G.s.coins + in);
  uint32_t over = n - in;
  if (over) {
    uint32_t f = (uint32_t)G.s.fund + over;
    while (f >= 100) {
      f -= 100;
      if (G.s.stars < 999) G.s.stars++;
      gq_emit(GQ_EV_STAR, G.s.stars & 0xFF, 0);
    }
    G.s.fund = (uint16_t)f;
  }
  G.s.earned_total = G.s.earned_total + n < G.s.earned_total ? 0xFFFFFFFFu : G.s.earned_total + n;
  g_dirty |= 1u;
  gq_touch();
}

int econ_pay(int coins) {
  if (coins <= 0) return 1;
  if (coins > (int)G.s.coins) return 0;
  G.s.coins = (uint16_t)(G.s.coins - coins);
  uint32_t sp = G.s.spent_total + (uint32_t)coins;
  G.s.spent_total = sp < G.s.spent_total ? 0xFFFFFFFFu : sp;
  g_dirty |= 1u;
  gq_touch();
  return 1;
}

int econ_item(int item) {
  if (item < 0 || item >= GQ_ITEM_COUNT) return 0;
  return G.s.inv[item];
}

int econ_item_cap(int item) {
  if (item < 0 || item >= GQ_ITEM_COUNT) return 0;
  return GC_RD8(&ITEM_CAP[item]);
}

int econ_add_item(int item, int n) {
  if (item < 0 || item >= GQ_ITEM_COUNT || n <= 0) return 0;
  int room = econ_item_cap(item) - (int)G.s.inv[item];
  if (room <= 0) return 0;
  int put = n < room ? n : room;
  G.s.inv[item] = (uint8_t)(G.s.inv[item] + put);
  g_dirty |= 1u;
  return put;
}

int econ_take_item(int item, int n) {
  if (item < 0 || item >= GQ_ITEM_COUNT || n < 0) return 0;
  if ((int)G.s.inv[item] < n) return 0;
  G.s.inv[item] = (uint8_t)(G.s.inv[item] - n);
  g_dirty |= 1u;
  return 1;
}

// How many of an item the hero keeps (the current goal needs it). Stopgap: 3 wood.
static int reserve(int item) {
  int r = item == GQ_ITEM_WOOD ? WOOD_RESERVE : 0;
  int cap = econ_item_cap(item);
  return r < cap ? r : cap;
}

static int sellable_units(int item) {
  int have = econ_item(item) - reserve(item);
  if (have <= 0) return 0;
  int price = GC_RD8(&SELL_PRICE[item]);
  if (price <= 0) return 0;
  int by_till = (int)G.s.till_left / price;
  return have < by_till ? have : by_till;
}

int econ_sell_all(void) {
  int total = 0;
  for (int item = 0; item < GQ_ITEM_COUNT; item++) {
    int units = sellable_units(item);
    if (units <= 0) continue;
    int coins = units * GC_RD8(&SELL_PRICE[item]);
    econ_take_item(item, units);
    G.s.till_left = (uint8_t)(G.s.till_left - coins);
    econ_earn(coins, GQ_SRC_SALE);
    total += coins;
    gq_emit(GQ_EV_SOLD, item, units);
  }
  return total;
}

int econ_goal(GqGoal* out) {
  if (G.s.goal >= GOAL_COUNT) return 0;
  if (out) {
    *out = GqGoal();
    out->index = G.s.goal;
    out->mat_item = out->mat2_item = 0xFF;
    out->price = GC_RD16(&GOAL_PRICE[G.s.goal]);
    out->affordable = G.s.coins >= out->price;
    out->name_txt = G.s.goal;
  }
  return 1;
}

int econ_buy_goal(void) {
  GqGoal g;
  if (!econ_goal(&g) || !g.affordable) return 0;
  if (!econ_pay(g.price)) return 0;
  if (G.s.tool[GQ_TOOL_PURSE] < 3) G.s.tool[GQ_TOOL_PURSE]++;
  G.s.goal++;
  gq_touch();
  gq_emit(GQ_EV_BOUGHT, g.index, 0);
  return 1;
}

int econ_tool(int tool) { return tool >= 0 && tool < GQ_TOOL_COUNT ? G.s.tool[tool] : 0; }
int econ_project_built(int project) { return project >= 0 && project < 10 && ((G.s.projects >> project) & 1); }

int econ_activity_bias(int act) {
  switch (act) {
    case GQ_ACT_SELL: return sellable_units(GQ_ITEM_WOOD) + sellable_units(GQ_ITEM_FISH) > 0 ? 0 : -9;
    case GQ_ACT_BUY: { GqGoal g; return econ_goal(&g) && g.affordable ? 0 : -9; }
    case GQ_ACT_QUEST: return G.s.errands_today >= 3 ? -9 : 0;
    case GQ_ACT_EAT: return econ_meal_price() ? 0 : -9;
    case GQ_ACT_CHOP: return econ_item(GQ_ITEM_WOOD) >= econ_item_cap(GQ_ITEM_WOOD) ? -9 : 0;
    case GQ_ACT_FISH: return econ_item(GQ_ITEM_FISH) >= econ_item_cap(GQ_ITEM_FISH) ? -9 : 0;
    default: return 0;
  }
}

int econ_focus_item(void) { return GQ_ITEM_WOOD; }

void econ_friend_add(int npc, int delta) {
  if (npc < 0 || npc >= 12) return;
  int v = (int)G.s.friend_[npc] + delta;
  if (v < 0) v = 0;
  if (v > 100) v = 100;
  G.s.friend_[npc] = (uint8_t)v;
  gq_touch();
}

int econ_friend(int npc) { return npc >= 0 && npc < 12 ? G.s.friend_[npc] : 0; }

void econ_collect(int c) {
  if (c < 0 || c >= 64) return;
  uint8_t bit = (uint8_t)(1u << (c & 7));
  if (G.s.seen[c >> 3] & bit) return;
  G.s.seen[c >> 3] = (uint8_t)(G.s.seen[c >> 3] | bit);
  gq_touch();
  gq_emit(GQ_EV_NEW_COLLECTIBLE, c, 0);
}

int econ_meal_price(void) {
  if (G.s.meals_today == 0) return 4;
  if (G.s.meals_today == 1) return 5;
  return 0;
}

void econ_on_event(int ev, int a, int b) {
  (void)a; (void)b;
  switch (ev) {
    case GQ_EV_NEW_DAY:
      G.s.till_left = TILL_PER_DAY;
      G.s.errands_today = G.s.meals_today = G.s.gifts_today = 0;
      G.s.chat_today = 0;
      gq_touch();
      break;
    case GQ_EV_CHOPPED: sat16(&G.s.stat[GQ_STAT_TREES]); break;
    case GQ_EV_CAUGHT: sat16(&G.s.stat[GQ_STAT_FISH]); break;
    case GQ_EV_ATE:
      if (G.s.meals_today < 3) G.s.meals_today++;
      sat16(&G.s.stat[GQ_STAT_MEALS]);
      break;
    case GQ_EV_TALKED: sat16(&G.s.stat[GQ_STAT_CHATS]); break;
    default: break;
  }
}
