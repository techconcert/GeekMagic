// Hero: scheduler and the 16 activities (design 2.1, 11.5). Every activity is a small phase machine.
// Safety net (recovery rules): every phase has a watchdog (120 s), every activity a hard limit of 4 min
// (sleep and festivals excepted); when one trips the hero drops everything and walks home. Travel has
// its own timeout, talking flags of villagers time out by themselves (npc.cpp).
#include "world_priv.h"

enum { ACT_NONE = GQ_ACT_NONE, ACT_HOME = 32, ACT_BREAKFAST = 33, ACT_FEST = 34 };

static const char* const LINES[8][2] = {
  {"FRESH APPLES TODAY|WANT TO TRADE?", "WOOD SELLS WELL|THIS WEEK."},
  {"IN MY DAY WE HAD|NO FANCY SWORDS.", "THE CAVES HUM AT|NIGHT. CAREFUL."},
  {"TEACH ME TO USE|A SWORD!", "I SAW A FROG AS|BIG AS A DOG!"},
  {"STEW IS ON THE|FIRE. SIT, EAT!", "BEST SEAT IS BY|THE HEARTH."},
  {"THE FISH ARE|BITING TODAY.", "WATCH THE TIDE,|HERO."},
  {"MEASURE TWICE,|CUT ONCE.", "THE VILLAGE IS|GROWING NICELY."},
  {"THE FOREST WHISPERS|IF YOU LISTEN.", "MIND THE SLIMES|IN THE MOSS."},
  {"RARE WARES FROM|FAR AWAY!", "SEE ANYTHING YOU|LIKE?"},
};
static const char* const REPLIES[] = {
  "HA! MAYBE LATER.", "I'LL KEEP WATCH.", "SOUNDS GOOD!", "SURE THING.", "TELL ME MORE!",
};
static const char* const PLACE_NAME[4] = {"CAVES", "RUINS", "FOREST", "TOWER"};
static const uint8_t PLACE_ICON[4] = {T_ROCK, T_FENCE, T_WATER, T_WALL_DOOR};
const char* hero_place_name(int place) { return PLACE_NAME[place & 3]; }
int hero_place_icon(int place) { return PLACE_ICON[place & 3]; }

// ------------------------------------------------------------------- state
static int32_t  s_act_ms, s_phase_ms;
static uint16_t s_key;
static uint8_t  s_last1, s_last2;
static uint16_t s_bf_day, s_gift_day, s_find_day;
static uint8_t  s_find_n;
static struct { uint8_t on, area, x, y, pathed, repaths; int32_t ms; } T;   // travel
static uint8_t  s_slot, s_npc, s_item_gift, s_force_npc, s_fight_area, s_fight_quest;
static uint8_t  s_win, s_swings, s_choice;
static int      s_tx, s_ty;
static GqNeed   s_need;

int hero_max_hearts(void) {
  int m = 3 + econ_tool(GQ_TOOL_SWORD);
  return m > 6 ? 6 : m;
}

// ------------------------------------------------------------------ helpers
static void tv_start(int area, int x, int y) {
  T.on = 1; T.area = (uint8_t)area; T.x = (uint8_t)x; T.y = (uint8_t)y; T.pathed = 0; T.repaths = 0; T.ms = 0;
}

// 0 busy, 1 arrived, -1 failed
static int tv_step(Actor* h, uint32_t dt) {
  T.ms += (int32_t)dt;
  if (T.ms > 70000) return -1;
  if (world_transition_busy()) return 0;
  int x, y;
  if (world_area() == T.area) {
    x = T.x; y = T.y;
    if (y == 0xFF && !world_spot(T.x, &x, &y)) return -1;
  } else {
    int hop = world_next_hop(world_area(), T.area), arg;
    if (hop < 0 || !world_find_exit(hop, &x, &y, &arg)) return -1;
    if (tile_x(h) == x && tile_y(h) == y && aligned(h)) { world_change_area(hop, arg, 1); T.pathed = 0; T.repaths = 0; return 0; }
  }
  if (world_area() == T.area && tile_x(h) == x && tile_y(h) == y && aligned(h)) return 1;
  if (!T.pathed || arrived(h)) {                      // not started, or the path ended somewhere else
    if (T.repaths++ > 4 || !find_path(h, x, y)) return -1;
    T.pathed = 1;
  }
  return 0;
}

static void clear_talk(void) {
  for (int i = 1; i < A_COUNT; i++) if (A[i].kind != AK_NONE && A[i].talking) npc_set_talking(i, 0);
}

static void tidy(Actor* h) {
  h->pose = POSE_NORMAL; h->item = I_NONE; h->emote = I_NONE; h->ex = h->ey = -1; h->bob = 0;
  if (!h->visible && world_area() != GQ_AREA_VILLAGE) h->visible = 1;
}

static void begin(Actor* h, int act) {
  h->act = (uint8_t)act; h->phase = 0; h->counter = 0; h->timer = 0;
  s_act_ms = 0; s_phase_ms = 0; s_key = 0xFFFF;
  T.on = 0;
}

static void finish(Actor* h) {
  clear_talk();
  tidy(h);
  s_last2 = s_last1; s_last1 = h->act;
  h->act = ACT_NONE; h->phase = 0; h->counter = 0;
  h->timer = rnd_range(800, 2500);
  s_fight_quest = 0; s_force_npc = 0; s_fight_area = 0xFF;
}

static void go_home(Actor* h) {
  clear_talk();
  tidy(h);
  begin(h, ACT_HOME);
}

static void say_hero(const char* t, int ms) { ui_say(GQ_NPC_HERO, t, ms); }

static int adjacent(const Actor* a, const Actor* b) {
  int dx = tile_x(a) - tile_x(b), dy = tile_y(a) - tile_y(b);
  if (dx < 0) dx = -dx;
  if (dy < 0) dy = -dy;
  return dx + dy == 1;
}

// reachable tile next to an actor, closest to the hero
static int stand_next_to(const Actor* n, int* ox, int* oy) {
  static const int8_t DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  int best = 999;
  const Actor* h = &A[A_HERO];
  for (int d = 0; d < 4; d++) {
    int x = tile_x(n) + DX[d], y = tile_y(n) + DY[d];
    if (!walkable(x, y)) continue;
    int dist = (x > tile_x(h) ? x - tile_x(h) : tile_x(h) - x) + (y > tile_y(h) ? y - tile_y(h) : tile_y(h) - y);
    if (dist < best) { best = dist; *ox = x; *oy = y; }
  }
  return best < 999;
}

