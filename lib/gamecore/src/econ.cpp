// Economy (design 3, chunk 4): wallet cap and overflow to stars, items, shop till, the 22
// goals (tools and village projects), village-fund mode, friendship, collection,
// achievements, statistics and the scheduler bias.
// All arithmetic saturates and every table index is clamped (design 8.10).
#include "world_priv.h"
#include "econ_data.h"

#define TILL_BASE 60
#define TILL_STALL 80          // after the market stall (project 4) is built
#define TILL_MAX 160
#define FIND_PER_DAY 10        // coins from monster drops and treasure per game day
#define WOOD_RESERVE 3
#define STALL_PROJECT 4

static const uint16_t COIN_CAP[4] GC_ROM_ATTR = {99, 250, 500, 999};

static uint8_t s_find_today;   // RAM only: coins earned from finds today (a restart merely forgives them)

static void sat16(uint16_t* p) { if (*p < 65535) (*p)++; }
static uint32_t add32(uint32_t a, uint32_t b) { return a + b < a ? 0xFFFFFFFFu : a + b; }

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void dirty_money(void) { g_dirty |= 1u | (1u << (GC_ROWS - 1)); }   // HUD strip and goal band

// ------------------------------------------------------------------ goals
struct GoalRow { uint8_t kind, id, m1, n1, m2, n2, pre, day; uint16_t price; };

static void goal_row(int i, GoalRow* r) {
  i = clampi(i, 0, GOAL_COUNT - 1);
  const uint8_t* p = &GOALS[i * 10];
  r->kind = GC_RD8(p + 0);
  r->id = GC_RD8(p + 1);
  r->price = (uint16_t)(GC_RD8(p + 2) | (GC_RD8(p + 3) << 8));
  r->m1 = GC_RD8(p + 4); r->n1 = GC_RD8(p + 5);
  r->m2 = GC_RD8(p + 6); r->n2 = GC_RD8(p + 7);
  r->pre = GC_RD8(p + 8);
  r->day = GC_RD8(p + 9);
}

// level a tool goal sets: 1 + the number of earlier goals for the same tool
static int goal_level(int i, const GoalRow* r) {
  int lvl = 1;
  for (int j = 0; j < i && j < GOAL_COUNT; j++)
    if (GC_RD8(&GOALS[j * 10]) == r->kind && GC_RD8(&GOALS[j * 10 + 1]) == r->id) lvl++;
  return lvl > 3 ? 3 : lvl;
}

static int goal_bought(int i) {
  GoalRow r;
  goal_row(i, &r);
  if (r.kind == GK_PROJECT) return r.id < 10 && ((G.s.projects >> r.id) & 1);
  return r.id < GQ_TOOL_COUNT && G.s.tool[r.id] >= goal_level(i, &r);
}

// Can the hero get `need` of an item with what he has unlocked? (Mining needs picks, shards come
// from the story.) This is what keeps the goal list free of dead ends.
static int can_get(int item, int need) {
  if (item == NO_ITEM || need <= 0) return 1;
  if (item >= GQ_ITEM_COUNT || need > econ_item_cap(item)) return 0;
  if (econ_item(item) >= need) return 1;
  switch (item) {
    case GQ_ITEM_STONE: return G.s.tool[GQ_TOOL_PICK] >= 1;
    case GQ_ITEM_ORE: case GQ_ITEM_GEM: return G.s.tool[GQ_TOOL_PICK] >= 2;
    case GQ_ITEM_SHARD: return G.s.chapter >= need;
    default: return 1;
  }
}

static int goal_available(int i) {
  GoalRow r;
  goal_row(i, &r);
  if (goal_bought(i)) return 0;
  if (r.pre != NO_PRE && !goal_bought(r.pre)) return 0;
  if (G.s.day < r.day) return 0;
  return can_get(r.m1, r.n1) && can_get(r.m2, r.n2);
}

// The current goal: the first one that can be bought now (prerequisite bought, day reached,
// materials obtainable). When none is available yet, the one that opens soonest (prerequisite
// bought, materials obtainable, smallest min_day), so the hero saves up for what comes next and
// the band shows it. Last resort: the first one not bought. -1 = everything bought (village fund).
static int find_goal(void) {
  int first_unbought = -1, soonest = -1, soonest_day = 256;
  for (int i = 0; i < GOAL_COUNT; i++) {
    if (goal_bought(i)) continue;
    if (first_unbought < 0) first_unbought = i;
    if (goal_available(i)) return i;
    GoalRow r;
    goal_row(i, &r);
    if ((r.pre == NO_PRE || goal_bought(r.pre)) && can_get(r.m1, r.n1) && can_get(r.m2, r.n2) && r.day < soonest_day) {
      soonest = i;
      soonest_day = r.day;
    }
  }
  return soonest >= 0 ? soonest : first_unbought;
}

