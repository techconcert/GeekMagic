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
  ACT_CHAT, ACT_QUEST, ACT_SLEEP, ACT_BUY
};

// internal activity -> GQ_ACT_*
static const uint8_t ACT_TO_GQ[] = {
  GQ_ACT_NONE, GQ_ACT_WANDER, GQ_ACT_CHOP, GQ_ACT_FISH, GQ_ACT_SELL, GQ_ACT_EAT, GQ_ACT_SIT,
  GQ_ACT_CHAT, GQ_ACT_QUEST, GQ_ACT_SLEEP, GQ_ACT_BUY
};

// quest places (stopgap for the postcard; the areas of chunk 3 replace them)
static const char* const PLACE_NAME[4] = {"CAVES", "RUINS", "SWAMP", "TOWER"};
static const uint8_t PLACE_ICON[4] = {T_ROCK, T_FENCE, T_WATER, T_WALL_DOOR};
const char* hero_place_name(int place) { return PLACE_NAME[place & 3]; }
int hero_place_icon(int place) { return PLACE_ICON[place & 3]; }

static uint8_t  s_place;          // quest place of the current trip
static int32_t  s_away_total;     // ms the current trip takes

// Hide the hero behind a door: a marker above the door alternates between his face and
// the icon of what he is doing inside (design 11.2 step 7), so he never just vanishes.
static void hide_hero(Actor* h, int door_x, int icon) {
  h->visible = 0;
  h->marker_icon = (int8_t)icon;
  h->mark_acc = 0;
  h->mark_alt = 0;
  h->emote = I_FACE;
  h->ex = (int16_t)(door_x * 16);
  h->ey = (int16_t)(DOOR_Y * 16 - 14);
}

static void show_hero(Actor* h) {
  h->visible = 1;
  h->marker_icon = I_NONE;
  h->emote = I_NONE;
  h->ex = -1;
}

static void hero_choose(Actor* h) {
  h->phase = 0;
  h->timer = 0;
  h->counter = 0;
  h->item = I_NONE;
  if (cal_is_night()) { h->act = ACT_SLEEP; return; }
  int hr = cal_minute() / 60;
  // weights: wander, chop, fish, shop, tavern, sit, chat, quest, buy
  int w[9] = {2, 0, 0, 0, 0, 0, 1, 0, 0};
  int goods = econ_item(GQ_ITEM_WOOD) + econ_item(GQ_ITEM_FISH);
  if (hr < 10)      { w[1] = 5; w[3] = goods ? 2 : 0; w[6] = 3; }
  else if (hr < 14) { w[2] = 3; w[3] = goods > 2 ? 3 : 0; w[7] = 3; w[1] = 1; }
  else if (hr < 18) { w[2] = 4; w[1] = 3; w[7] = 2; w[3] = goods > 3 ? 2 : 0; }
  else              { w[4] = 3; w[5] = 4; w[6] = 2; w[3] = goods ? 1 : 0; }
  // the economy says what is pointless right now (-9) or worth doing
  if (econ_activity_bias(GQ_ACT_CHOP) <= -9) w[1] = 0;
  if (econ_activity_bias(GQ_ACT_FISH) <= -9) w[2] = 0;
  if (econ_activity_bias(GQ_ACT_SELL) <= -9) w[3] = 0;
  if (econ_activity_bias(GQ_ACT_EAT) <= -9 || econ_meal_price() > (int)G.s.coins) w[4] = 0;
  if (econ_activity_bias(GQ_ACT_QUEST) <= -9) w[7] = 0;
  if (econ_activity_bias(GQ_ACT_BUY) > -9) w[8] = 6;
  int total = 0;
  for (int i = 0; i < 9; i++) total += w[i];
  int r = (int)(gc_rnd() % (uint32_t)total);
  static const uint8_t acts[9] = {ACT_WANDER, ACT_CHOP, ACT_FISH, ACT_SHOP, ACT_TAVERN, ACT_SIT, ACT_CHAT, ACT_QUEST, ACT_BUY};
  for (int i = 0; i < 9; i++) {
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

static void chopped(void) {
  econ_add_item(GQ_ITEM_WOOD, 1);
  gq_emit(GQ_EV_CHOPPED, 0, 0);
}

static void finish(Actor* h) { h->act = ACT_NONE; h->item = I_NONE; h->timer = rnd_range(800, 2500); }

static void hero_step(Actor* h, uint32_t dt) {
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
          if (!find_path(h, PILE_FRONT_X, STREET_Y)) { h->item = I_NONE; chopped(); finish(h); break; }
          h->phase = 3;
        }
      } else if (h->phase == 3 && arrived(h)) {
        h->dir = DIR_UP;
        h->item = I_NONE;
        chopped();
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
          if (gc_rnd() % 10 < 6) { h->emote = I_FISH; econ_add_item(GQ_ITEM_FISH, 1); gq_emit(GQ_EV_CAUGHT, 0xFF, 0); }
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
        hide_hero(h, SHOP_DOOR_X, I_COIN);
        h->timer = 3000;
        h->phase = 2;
      } else if (h->phase == 2 && h->timer <= 0) {
        show_hero(h);
        h->dir = DIR_DOWN;
        int earned = econ_sell_all();
        g_dirty |= 1u;
        h->emote = earned > 0 ? I_COIN : I_BUBBLE;
        h->timer = 1500;
        h->phase = 3;
      } else if (h->phase == 3 && h->timer <= 0) {
        h->emote = I_NONE;
        finish(h);
      }
      break;

    case ACT_BUY:
      if (h->phase == 0) {
        if (!find_path(h, SHOP_DOOR_X, STREET_Y)) { finish(h); break; }
        h->phase = 1;
      } else if (h->phase == 1 && arrived(h)) {
        h->dir = DIR_UP;
        hide_hero(h, SHOP_DOOR_X, I_COIN);
        h->timer = 6000;
        h->phase = 2;
      } else if (h->phase == 2 && h->timer <= 0) {
        show_hero(h);
        h->dir = DIR_DOWN;
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
          ui_say(A_HERO, msg, 2500);
          h->emote = I_HEART;
          h->phase = 3;
        } else {
          finish(h);
        }
      } else if (h->phase == 3 && ui_dialogue_done()) {
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
        hide_hero(h, TAVERN_DOOR_X, I_HEART);
        h->timer = rnd_range(8000, 12000);
        h->phase = 2;
      } else if (h->phase == 2 && h->timer <= 0) {
        show_hero(h);
        h->dir = DIR_DOWN;
        int price = econ_meal_price();
        if (price > 0 && econ_pay(price)) { gq_emit(GQ_EV_ATE, 0, 0); h->emote = I_HEART; }
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
      } else if (h->phase == 2 && h->x >= GC_W) {
        h->manual_dx = 0;
        h->x = (int16_t)GC_W;
        h->visible = 0;
        s_place = (uint8_t)(gc_rnd() % 4);
        s_away_total = rnd_range(40000, 90000);
        h->timer = s_away_total;
        h->phase = 3;
      } else if (h->phase == 3 && h->timer <= 0) {
        h->visible = 1;
        h->x = (int16_t)(GC_W - 1);
        h->manual_dx = -1;                  // walk back in
        h->phase = 4;
      } else if (h->phase == 4 && h->x <= EXIT_X * 16) {
        h->x = (int16_t)(EXIT_X * 16);
        h->manual_dx = 0;
        uint8_t errands = G.s.errands_today;
        uint32_t earned0 = G.s.earned_total;
        gq_emit(GQ_EV_QUEST_DONE, 0xFF, 0);   // the quest module pays (at most 3 errands a day)
        int paid = G.s.errands_today != errands;
        char msg[40] = "BACK FROM THE|";
        int n = str_len(msg);
        str_copy(msg + n, hero_place_name(s_place), 40 - n);
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
        hide_hero(h, HOME_DOOR_X, I_ZZZ);
        gq_emit(GQ_EV_SLEPT, 0, 0);
        h->phase = 2;
      } else if (h->phase == 2 && !cal_is_night()) {
        show_hero(h);
        h->dir = DIR_DOWN;
        gq_emit(GQ_EV_WOKE, 0, 0);
        finish(h);
      }
      break;
  }
}