// tile with an object of one of the kinds (k1 may equal k0); adj = stand next to it (blocking objects)
static int find_node(int k0, int k1, int adj, int* sx, int* sy, int* ox, int* oy) {
  static const int8_t DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  int start = rnd_range(0, GC_ROWS * GC_COLS - 1);
  for (int i = 0; i < GC_ROWS * GC_COLS; i++) {
    int t = (start + i) % (GC_ROWS * GC_COLS), x = TX(t), y = TY(t);
    uint8_t o = g_obj[y][x];
    if (o != k0 && o != k1) continue;
    if (!adj) { *sx = x; *sy = y; *ox = x; *oy = y; return 1; }
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (walkable(nx, ny) && g_obj[ny][nx] == T_NONE) { *sx = nx; *sy = ny; *ox = x; *oy = y; return 1; }
    }
  }
  return 0;
}

static int item_icon(int item) {
  static const int8_t IC[13] = {I_WOOD, WI_STONE, I_FISH, WI_MUSHROOM, WI_BERRY, WI_FLOWER, WI_SHELL, WI_ORE, WI_GEM, WI_CROP, WI_SEED, WI_HERB, WI_GEL};
  return item >= 0 && item < 13 ? (int)IC[item] : (int)I_HEART;
}

static void gain(int item, int n) { if (econ_add_item(item, n)) g_dirty |= 1u; }

// ---------------------------------------------------------------- scheduler
static int gift_item(void) {
  static const uint8_t G_ITEMS[6] = {GQ_ITEM_FLOWER, GQ_ITEM_BERRY, GQ_ITEM_MUSHROOM, GQ_ITEM_HERB, GQ_ITEM_SHELL, GQ_ITEM_CROP};
  for (int i = 0; i < 6; i++) if (econ_item(G_ITEMS[i]) > 0) return G_ITEMS[i];
  return -1;
}

static int pick_activity(void) {
  int m = cal_minute();
  if (cal_is_night()) return GQ_ACT_SLEEP;
  if (cal_festival() >= 0) return ACT_FEST;
  if (m >= 360 && m < 420 && s_bf_day != (uint16_t)(G.s.day + 1)) return ACT_BREAKFAST;
  int w[GQ_ACT_COUNT];
  for (int i = 0; i < GQ_ACT_COUNT; i++) w[i] = 0;
  if (m < 390)       { w[GQ_ACT_WANDER] = 2; w[GQ_ACT_CAT] = 2; }
  else if (m < 600)  { w[GQ_ACT_CHOP] = 5; w[GQ_ACT_FORAGE] = 3; w[GQ_ACT_GARDEN] = 4; w[GQ_ACT_CAT] = 1; w[GQ_ACT_WANDER] = 1; w[GQ_ACT_FISH] = 1; }
  else if (m < 720)  { w[GQ_ACT_CHAT] = 3; w[GQ_ACT_SELL] = 3; w[GQ_ACT_QUEST] = 2; w[GQ_ACT_BUY] = 6; w[GQ_ACT_WANDER] = 1; w[GQ_ACT_GIFT] = 1; }
  else if (m < 780)  { w[GQ_ACT_EAT] = 8; w[GQ_ACT_CHAT] = 3; w[GQ_ACT_SIT] = 1; }
  else if (m < 990)  { w[GQ_ACT_QUEST] = 1; w[GQ_ACT_FIGHT] = 3; w[GQ_ACT_FISH] = 3; w[GQ_ACT_MINE] = 3; w[GQ_ACT_FORAGE] = 2; w[GQ_ACT_CHOP] = 1; }
  else if (m < 1110) { w[GQ_ACT_SELL] = 3; w[GQ_ACT_BUY] = 6; w[GQ_ACT_GIFT] = 2; w[GQ_ACT_CHAT] = 3; w[GQ_ACT_SIT] = 1; w[GQ_ACT_CHOP] = 1; }
  else if (m < 1290) { w[GQ_ACT_EAT] = 6; w[GQ_ACT_SIT] = 4; w[GQ_ACT_CHAT] = 3; w[GQ_ACT_WANDER] = 1; w[GQ_ACT_CAT] = 1; }
  else               { w[GQ_ACT_SIT] = 4; w[GQ_ACT_WANDER] = 1; w[GQ_ACT_CAT] = 1; }

  GqNeed need;
  quest_need(&need);
  int need_ok = need.kind != GQ_NEED_NONE && need.kind != GQ_NEED_WAIT && world_area_open(need.area < GQ_AREA_COUNT ? need.area : 0);
  if (need_ok) w[GQ_ACT_QUEST] += 8;

  if (econ_activity_bias(GQ_ACT_CHOP) <= -9) w[GQ_ACT_CHOP] = 0;
  if (econ_activity_bias(GQ_ACT_FISH) <= -9) w[GQ_ACT_FISH] = 0;
  if (econ_activity_bias(GQ_ACT_SELL) <= -9) w[GQ_ACT_SELL] = 0;
  if (econ_activity_bias(GQ_ACT_BUY) <= -9) w[GQ_ACT_BUY] = 0;
  if (econ_activity_bias(GQ_ACT_EAT) <= -9 || econ_meal_price() <= 0 || econ_meal_price() > (int)G.s.coins) w[GQ_ACT_EAT] = 0;
  if (econ_activity_bias(GQ_ACT_QUEST) <= -9 && !need_ok) w[GQ_ACT_QUEST] = 0;
  if (cal_season() == GQ_WINTER) w[GQ_ACT_FORAGE] = 0;
  if (econ_tool(GQ_TOOL_PICK) < 1) w[GQ_ACT_MINE] = 0;
  if (!econ_project_built(PRJ_GARDEN)) w[GQ_ACT_GARDEN] = 0;
  if (G.s.hero_hearts < 2) w[GQ_ACT_FIGHT] = 0;
  if (G.s.gifts_today >= 1 || s_gift_day == (uint16_t)(G.s.day + 1) || gift_item() < 0) w[GQ_ACT_GIFT] = 0;
  int wx = cal_weather();
  static const uint8_t OUT[] = {GQ_ACT_CHOP, GQ_ACT_FISH, GQ_ACT_FORAGE, GQ_ACT_MINE, GQ_ACT_GARDEN, GQ_ACT_FIGHT, GQ_ACT_QUEST, GQ_ACT_CAT};
  if (wx == GQ_W_STORM) for (unsigned i = 0; i < sizeof(OUT); i++) w[OUT[i]] = 0;
  else if (wx == GQ_W_RAIN) for (unsigned i = 0; i < sizeof(OUT); i++) w[OUT[i]] = (w[OUT[i]] + 1) / 2;
  if (s_last1 == s_last2) w[s_last1 < GQ_ACT_COUNT ? s_last1 : 0] = 0;     // never the same thing three times in a row
  int total = 0;
  for (int i = 1; i < GQ_ACT_COUNT; i++) { if (i == GQ_ACT_SLEEP) w[i] = 0; total += w[i]; }
  if (total <= 0) return GQ_ACT_WANDER;
  int r = (int)(gc_rnd() % (uint32_t)total);
  for (int i = 1; i < GQ_ACT_COUNT; i++) {
    if (r < w[i]) return i;
    r -= w[i];
  }
  return GQ_ACT_WANDER;
}