static void sync_goal(void) {
  int g = find_goal();
  G.s.goal = (uint8_t)(g < 0 ? GOAL_COUNT : g);
}

static int mats_held(const GoalRow* r) {
  return (r->m1 == NO_ITEM || econ_item(r->m1) >= r->n1) && (r->m2 == NO_ITEM || econ_item(r->m2) >= r->n2);
}

// ------------------------------------------------------------------ wallet
int econ_coin_cap(void) {
  int t = G.s.tool[GQ_TOOL_PURSE];
  if (t > 3) t = 3;
  return GC_RD16(&COIN_CAP[t]);
}

static int till_base(void) { return econ_project_built(STALL_PROJECT) ? TILL_STALL : TILL_BASE; }

// design 3.4: room first, the rest into the village fund, every 100 of fund is a star.
void econ_earn(int coins, int source) {
  if (coins <= 0) return;
  uint32_t n = (uint32_t)coins;
  if (source == GQ_SRC_FIND) {                      // finds are limited to FIND_PER_DAY a day
    uint32_t room_f = FIND_PER_DAY - (s_find_today < FIND_PER_DAY ? s_find_today : FIND_PER_DAY);
    if (n > room_f) n = room_f;
    if (n == 0) return;
    s_find_today = (uint8_t)(s_find_today + n);
  }
  int room = econ_coin_cap() - (int)G.s.coins;
  if (room < 0) room = 0;
  uint32_t in = n < (uint32_t)room ? n : (uint32_t)room;
  G.s.coins = (uint16_t)(G.s.coins + in);
  uint32_t over = n - in;
  if (over) {
    uint32_t f = (uint32_t)G.s.fund + over;
    uint32_t stars = f / 100;
    G.s.fund = (uint16_t)(f % 100);
    if (stars) {
      uint32_t s = (uint32_t)G.s.stars + stars;
      G.s.stars = (uint16_t)(s > 999 ? 999 : s);
      gq_emit(GQ_EV_STAR, G.s.stars & 0xFF, 0);
    }
  }
  G.s.earned_total = add32(G.s.earned_total, n);
  dirty_money();
  gq_touch();
}

int econ_pay(int coins) {
  if (coins <= 0) return 1;
  if (coins > (int)G.s.coins) return 0;
  G.s.coins = (uint16_t)(G.s.coins - coins);
  G.s.spent_total = add32(G.s.spent_total, (uint32_t)coins);
  dirty_money();
  gq_touch();
  return 1;
}

// ------------------------------------------------------------------ items
int econ_item(int item) {
  if (item < 0 || item >= GQ_ITEM_COUNT) return 0;
  return G.s.inv[item];
}

int econ_item_cap(int item) {
  if (item < 0 || item >= GQ_ITEM_COUNT) return 0;
  return GC_RD8(&ITEM_CAP[item]);
}

int econ_item_icon(int item) {
  if (item < 0 || item >= GQ_ITEM_COUNT) return I_WOOD;
  return GC_RD8(&ITEM_ICON[item]);
}

int econ_add_item(int item, int n) {
  if (item < 0 || item >= GQ_ITEM_COUNT || n <= 0) return 0;
  int room = econ_item_cap(item) - (int)G.s.inv[item];
  if (room <= 0) return 0;
  int put = n < room ? n : room;
  G.s.inv[item] = (uint8_t)(G.s.inv[item] + put);
  g_dirty |= 1u;
  sync_goal();
  return put;
}

int econ_take_item(int item, int n) {
  if (item < 0 || item >= GQ_ITEM_COUNT || n < 0) return 0;
  if ((int)G.s.inv[item] < n) return 0;
  G.s.inv[item] = (uint8_t)(G.s.inv[item] - n);
  g_dirty |= 1u;
  sync_goal();
  return 1;
}

// ------------------------------------------------------------------ shop
// How many of an item the hero keeps: what the current goal still needs (at most the item cap);
// wood never drops below WOOD_RESERVE.
static int reserve(int item) {
  int r = item == GQ_ITEM_WOOD ? WOOD_RESERVE : 0;
  int g = find_goal();
  if (g >= 0) {
    GoalRow row;
    goal_row(g, &row);
    if (row.m1 == item && row.n1 > r) r = row.n1;
    if (row.m2 == item && row.n2 > r) r = row.n2;
  }
  int cap = econ_item_cap(item);
  return r < cap ? r : cap;
}