// The door marker flips every 600 ms.
static void marker_tick(Actor* h, uint32_t dt) {
  if (h->visible || h->marker_icon == I_NONE) return;
  h->mark_acc = (uint16_t)(h->mark_acc + dt);
  while (h->mark_acc >= 600) { h->mark_acc = (uint16_t)(h->mark_acc - 600); h->mark_alt ^= 1; }
  h->emote = h->mark_alt ? h->marker_icon : (int8_t)I_FACE;
}

void hero_tick(Actor* h, uint32_t dt) {
  hero_step(h, dt);
  marker_tick(h, dt);
}

// Away progress 0..255 while the hero is off screen on a quest, -1 otherwise (design 11.2 step 7).
int hero_away_progress(int* place_txt) {
  const Actor* h = &A[A_HERO];
  if (place_txt) *place_txt = s_place;
  if (h->act != ACT_QUEST) return -1;
  if (h->phase == 3) {
    if (s_away_total <= 0) return 255;
    int32_t left = h->timer < 0 ? 0 : (h->timer > s_away_total ? s_away_total : h->timer);
    return 255 - (int)((int64_t)left * 255 / s_away_total);
  }
  // overshoot while walking off or on: he is already (still) outside the screen
  if (h->phase == 2 && (h->x >= GC_W || h->x <= -16)) return 0;
  if (h->phase == 4 && (h->x >= GC_W || h->x <= -16)) return 255;
  return -1;
}

// Presence (GQ_Q_HERO_PRESENCE): the hero is always accounted for on screen. Called after the
// whole world tick, when the final positions of this tick are known.
void hero_update_presence(void) {
  const Actor* h = &A[A_HERO];
  uint8_t p;
  if (h->visible && h->x > -16 && h->x < GC_W && h->y > -16 && h->y < GC_H) p = GQ_PRES_SPRITE;
  else if (hero_away_progress(0) >= 0) p = GQ_PRES_POSTCARD;
  else if (!h->visible && h->emote != I_NONE) p = GQ_PRES_DOOR;
  else p = GQ_PRES_NONE;
  G.presence = p;
}

int hero_activity(void) {
  int a = A[A_HERO].act;
  return a >= 0 && a < (int)sizeof(ACT_TO_GQ) ? (int)ACT_TO_GQ[a] : (int)GQ_ACT_NONE;
}

// gc_debug(GQ_DBG_FORCE_ACT): start an activity now, from a clean state at the home door.
void hero_force_activity(int gq_act) {
  int act = -1;
  for (int i = 1; i < (int)sizeof(ACT_TO_GQ); i++) if (ACT_TO_GQ[i] == gq_act) act = i;
  if (act < 0) return;
  Actor* h = &A[A_HERO];
  show_hero(h);
  h->manual_dx = 0;
  h->item = I_NONE;
  place(h, HOME_DOOR_X, STREET_Y);
  h->dir = DIR_DOWN;
  h->phase = 0;
  h->counter = 0;
  h->timer = 0;
  h->act = (uint8_t)act;
}