// ------------------------------------------------------------- the chat family
static int pick_npc(int force, int* area) {
  int present[16], n = 0;
  for (int i = 1; i <= 8; i++) {
    int ar;
    if (!npc_where(i, &ar, 0, 0, 0)) continue;
    if (force && i != force) continue;
    present[n++] = i;
    if (ar == world_area()) present[n++] = i;      // people here are twice as likely
  }
  if (!n) return 0;
  int id = present[rnd_range(0, n - 1)];
  npc_where(id, area, 0, 0, 0);
  return id;
}

static void chat_step(Actor* h, uint32_t dt, int gift) {
  Actor* n = (s_slot > 0 && s_slot < A_COUNT) ? &A[s_slot] : 0;
  switch (h->phase) {
    case 0: {
      int area = 0;
      s_npc = (uint8_t)pick_npc(s_force_npc, &area);
      s_item_gift = 0xFF;
      if (gift) { int it = gift_item(); if (it >= 0) s_item_gift = (uint8_t)it; }
      if (!s_npc || (gift && s_item_gift == 0xFF)) { finish(h); return; }
      s_slot = 0;
      tv_start(area, SP_ENTRY, 0xFF);
      h->phase = 1;
      return;
    }
    case 1: {
      int r = tv_step(h, dt);
      if (r < 0) { finish(h); return; }
      if (r > 0) h->phase = 2;
      return;
    }
    case 2: {   // find the villager, make him stop at a tile boundary
      int slot = npc_actor(s_npc);
      if (slot < 0) { finish(h); return; }
      s_slot = (uint8_t)slot;
      npc_set_talking(slot, 1);
      h->phase = 3;
      return;
    }
    case 3: {
      if (!n || n->kind != AK_VILLAGER || n->id != s_npc) { finish(h); return; }
      if (!aligned(n)) return;
      int ox, oy;
      if (!stand_next_to(n, &ox, &oy) || !find_path(h, ox, oy)) { finish(h); return; }
      h->phase = 4;
      return;
    }
    case 4: {
      if (!n || n->kind != AK_VILLAGER || n->id != s_npc) { finish(h); return; }
      if (!arrived(h)) return;
      if (!adjacent(h, n)) {                              // he moved: try again a few times
        int ox, oy;
        if (++h->counter > 4 || !stand_next_to(n, &ox, &oy) || !find_path(h, ox, oy)) finish(h);
        return;
      }
      face(h, tile_x(n), tile_y(n));
      face(n, tile_x(h), tile_y(h));
      char buf[40];
      const char* line;
      if (gift && s_item_gift != 0xFF) {
        econ_take_item(s_item_gift, 1);
        h->emote = (int8_t)item_icon(s_item_gift);
        h->pose = POSE_CHEER;
        gq_emit(GQ_EV_GIFTED, s_npc, s_item_gift);
        s_gift_day = (uint16_t)(G.s.day + 1);
        line = "FOR ME? THANK|YOU, HERO!";
      } else if (quest_talk(s_npc, buf, 40) && buf[0]) line = buf;
      else {
        quest_ambient_line(s_npc, buf, 40);
        line = buf[0] ? buf : LINES[s_npc - 1][rnd_range(0, 1)];
      }
      ui_say(s_npc, line, 2500);
      h->phase = 5;
      return;
    }
    case 5:
      if (!ui_dialogue_done()) return;
      h->emote = I_NONE;
      if (gift) { h->phase = 7; return; }
      {
        char buf[40];
        quest_reply_line(buf, 40);
        say_hero(buf[0] ? buf : REPLIES[gc_rnd() % 5], 2000);
      }
      h->phase = 6;
      return;
    case 6:
      if (!ui_dialogue_done()) return;
      h->phase = 7;
      return;
    default:   // 7
      if (n) npc_set_talking(s_slot, 0);
      gq_emit(GQ_EV_TALKED, s_npc, 0);
      h->pose = POSE_NORMAL;
      if (gc_rnd() % 3 == 0) { h->emote = I_HEART; h->timer = 1200; h->phase = 8; }
      else finish(h);
      return;
    case 8:
      if (h->timer <= 0) finish(h);
      return;
  }
}

// ------------------------------------------------------------------ fighting
static void release_monster(void) { if (s_slot > 0 && s_slot < A_COUNT && A[s_slot].kind == AK_MONSTER) npc_set_talking(s_slot, 0); }