static int in_season(int item) {
  int mask = GC_RD8(&ITEM_SEASON[item]);
  return mask != 0x0F && ((mask >> cal_season()) & 1);
}

static int price_of(int item) {
  int p = GC_RD8(&SELL_PRICE[item]);
  return p > 0 && in_season(item) ? p + 1 : p;
}

static int sellable_units(int item) {
  if (item < 0 || item >= GQ_ITEM_COUNT) return 0;
  int have = econ_item(item) - reserve(item);
  if (have <= 0) return 0;
  int price = price_of(item);
  if (price <= 0) return 0;
  int by_till = (int)G.s.till_left / price;
  return have < by_till ? have : by_till;
}

// Coins the hero could still get for his surplus goods (unconstrained by the till).
static int surplus_value(void) {
  int v = 0;
  for (int i = 0; i < GQ_ITEM_COUNT; i++) {
    int have = econ_item(i) - reserve(i);
    if (have > 0) v += have * price_of(i);
  }
  return v;
}

int econ_sell_all(void) {
  int total = 0;
  for (int item = 0; item < GQ_ITEM_COUNT; item++) {
    int units = sellable_units(item);
    if (units <= 0) continue;
    int coins = units * price_of(item);
    econ_take_item(item, units);
    G.s.till_left = (uint8_t)(G.s.till_left - coins);
    econ_earn(coins, GQ_SRC_SALE);
    total += coins;
    gq_emit(GQ_EV_SOLD, item, units);
  }
  return total;
}

// ------------------------------------------------------------------ goals API
int econ_goal(GqGoal* out) {
  int g = find_goal();
  G.s.goal = (uint8_t)(g < 0 ? GOAL_COUNT : g);
  if (g < 0) return 0;
  if (out) {
    GoalRow r;
    goal_row(g, &r);
    *out = GqGoal();
    out->index = (uint8_t)g;
    out->is_project = r.kind == GK_PROJECT;
    out->mat_item = r.m1; out->mat_need = r.n1;
    out->mat2_item = r.m2; out->mat2_need = r.n2;
    out->price = r.price;
    out->affordable = goal_available(g) && G.s.coins >= r.price && mats_held(&r);
    out->name_txt = (uint8_t)g;
  }
  return 1;
}

int econ_goal_count(void) { return GOAL_COUNT; }

void econ_goal_name(int goal, char* buf, int cap) {
  if (cap <= 0) return;
  int n = 0;
  if (goal >= 0 && goal < GOAL_COUNT)
    for (; n < GOAL_NAME_LEN && n < cap - 1; n++) {
      char c = (char)GC_RD8(&GOAL_NAME[goal][n]);
      if (!c) break;
      buf[n] = c;
    }
  buf[n] = 0;
}

// Band text: name of the current goal and progress in coins (design 6.6). Returns 0 in village-fund mode
// (then num/den are the wallet and its cap).
int econ_goal_band(char* name, int cap, int* num, int* den) {
  GqGoal g;
  if (!econ_goal(&g)) {
    str_copy(name, "FUND", cap);
    if (num) *num = G.s.coins;
    if (den) *den = econ_coin_cap();
    return 0;
  }
  econ_goal_name(g.index, name, cap);
  if (num) *num = G.s.coins < g.price ? G.s.coins : g.price;
  if (den) *den = g.price;
  return 1;
}

static void ach_check(void);

int econ_buy_goal(void) {
  int g = find_goal();
  if (g < 0) {
    // village fund: with a full wallet the hero donates 100 coins at the notice board for a star
    int cap = econ_coin_cap();
    if (cap < 100 || (int)G.s.coins < cap || !econ_pay(100)) return 0;
    if (G.s.stars < 999) G.s.stars++;
    gq_emit(GQ_EV_STAR, G.s.stars & 0xFF, 0);
    return 1;
  }
  GqGoal info;
  GoalRow r;
  if (!econ_goal(&info) || !info.affordable) return 0;
  goal_row(g, &r);
  if (!econ_pay(r.price)) return 0;
  if (r.m1 != NO_ITEM) econ_take_item(r.m1, r.n1);
  if (r.m2 != NO_ITEM) econ_take_item(r.m2, r.n2);
  if (r.kind == GK_PROJECT) {
    if (r.id < 10) G.s.projects = (uint16_t)(G.s.projects | (1u << r.id));
  } else if (r.id < GQ_TOOL_COUNT) {
    int lvl = goal_level(g, &r);
    if (G.s.tool[r.id] < lvl) G.s.tool[r.id] = (uint8_t)lvl;
  }
  if (G.s.coins > econ_coin_cap()) G.s.coins = (uint16_t)econ_coin_cap();
  sync_goal();
  gq_touch();
  dirty_money();
  gq_emit(GQ_EV_BOUGHT, g, 0);
  if (r.kind == GK_PROJECT) gq_emit(GQ_EV_BUILT, r.id, 0);
  return 1;
}