static void fight_step(Actor* h, uint32_t dt) {
  Actor* m = (s_slot > 0 && s_slot < A_COUNT) ? &A[s_slot] : 0;
  switch (h->phase) {
    case 0: {
      int area = GQ_AREA_FOREST;
      if (s_fight_area != 0xFF && s_fight_area < GQ_AREA_COUNT) area = s_fight_area;
      else {
        static const uint8_t CAND[4] = {GQ_AREA_FOREST, GQ_AREA_SHORE, GQ_AREA_CAVE, GQ_AREA_RUINS};
        int open[4], k = 0;
        for (int i = 0; i < 4; i++) if (world_area_open(CAND[i])) open[k++] = CAND[i];
        area = open[rnd_range(0, k - 1)];
      }
      tv_start(area, SP_ENTRY, 0xFF);
      h->phase = 1;
      return;
    }
    case 1: {
      int r = tv_step(h, dt);
      if (r < 0) { finish(h); return; }
      if (r > 0) h->phase = 2;
      return;
    }
    case 2: {
      int slot = npc_monster_slot(0);
      if (slot < 0) { finish(h); return; }
      s_slot = (uint8_t)slot;
      npc_set_talking(slot, 1);
      h->phase = 3;
      return;
    }
    case 3: {
      if (!m || m->kind != AK_MONSTER) { finish(h); return; }
      if (!aligned(m)) return;
      int ox, oy;
      if (!stand_next_to(m, &ox, &oy) || !find_path(h, ox, oy)) { release_monster(); finish(h); return; }
      h->phase = 4;
      return;
    }
    case 4: {
      if (!m || m->kind != AK_MONSTER) { finish(h); return; }
      if (!arrived(h)) return;
      if (!adjacent(h, m)) {
        int ox, oy;
        if (++h->counter > 4 || !stand_next_to(m, &ox, &oy) || !find_path(h, ox, oy)) { release_monster(); finish(h); }
        return;
      }
      face(h, tile_x(m), tile_y(m));
      int p = 55 + 12 * econ_tool(GQ_TOOL_SWORD) + 5 * (int)G.s.hero_hearts - (m->id == GQ_MON_BOSS ? 30 : 0);
      s_win = (int)(gc_rnd() % 100) < (p > 95 ? 95 : p);
      s_swings = 0;
      h->pose = POSE_SWING;
      h->timer = 260;
      h->phase = 5;
      return;
    }
    case 5:
      if (!m || m->kind != AK_MONSTER) { finish(h); return; }
      if (h->timer > 0) return;
      s_swings++;
      m->visible = (uint8_t)(s_swings & 1 ? 0 : 1);   // it flashes when struck
      h->timer = 240;
      if (s_swings >= 3) {
        m->visible = 1;
        if (s_win) { h->pose = POSE_CHEER; m->emote = WI_POOF; h->timer = 500; h->phase = 6; }
        else { h->pose = POSE_HURT; h->timer = 800; h->phase = 8; }
      }
      return;
    case 6:   // victory: the monster poofs away and drops something
      if (h->timer > 0) return;
      if (m && m->kind == AK_MONSTER) {
        int mon = m->id;
        m->visible = 0; m->emote = I_NONE; m->act = 1; m->talking = 0;
        m->timer = rnd_range(60000, 120000);
        if (s_find_day != (uint16_t)(G.s.day + 1)) { s_find_day = (uint16_t)(G.s.day + 1); s_find_n = 0; }
        int coins = rnd_range(1, 3);
        if (s_find_n + coins <= 10) { s_find_n = (uint8_t)(s_find_n + coins); econ_earn(coins, GQ_SRC_FIND); h->emote = I_COIN; }
        if (mon == GQ_MON_SLIME && gc_rnd() % 10 < 4) gain(GQ_ITEM_GEL, 1);
        gq_emit(GQ_EV_SLAIN, mon, 0);
      }
      h->timer = 1100;
      h->phase = 7;
      return;
    case 7:
      if (h->timer > 0) return;
      h->emote = I_NONE; h->pose = POSE_NORMAL;
      if (s_fight_quest && ++h->counter < 3) { h->phase = 2; return; }
      finish(h);
      return;
    case 8:   // defeat
      if (h->timer > 0) return;
      release_monster();
      h->pose = POSE_NORMAL;
      gq_emit(GQ_EV_HURT, 0, 0);
      if (G.s.hero_hearts <= 1) {
        say_hero("OUCH! HOME I GO.", 2200);
        gq_emit(GQ_EV_FAINTED, 0, 0);
        h->phase = 9;
      } else {
        G.s.hero_hearts--;
        gq_touch();
        finish(h);
      }
      return;
    default:  // 9: fainted, walks home after the line
      if (!ui_dialogue_done()) return;
      go_home(h);
      return;
  }
}

// -------------------------------------------------------------------- errands
static void errand_done(Actor* h) {
  uint8_t errands = G.s.errands_today;
  uint32_t earned0 = G.s.earned_total;
  gq_emit(GQ_EV_QUEST_DONE, 0xFF, 0);   // the quest module pays (at most 3 errands a day)
  int paid = G.s.errands_today != errands;
  char msg[40] = "BACK FROM THE|";
  int n = str_len(msg);
  str_copy(msg + n, hero_place_name(2), 40 - n);
  n = str_len(msg);
  if (paid) {
    str_copy(msg + n, "! +", 40 - n);
    n = str_len(msg);
    itoa_into(msg + n, (int)(G.s.earned_total - earned0));
    n = str_len(msg);
    str_copy(msg + n, " COINS", 40 - n);
  } else {
    str_copy(msg + n, "! A FINE WALK.", 40 - n);
  }
  h->dir = DIR_DOWN;
  say_hero(msg, 3000);
  h->emote = I_HEART;
}

static void quest_step(Actor* h, uint32_t dt) {
  switch (h->phase) {
    case 0: {
      quest_need(&s_need);
      int k = s_need.kind;
      if (k == GQ_NEED_TALK && s_need.npc >= 1 && s_need.npc <= 8) { s_force_npc = s_need.npc; begin(h, GQ_ACT_CHAT); return; }
      if (k == GQ_NEED_ITEM) {
        static const uint8_t SRC[13] = {GQ_ACT_CHOP, GQ_ACT_MINE, GQ_ACT_FISH, GQ_ACT_FORAGE, GQ_ACT_FORAGE, GQ_ACT_FORAGE,
                                        GQ_ACT_FORAGE, GQ_ACT_MINE, GQ_ACT_MINE, GQ_ACT_GARDEN, GQ_ACT_SELL, GQ_ACT_FORAGE, GQ_ACT_FIGHT};
        begin(h, s_need.item < 13 ? (int)SRC[s_need.item] : (int)GQ_ACT_WANDER);
        return;
      }
      if (k == GQ_NEED_SLAY && s_need.area < GQ_AREA_COUNT && world_area_open(s_need.area)) {
        s_fight_area = s_need.area; s_fight_quest = 1; begin(h, GQ_ACT_FIGHT); return;
      }
      if (k == GQ_NEED_DO && s_need.activity > 0 && s_need.activity < GQ_ACT_COUNT && s_need.activity != GQ_ACT_QUEST &&
          s_need.activity != GQ_ACT_SLEEP) { begin(h, s_need.activity); return; }
      if (k == GQ_NEED_GOTO && s_need.area < GQ_AREA_COUNT && world_area_open(s_need.area)) {
        tv_start(s_need.area, s_need.tx, s_need.ty);
        h->phase = 10;
        return;
      }
      if (econ_activity_bias(GQ_ACT_QUEST) <= -9) { finish(h); return; }
      tv_start(GQ_AREA_FOREST, SP_TARGET_B, 0xFF);     // an errand into the forest
      h->phase = 1;
      return;
    }
    case 1: {
      int r = tv_step(h, dt);
      if (r < 0) { finish(h); return; }
      if (r > 0) { h->emote = WI_SCROLL; h->dir = DIR_DOWN; h->timer = 3000; h->phase = 2; }
      return;
    }
    case 2:
      if (h->timer > 0) return;
      h->emote = I_NONE;
      tv_start(GQ_AREA_VILLAGE, SP_TARGET_B, 0xFF);
      h->phase = 3;
      return;
    case 3: {
      int r = tv_step(h, dt);
      if (r < 0) { go_home(h); return; }
      if (r > 0) { errand_done(h); h->phase = 4; }
      return;
    }
    case 4:
      if (!ui_dialogue_done()) return;
      finish(h);
      return;
    case 10: {   // go to a quest tile
      int r = tv_step(h, dt);
      if (r < 0) { finish(h); return; }
      if (r > 0) {
        gq_emit(GQ_EV_ARRIVED, world_area(), tile_y(h) * GC_COLS + tile_x(h));
        h->pose = POSE_CHEER; h->timer = 1000; h->phase = 11;
      }
      return;
    }
    default:
      if (h->timer <= 0) finish(h);
      return;
  }
}

// -------------------------------------------------------------------- the rest
static void chopped(void) {
  gain(GQ_ITEM_WOOD, 1 + econ_tool(GQ_TOOL_AXE));
  gq_emit(GQ_EV_CHOPPED, 0, 0);
}

static int pick_tree(int* sx, int* sy, int* tx, int* ty) {
  static const int8_t DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
  for (int tries = 0; tries < 60; tries++) {
    int x = rnd_range(0, GC_COLS - 1), y = rnd_range(0, GC_ROWS - 2);
    if (g_obj[y][x] != T_TREE) continue;
    for (int d = 0; d < 4; d++) {
      int nx = x + DX[d], ny = y + DY[d];
      if (walkable(nx, ny) && g_obj[ny][nx] == T_NONE) { *sx = nx; *sy = ny; *tx = x; *ty = y; return 1; }
    }
  }
  return 0;
}

static void hero_step(Actor* h, uint32_t dt) {
  h->timer -= (int32_t)dt;
  switch (h->act) {
    case ACT_NONE:
      if (h->timer <= 0 && (ui_dialogue_done() || h->timer < -20000)) {
        int a = pick_activity();
        s_fight_area = 0xFF;
        begin(h, a);
      }
      break;

    case ACT_HOME: {
      if (h->phase == 0) { tv_start(GQ_AREA_VILLAGE, SP_ENTRY, 0xFF); h->phase = 1; break; }
      int r = tv_step(h, dt);
      if (r != 0) {
        if (r < 0) world_instant_area(GQ_AREA_VILLAGE, SP_ENTRY);   // last resort: he is home
        finish(h);
      }
      break;
    }

    case GQ_ACT_WANDER:
      if (h->phase == 0) {
        int r = cal_weather() == GQ_W_FOG ? 1 : 3, ok = 0;
        for (int i = 0; i < 20 && !ok; i++) {
          int x = tile_x(h) + rnd_range(-r, r), y = tile_y(h) + rnd_range(-r, r);
          ok = find_path(h, x, y);
        }
        if (!ok) { finish(h); break; }
        h->phase = 1;
      } else if (arrived(h)) {
        if (++h->counter < 2) h->phase = 0; else finish(h);
      }
      break;

    case GQ_ACT_CHOP:
      switch (h->phase) {
        case 0: {
          int here = world_area();
          int area = (here == GQ_AREA_FOREST || (here == GQ_AREA_VILLAGE && (gc_rnd() & 1))) ? here : GQ_AREA_VILLAGE;
          if (here == GQ_AREA_VILLAGE && (gc_rnd() & 1)) area = GQ_AREA_FOREST;
          tv_start(area, SP_ENTRY, 0xFF);
          h->phase = 1;
          break;
        }
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) {
            int sx, sy;
            if (!pick_tree(&sx, &sy, &s_tx, &s_ty)) { finish(h); break; }
            tv_start(world_area(), sx, sy);
            h->phase = 2;
          }
          break;
        }
        case 2: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) {
            if (g_obj[s_ty][s_tx] != T_TREE) { finish(h); break; }
            face(h, s_tx, s_ty);
            h->item = I_AXE_UP; h->counter = 0; h->timer = 380; h->phase = 3;
          }
          break;
        }
        case 3:
          if (h->timer > 0) break;
          h->item = h->item == I_AXE_UP ? I_AXE_DOWN : I_AXE_UP;
          h->timer = 380;
          if (++h->counter >= 10) {
            world_obj_change(s_tx, s_ty, T_STUMP, 150);
            if (world_area() == GQ_AREA_VILLAGE) {
              h->item = I_LOG;
              tv_start(GQ_AREA_VILLAGE, SP_PILE, 0xFF);
              h->phase = 4;
            } else {
              h->item = I_NONE; chopped(); h->emote = I_WOOD; h->timer = 1000; h->phase = 5;
            }
          }
          break;
        case 4: {
          int r = tv_step(h, dt);
          if (r < 0) { h->item = I_NONE; chopped(); finish(h); break; }
          if (r > 0) { h->dir = DIR_UP; h->item = I_NONE; chopped(); h->emote = I_WOOD; h->timer = 1200; h->phase = 5; }
          break;
        }
        default:
          if (h->timer <= 0) finish(h);
          break;
      }
      break;

    case GQ_ACT_FISH:
      switch (h->phase) {
        case 0:
          tv_start(world_area_open(GQ_AREA_SHORE) && (gc_rnd() % 5) < 2 ? GQ_AREA_SHORE : GQ_AREA_VILLAGE, SP_DOCK, 0xFF);
          h->phase = 1;
          break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) {
            h->dir = DIR_RIGHT; h->item = I_ROD; h->timer = rnd_range(3500, 8000);
            h->counter = (uint8_t)rnd_range(2, 3); h->phase = 2;
          }
          break;
        }
        case 2:
          h->bob = (uint8_t)(((uint32_t)h->timer / 500) & 1);
          if (h->timer <= 0) {
            int odds = 50 + 8 * econ_tool(GQ_TOOL_ROD) + (cal_weather() == GQ_W_RAIN ? 20 : 0);
            if ((int)(gc_rnd() % 100) < odds) {
              h->emote = I_FISH;
              gain(GQ_ITEM_FISH, 1);
              int sp = cal_season() * 3 + rnd_range(0, econ_tool(GQ_TOOL_ROD) >= 2 ? 2 : 1);
              gq_emit(GQ_EV_CAUGHT, sp, 0);
            } else h->emote = I_BUBBLE;
            h->timer = 1400; h->phase = 3;
          }
          break;
        default:
          if (h->timer > 0) break;
          h->emote = I_NONE;
          if (--h->counter > 0) { h->timer = rnd_range(3000, 7000); h->phase = 2; } else finish(h);
          break;
      }
      break;

    case GQ_ACT_FORAGE:
      switch (h->phase) {
        case 0:
          tv_start(world_area_open(GQ_AREA_SHORE) && (gc_rnd() & 3) == 0 ? GQ_AREA_SHORE : GQ_AREA_FOREST, SP_ENTRY, 0xFF);
          h->phase = 1;
          break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) h->phase = 2;
          break;
        }
        case 2: {   // next node
          int sx, sy, ox, oy;
          if (h->counter >= 3 || !(find_node(WT_MUSHROOM, WT_HERB, 0, &sx, &sy, &ox, &oy) ||
                                   find_node(WT_SAND_SHELL, WT_SAND_SHELL, 0, &sx, &sy, &ox, &oy) ||
                                   find_node(WT_BUSH, WT_BUSH, 1, &sx, &sy, &ox, &oy))) { finish(h); break; }
          s_tx = ox; s_ty = oy;
          tv_start(world_area(), sx, sy);
          h->phase = 3;
          break;
        }
        case 3: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) {
            uint8_t o = g_obj[s_ty][s_tx];
            int item = -1, icon = I_BUBBLE;
            if (o == WT_MUSHROOM) { item = GQ_ITEM_MUSHROOM; icon = WI_MUSHROOM; }
            else if (o == WT_HERB) { item = GQ_ITEM_HERB; icon = WI_HERB; }
            else if (o == WT_SAND_SHELL) { item = GQ_ITEM_SHELL; icon = WI_SHELL; }
            else if (o == WT_BUSH) { item = cal_season() == GQ_SPRING ? GQ_ITEM_FLOWER : GQ_ITEM_BERRY; icon = item == GQ_ITEM_FLOWER ? WI_FLOWER : WI_BERRY; }
            if (item < 0) { finish(h); break; }
            face(h, s_tx, s_ty);
            gain(item, 1);
            gq_emit(GQ_EV_FORAGED, item, 0);
            if (o != WT_BUSH) world_obj_change(s_tx, s_ty, T_NONE, 600);
            h->emote = (int8_t)icon; h->counter++; h->timer = 900; h->phase = 4;
          }
          break;
        }
        default:
          if (h->timer > 0) break;
          h->emote = I_NONE;
          h->phase = 2;
          break;
      }
      break;

    case GQ_ACT_MINE:
      switch (h->phase) {
        case 0:
          tv_start(GQ_AREA_CAVE, SP_ENTRY, 0xFF);
          h->phase = 1;
          break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) h->phase = 2;
          break;
        }
        case 2: {
          int sx, sy, ox, oy;
          if (h->counter >= 2 || !find_node(WT_ORE_ROCK, WT_GEM_ROCK, 1, &sx, &sy, &ox, &oy)) { finish(h); break; }
          s_tx = ox; s_ty = oy;
          tv_start(world_area(), sx, sy);
          h->phase = 3;
          break;
        }
        case 3: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) { face(h, s_tx, s_ty); h->item = WI_PICK_UP; s_swings = 0; h->timer = 400; h->phase = 4; }
          break;
        }
        case 4:
          if (h->timer > 0) break;
          h->item = h->item == WI_PICK_UP ? WI_PICK_DOWN : WI_PICK_UP;
          h->timer = 400;
          if (++s_swings >= 8) {
            uint8_t o = g_obj[s_ty][s_tx];
            int pick = econ_tool(GQ_TOOL_PICK);
            int item = o == WT_GEM_ROCK && pick >= 2 ? GQ_ITEM_GEM : (o == WT_ORE_ROCK && pick >= 2 ? GQ_ITEM_ORE : GQ_ITEM_STONE);
            gain(item, 1);
            gq_emit(GQ_EV_MINED, item, 0);
            if (o == WT_GEM_ROCK || o == WT_ORE_ROCK) world_obj_change(s_tx, s_ty, T_NONE, 900);
            h->item = I_NONE;
            h->emote = (int8_t)(item == GQ_ITEM_GEM ? WI_GEM : (item == GQ_ITEM_ORE ? WI_ORE : WI_STONE));
            h->counter++; h->timer = 1000; h->phase = 5;
          }
          break;
        default:
          if (h->timer > 0) break;
          h->emote = I_NONE;
          h->phase = 2;
          break;
      }
      break;

    case GQ_ACT_GARDEN:
      switch (h->phase) {
        case 0:
          tv_start(GQ_AREA_VILLAGE, SP_ENTRY, 0xFF);
          h->phase = 1;
          break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) h->phase = 2;
          break;
        }
        case 2: {
          int sx, sy, ox, oy;
          if (h->counter >= 4 || !find_node(WT_CROP_2, WT_CROP_2, 1, &sx, &sy, &ox, &oy)) { finish(h); break; }
          s_tx = ox; s_ty = oy;
          tv_start(world_area(), sx, sy);
          h->phase = 3;
          break;
        }
        case 3: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) {
            face(h, s_tx, s_ty);
            if (g_obj[s_ty][s_tx] == WT_CROP_2) {
              gain(GQ_ITEM_CROP, 1);
              gq_emit(GQ_EV_HARVESTED, 0, 0);
              world_obj_change(s_tx, s_ty, WT_CROP_0, 300);
              h->emote = WI_CROP;
            }
            h->counter++; h->timer = 1200; h->phase = 4;
          }
          break;
        }
        default:
          if (h->timer > 0) break;
          h->emote = I_NONE;
          h->phase = 2;
          break;
      }
      break;

    case GQ_ACT_SELL:
      switch (h->phase) {
        case 0: tv_start(GQ_AREA_SHOP, SP_COUNTER, 0xFF); h->phase = 1; break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) { h->dir = DIR_UP; h->timer = 1500; h->phase = 2; }
          break;
        }
        case 2:
          if (h->timer > 0) break;
          h->emote = econ_sell_all() > 0 ? I_COIN : I_BUBBLE;
          g_dirty |= 1u;
          h->timer = 1500; h->phase = 3;
          break;
        default:
          if (h->timer <= 0) finish(h);
          break;
      }
      break;

    case GQ_ACT_BUY:
      switch (h->phase) {
        case 0: {
          GqGoal g;
          if (!econ_goal(&g)) { finish(h); break; }
          s_choice = g.is_project;
          if (s_choice) tv_start(GQ_AREA_VILLAGE, SP_SITE, 0xFF); else tv_start(GQ_AREA_SHOP, SP_COUNTER, 0xFF);
          h->phase = 1;
          break;
        }
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) {
            if (s_choice) { h->dir = DIR_DOWN; h->item = WI_HAMMER; h->timer = 3500; }
            else { h->dir = DIR_UP; h->timer = 2500; }
            h->phase = 2;
          }
          break;
        }
        case 2:
          if (h->timer > 0) break;
          h->item = I_NONE;
          {
            GqGoal bought;
            int had_goal = econ_goal(&bought);
            if (econ_buy_goal()) {
              // "BOUGHT|AXE II!", "BUILT|WELL!" or, in village-fund mode, "VILLAGE FUND|+1 STAR!"
              char msg[40];
              if (had_goal) {
                str_copy(msg, bought.is_project ? "BUILT|" : "BOUGHT|", 40);
                int n = str_len(msg);
                econ_goal_name(bought.index, msg + n, 40 - n);
                n = str_len(msg);
                str_copy(msg + n, "!", 40 - n);
              } else {
                str_copy(msg, "VILLAGE FUND|+1 STAR!", 40);
              }
              say_hero(msg, 2500);
              h->pose = POSE_CHEER;
              h->emote = I_HEART;
              h->phase = 3;
            } else finish(h);
          }
          break;
        default:
          if (ui_dialogue_done()) finish(h);
          break;
      }
      break;

    case GQ_ACT_EAT:
      switch (h->phase) {
        case 0:
          if (econ_meal_price() <= 0 || econ_meal_price() > (int)G.s.coins) { finish(h); break; }
          tv_start(GQ_AREA_TAVERN, SP_TABLE, 0xFF);
          h->phase = 1;
          break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) { h->pose = POSE_SIT; h->dir = DIR_UP; h->timer = rnd_range(8000, 12000); h->phase = 2; }
          break;
        }
        case 2:
          if (h->timer > 0) break;
          h->pose = POSE_NORMAL;
          {
            int price = econ_meal_price();
            if (price > 0 && econ_pay(price)) {
              gq_emit(GQ_EV_ATE, 0, 0);
              if (G.s.hero_hearts < hero_max_hearts()) { G.s.hero_hearts++; gq_touch(); }
              h->emote = I_HEART;
            }
          }
          h->timer = 1500; h->phase = 3;
          break;
        default:
          if (h->timer <= 0) finish(h);
          break;
      }
      break;

    case ACT_BREAKFAST:
      switch (h->phase) {
        case 0: tv_start(GQ_AREA_HOME, SP_TABLE, 0xFF); h->phase = 1; break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { s_bf_day = (uint16_t)(G.s.day + 1); finish(h); break; }
          if (r > 0) { h->pose = POSE_SIT; h->dir = DIR_RIGHT; h->timer = 5000; h->phase = 2; }
          break;
        }
        case 2:
          if (h->timer > 0) break;
          s_bf_day = (uint16_t)(G.s.day + 1);
          h->pose = POSE_NORMAL; h->emote = I_HEART;
          if (G.s.hero_hearts < hero_max_hearts()) { G.s.hero_hearts++; gq_touch(); }
          h->timer = 1200; h->phase = 3;
          break;
        default:
          if (h->timer <= 0) finish(h);
          break;
      }
      break;

    case GQ_ACT_SIT:
      switch (h->phase) {
        case 0: {
          int wx = cal_weather();
          int indoors = wx == GQ_W_RAIN || wx == GQ_W_STORM || cal_minute() >= 1290 || (gc_rnd() % 10) < 3;
          s_choice = (uint8_t)indoors;
          if (indoors) tv_start(GQ_AREA_HOME, SP_BENCH, 0xFF);
          else {
            int bx = 6;
            for (int i = 1; i < A_COUNT; i++) if (A[i].kind != AK_NONE && tile_x(&A[i]) == 6 && tile_y(&A[i]) == 8) bx = 8;
            tv_start(GQ_AREA_VILLAGE, bx, 8);
          }
          h->phase = 1;
          break;
        }
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) {
            h->pose = POSE_SIT; h->dir = DIR_UP; h->timer = rnd_range(15000, 25000);
            if (!s_choice && cal_minute() >= 1110 && cal_weather() == GQ_W_CLEAR) h->emote = I_STAR;   // stargazing
            h->phase = 2;
          }
          break;
        }
        default:
          if (h->timer <= 0) finish(h);
          break;
      }
      break;

    case GQ_ACT_CHAT: chat_step(h, dt, 0); break;
    case GQ_ACT_GIFT: chat_step(h, dt, 1); break;
    case GQ_ACT_QUEST: quest_step(h, dt); break;
    case GQ_ACT_FIGHT: fight_step(h, dt); break;

    case GQ_ACT_CAT:
      switch (h->phase) {
        case 0: tv_start(GQ_AREA_VILLAGE, SP_ENTRY, 0xFF); h->phase = 1; break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { finish(h); break; }
          if (r > 0) h->phase = 2;
          break;
        }
        case 2: {
          int slot = -1;
          for (int i = 1; i < A_COUNT; i++) if (A[i].kind == AK_CAT) slot = i;
          if (slot < 0) { finish(h); break; }
          s_slot = (uint8_t)slot;
          npc_set_talking(slot, 1);
          h->phase = 3;
          break;
        }
        case 3: {
          Actor* c = &A[s_slot];
          if (c->kind != AK_CAT) { finish(h); break; }
          if (!aligned(c)) break;
          int ox, oy;
          if (!stand_next_to(c, &ox, &oy) || !find_path(h, ox, oy)) { finish(h); break; }
          h->phase = 4;
          break;
        }
        case 4: {
          Actor* c = &A[s_slot];
          if (c->kind != AK_CAT) { finish(h); break; }
          if (!arrived(h)) break;
          if (!adjacent(h, c)) {
            int ox, oy;
            if (++h->counter > 4 || !stand_next_to(c, &ox, &oy) || !find_path(h, ox, oy)) finish(h);
            break;
          }
          face(h, tile_x(c), tile_y(c));
          h->emote = I_HEART; h->pose = POSE_CHEER; h->timer = 5000; h->phase = 5;
          break;
        }
        default:
          if (h->timer <= 0) finish(h);
          break;
      }
      break;

    case ACT_FEST:
      if (h->phase == 0) { tv_start(GQ_AREA_VILLAGE, SP_PLAZA, 0xFF); h->phase = 1; }
      else if (h->phase == 1) {
        int r = tv_step(h, dt);
        if (r < 0) { finish(h); h->timer = 20000; }
        else if (r > 0) { h->dir = DIR_DOWN; h->phase = 2; }
      } else if (cal_festival() < 0 || cal_is_night()) finish(h);
      break;

    case GQ_ACT_SLEEP:
      switch (h->phase) {
        case 0: tv_start(GQ_AREA_HOME, SP_BED, 0xFF); h->phase = 1; break;
        case 1: {
          int r = tv_step(h, dt);
          if (r < 0) { go_home(h); break; }
          if (r > 0) {
            h->pose = POSE_BED; h->dir = DIR_DOWN; h->emote = I_ZZZ;
            G.s.hero_hearts = (uint8_t)hero_max_hearts();
            gq_touch();
            gq_emit(GQ_EV_SLEPT, 0, 0);
            h->timer = rnd_range(180000, 300000);
            h->phase = 2;
          }
          break;
        }
        case 2:   // asleep in the bedroom
          if (!cal_is_night()) {
            h->pose = POSE_NORMAL; h->emote = I_NONE;
            gq_emit(GQ_EV_WOKE, 0, 0);
            finish(h);
          } else if (h->timer <= 0 && world_change_area(GQ_AREA_VILLAGE, SP_ENTRY, 0)) {   // night camera: cut to the village
            h->timer = 60000;
            h->phase = 3;
          }
          break;
        case 3:   // the camera shows the night village for a minute
          if (world_transition_busy()) break;
          if (h->timer <= 0 || !cal_is_night()) {
            if (world_change_area(GQ_AREA_HOME, SP_BED, 1)) h->phase = 4;
          }
          break;
        default:  // 4: back in the bedroom
          if (world_transition_busy()) break;
          h->pose = POSE_BED; h->emote = I_ZZZ; h->ex = h->ey = -1;
          if (!cal_is_night()) {
            h->pose = POSE_NORMAL; h->emote = I_NONE;
            gq_emit(GQ_EV_WOKE, 0, 0);
            finish(h);
          } else { h->timer = rnd_range(180000, 300000); h->phase = 2; }
          break;
      }
      break;

    default:
      finish(h);
      break;
  }
}