int econ_tool(int tool) { return tool >= 0 && tool < GQ_TOOL_COUNT ? G.s.tool[tool] : 0; }
int econ_project_built(int project) { return project >= 0 && project < 10 && ((G.s.projects >> project) & 1); }

// The material the HUD shows: the first one the current goal still lacks, else wood.
int econ_focus_item(void) {
  int g = find_goal();
  if (g >= 0) {
    GoalRow r;
    goal_row(g, &r);
    if (r.m1 != NO_ITEM && econ_item(r.m1) < r.n1) return r.m1;
    if (r.m2 != NO_ITEM && econ_item(r.m2) < r.n2) return r.m2;
  }
  return GQ_ITEM_WOOD;
}

// ------------------------------------------------------------------ scheduler bias
static int lacks(int item) {   // the current goal needs more of this item than the hero has
  int g = find_goal();
  if (g < 0) return 0;
  GoalRow r;
  goal_row(g, &r);
  return (r.m1 == item && econ_item(item) < r.n1) || (r.m2 == item && econ_item(item) < r.n2);
}

static int gather_bias(int item) {
  if (econ_item(item) >= econ_item_cap(item)) return -9;
  return lacks(item) ? 3 : 0;
}

int econ_activity_bias(int act) {
  switch (act) {
    case GQ_ACT_SELL: {
      int units = 0;
      for (int i = 0; i < GQ_ITEM_COUNT; i++) units += sellable_units(i);
      if (units <= 0) return -9;
      return surplus_value() >= 10 && G.s.till_left > 0 ? 2 : 0;
    }
    case GQ_ACT_BUY: {
      GqGoal g;
      if (econ_goal(&g)) return g.affordable ? 3 : -9;
      int cap = econ_coin_cap();
      return cap >= 100 && (int)G.s.coins >= cap ? 3 : -9;
    }
    case GQ_ACT_QUEST: return G.s.errands_today >= 3 ? -9 : 0;
    case GQ_ACT_EAT: {
      int p = econ_meal_price();
      return p && p <= (int)G.s.coins ? 0 : -9;
    }
    case GQ_ACT_CHOP: return gather_bias(GQ_ITEM_WOOD);
    case GQ_ACT_FISH: return gather_bias(GQ_ITEM_FISH);
    case GQ_ACT_FORAGE: {
      static const uint8_t F[4] = {GQ_ITEM_MUSHROOM, GQ_ITEM_BERRY, GQ_ITEM_FLOWER, GQ_ITEM_HERB};
      int best = -9;
      for (int i = 0; i < 4; i++) {
        int it = F[i];
        if (GC_RD8(&ITEM_SEASON[it]) != 0x0F && !in_season(it)) continue;   // cannot be found now
        int b = gather_bias(it);
        if (b > best) best = b;
      }
      return best;
    }
    case GQ_ACT_MINE: {
      if (G.s.tool[GQ_TOOL_PICK] < 1) return -9;
      int b = gather_bias(GQ_ITEM_STONE);
      if (G.s.tool[GQ_TOOL_PICK] >= 2) {
        int o = gather_bias(GQ_ITEM_ORE), m = gather_bias(GQ_ITEM_GEM);
        if (o > b) b = o;
        if (m > b) b = m;
      }
      return b;
    }
    case GQ_ACT_GARDEN: return econ_project_built(1) ? gather_bias(GQ_ITEM_CROP) : -9;
    case GQ_ACT_GIFT: return G.s.gifts_today >= 1 ? -9 : 0;
    default: return 0;
  }
}

// ------------------------------------------------------------------ friendship, collection
void econ_friend_add(int npc, int delta) {
  if (npc < 0 || npc >= 12) return;
  int v = clampi((int)G.s.friend_[npc] + clampi(delta, -200, 200), 0, 100);
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

// ------------------------------------------------------------------ achievements
static int popcount(uint32_t v) { int c = 0; while (v) { c += v & 1; v >>= 1; } return c; }

static int ach_met(int i) {
  const uint8_t* p = &ACH[i * 5];
  int kind = GC_RD8(p), index = GC_RD8(p + 1);
  uint32_t thr = (uint32_t)(GC_RD8(p + 2) | (GC_RD8(p + 3) << 8));
  switch (kind) {
    case AK_STAT: return index < GQ_STAT_COUNT && G.s.stat[index] >= thr;
    case AK_SEEN: return index < 8 && (uint32_t)popcount(G.s.seen[index]) >= thr;
    case AK_CHAPTER: return G.s.chapter >= thr;
    case AK_STARS: return G.s.stars >= thr;
    case AK_BUILT: return (uint32_t)popcount(G.s.projects & 0x3FF) >= thr;
    case AK_DAY: return G.s.day >= thr;
    case AK_FRIENDS:
      for (int n = GQ_NPC_MIRA; n < GQ_NPC_COUNT; n++) if (G.s.friend_[n] < thr) return 0;
      return 1;
    default: return 0;
  }
}

// Unlocks at most one achievement per call, so a restored game cannot flood the event queue.
static void ach_check(void) {
  for (int i = 0; i < ACH_COUNT; i++) {
    if ((G.s.ach[i >> 3] >> (i & 7)) & 1) continue;
    if (!ach_met(i)) continue;
    G.s.ach[i >> 3] = (uint8_t)(G.s.ach[i >> 3] | (1u << (i & 7)));
    gq_touch();
    gq_emit(GQ_EV_ACHIEVEMENT, i, 0);
    return;
  }
}

void econ_ach_name(int i, char* buf, int cap) {
  if (cap <= 0) return;
  int n = 0;
  if (i >= 0 && i < ACH_COUNT)
    for (; n < ACH_NAME_LEN && n < cap - 1; n++) {
      char c = (char)GC_RD8(&ACH_NAME[i][n]);
      if (!c) break;
      buf[n] = c;
    }
  buf[n] = 0;
}

// ------------------------------------------------------------------ events
void econ_init(void) {
  s_find_today = 0;
  int cap = econ_coin_cap();
  if ((int)G.s.coins > cap) G.s.coins = (uint16_t)cap;
  if (G.s.fund > 99) G.s.fund = 99;
  sync_goal();
}

static void stat_inc(int stat) { if (stat >= 0 && stat < GQ_STAT_COUNT) sat16(&G.s.stat[stat]); }

void econ_on_event(int ev, int a, int b) {
  switch (ev) {
    case GQ_EV_NEW_DAY:
      G.s.till_left = (uint8_t)till_base();
      G.s.errands_today = G.s.meals_today = G.s.gifts_today = 0;
      G.s.chat_today = 0;
      s_find_today = 0;
      gq_touch();
      dirty_money();
      break;
    case GQ_EV_FESTIVAL:
      if (b == 1) {                       // the till doubles on a festival day
        stat_inc(GQ_STAT_FESTIVALS);
        int t = (int)G.s.till_left + till_base();
        G.s.till_left = (uint8_t)(t > TILL_MAX ? TILL_MAX : t);
        gq_touch();
      }
      break;
    case GQ_EV_CHOPPED: stat_inc(GQ_STAT_TREES); break;
    case GQ_EV_CAUGHT:
      stat_inc(GQ_STAT_FISH);
      if (a >= 0 && a < 64) econ_collect(a);
      break;
    case GQ_EV_FORAGED: case GQ_EV_HARVESTED: stat_inc(GQ_STAT_FORAGE); break;
    case GQ_EV_MINED: stat_inc(GQ_STAT_MINED); break;
    case GQ_EV_SLAIN: stat_inc(GQ_STAT_MONSTERS); break;
    case GQ_EV_QUEST_DONE: if (a != 0xFF) stat_inc(GQ_STAT_QUESTS); break;   // errands count in quest.cpp
    case GQ_EV_ATE:
      if (G.s.meals_today < 3) G.s.meals_today++;
      stat_inc(GQ_STAT_MEALS);
      break;
    case GQ_EV_TALKED:
      stat_inc(GQ_STAT_CHATS);
      if (a > 0 && a < 10 && !((G.s.chat_today >> a) & 1)) {      // +2 once a day per villager
        G.s.chat_today = (uint16_t)(G.s.chat_today | (1u << a));
        econ_friend_add(a, 2);
      }
      break;
    case GQ_EV_GIFTED:
      stat_inc(GQ_STAT_GIFTS);
      if (G.s.gifts_today < 1) {                                  // +5 once a day in total
        G.s.gifts_today = 1;
        if (a > 0 && a < 10) econ_friend_add(a, 5);
      }
      break;
    case GQ_EV_FAINTED: stat_inc(GQ_STAT_FAINTS); break;
    default: break;
  }
  sync_goal();
  ach_check();
}