// -------------------------------------------------------------------- watchdog
static void watchdog(Actor* h, uint32_t dt) {
  if (h->act == ACT_NONE) return;
  s_act_ms += (int32_t)dt;
  uint16_t key = (uint16_t)((h->act << 8) | h->phase);
  if (key != s_key) { s_key = key; s_phase_ms = 0; } else s_phase_ms += (int32_t)dt;
  if (h->act == ACT_FEST || (h->act == GQ_ACT_SLEEP && h->phase == 2)) return;
  if (h->act == ACT_HOME) {
    if (s_act_ms > 60000) { world_instant_area(GQ_AREA_VILLAGE, SP_ENTRY); finish(h); }
    return;
  }
  if (s_phase_ms > 120000 || (h->act != GQ_ACT_SLEEP && s_act_ms > 240000)) go_home(h);
}

void hero_tick(Actor* h, uint32_t dt) {
  watchdog(h, dt);
  hero_step(h, dt);
}

void hero_reset(void) {
  Actor* h = &A[A_HERO];
  clear_talk();
  s_act_ms = s_phase_ms = 0; s_key = 0xFFFF;
  s_last1 = s_last2 = 0;
  s_bf_day = s_gift_day = s_find_day = 0; s_find_n = 0;
  s_slot = s_npc = 0; s_item_gift = 0xFF; s_force_npc = 0; s_fight_area = 0xFF; s_fight_quest = 0;
  T.on = 0;
  h->kind = AK_HERO; h->visible = 1; h->act = ACT_NONE; h->phase = 0; h->counter = 0;
  h->pose = POSE_NORMAL; h->item = h->emote = I_NONE; h->ex = h->ey = -1;
  h->timer = 1500;
}

// Away progress: the hero is always on screen now, the postcard of chunk 0 is gone.
int hero_away_progress(int* place_txt) { if (place_txt) *place_txt = 0; return -1; }

// Presence (GQ_Q_HERO_PRESENCE): the hero is always accounted for. Called after the whole world tick.
void hero_update_presence(void) {
  const Actor* h = &A[A_HERO];
  uint8_t p;
  if (world_transition_busy()) p = GQ_PRES_CURTAIN;
  else if (h->visible && h->x > -16 && h->x < GC_W && h->y > -16 && h->y < GC_H) p = GQ_PRES_SPRITE;
  else if (!h->visible && h->emote != I_NONE) p = GQ_PRES_DOOR;   // asleep: the Zzz above his door
  else p = GQ_PRES_NONE;
  G.presence = p;
}

int hero_activity(void) {
  int a = A[A_HERO].act;
  if (a == ACT_BREAKFAST) return GQ_ACT_EAT;
  if (a == ACT_FEST) return GQ_ACT_SIT;
  if (a == ACT_HOME) return GQ_ACT_NONE;
  return a >= 0 && a < GQ_ACT_COUNT ? a : GQ_ACT_NONE;
}

// gc_debug(GQ_DBG_FORCE_ACT): start an activity now, from a clean state at the home door.
void hero_force_activity(int gq_act) {
  if (gq_act < 1 || gq_act >= GQ_ACT_COUNT) return;
  Actor* h = &A[A_HERO];
  clear_talk();
  h->visible = 1;
  world_instant_area(GQ_AREA_VILLAGE, SP_ENTRY);
  tidy(h);
  h->dir = DIR_DOWN;
  s_fight_area = 0xFF;
  begin(h, gq_act);
}
