// Quests, story and all text (chunk 5, design 4.1 to 4.3 and 11.7).
//
// One quest is active at a time (G.s.q_active, q_step, q_count). Priority: story > side quest >
// legend > errand. The module never moves the hero: quest_need() says what would make progress and
// the hero scheduler (chunk 3) follows it, then reports back through events (TALKED, ARRIVED, SLAIN,
// SCENE_DONE and the activity events). Every table index and every saved value is validated on use.
//
// Conventions fixed here (the frozen headers leave them open):
//  - G.s.chapter is the story chapter that was started last (0..5); 6 once the bell has rung.
//  - GqNeed for SLAY: `item` = monster id, `count` = kills still missing, `area` = where.
//    For ITEM: `item`/`count` = what to hold, `area`/`activity` = where and how to get it.
//    For SCENE steps the need is GOTO with `npc` = scene id (scene 1 = the bell is rung).
//  - quest_talk(GQ_NPC_HERO, ...) returns the hero's own line for a step that has just started.
//  - Project indices follow the order of design 3.5 (QP_* in quest_data.h).
#include "world_priv.h"
#include "text_data.h"
#include "quest_data.h"
#include "talk_data.h"   // enums only (QK_*): the tables belong to talk.cpp

enum { NO_QUEST = 0xFF, SCENE_BELL = 1, MAX_AGE_DAYS = 4, BELL_X = 7, BELL_Y = 6 };

// ----------------------------------------------------------------- volatile state (about 70 bytes)
static uint8_t  s_err[3];            // errand template ids (0..7) drawn for today, 0xFF = none
static uint8_t  s_err_done;          // bit per slot: already done today
static uint16_t s_err_day = 0xFFFF;  // day the errands were drawn for
static uint8_t  s_age;               // game days the active quest has been active
static uint32_t s_skip;              // quests given up for now (cleared every 7 days)
static uint16_t s_blocked_day = 0xFFFF;  // fainted today: fight and activity steps wait until tomorrow
static uint8_t  s_pend_hero = 0xFF;  // text id of the hero's line for the step that just started
static uint8_t  s_thanks_npc = 0xFF; // who says thanks for the quest that just ended
static uint8_t  s_chat_kind = QK_THANKS, s_chat_npc;   // what the last quest line was about (quest_chat_kind)
static char     s_tmp[48];           // scratch for the band text

static void select_quest(void);

// ----------------------------------------------------------------------------- table access
static int qd_giver(int q) { return GC_RD8(&QUESTS[q].giver); }
static int qd_first(int q) { return GC_RD8(&QUESTS[q].first_step); }
static int qd_n(int q) { return GC_RD8(&QUESTS[q].n_steps); }
static int qd_req_kind(int q) { return GC_RD8(&QUESTS[q].req_kind); }
static int qd_req_val(int q) { return GC_RD8(&QUESTS[q].req_val); }

static int st_type(int i) { return GC_RD8(&QSTEPS[i].type); }
static int st_a(int i) { return GC_RD8(&QSTEPS[i].a); }
static int st_b(int i) { return GC_RD8(&QSTEPS[i].b); }
static int st_c(int i) { return GC_RD8(&QSTEPS[i].c); }

int quest_count(void) { return QUEST_COUNT; }
int quest_steps(int q) { return q >= 0 && q < QUEST_COUNT ? qd_n(q) : 0; }

// ---------------------------------------------------------------------------------- text
const char* gq_text(int id, char* buf, int cap) {
  if (cap <= 0) return buf;
  int n = 0;
  if (id >= 0 && id < TX_COUNT) {
    int o = GC_RD16(&TEXT_OFF[id]);
    while (n < cap - 1 && o + n < (int)sizeof(TEXT_BLOB)) {
      char c = (char)GC_RD8(&TEXT_BLOB[o + n]);
      if (!c) break;
      buf[n++] = c;
    }
  }
  buf[n] = 0;
  return buf;
}

const char* gq_npc_name(int npc) {
  static char name[8];
  gq_text(npc >= 0 && npc < GQ_NPC_COUNT ? TX_NAME0 + npc : -1, name, 8);
  return name;
}

// ---------------------------------------------------------------------------- world rules
static int season(void) { return (G.s.day / 7) & 3; }
static int is_done(int q) { return q >= 0 && q < 64 && (G.s.q_done[q >> 3] >> (q & 7)) & 1; }
static void set_done(int q) { if (q >= 0 && q < 64) G.s.q_done[q >> 3] = (uint8_t)(G.s.q_done[q >> 3] | (1u << (q & 7))); }
static int tool_lvl(int t) { return t >= 0 && t < GQ_TOOL_COUNT ? G.s.tool[t] : 0; }

static int area_unlocked(int a) {
  switch (a) {
    case GQ_AREA_VILLAGE: case GQ_AREA_FOREST: case GQ_AREA_HOME: case GQ_AREA_SHOP: case GQ_AREA_TAVERN: return 1;
    case GQ_AREA_SHORE: return econ_project_built(QP_BRIDGE);
    case GQ_AREA_CAVE: return tool_lvl(GQ_TOOL_PICK) >= 1;
    case GQ_AREA_RUINS: return tool_lvl(GQ_TOOL_LANTERN) >= 1;
    default: return 0;
  }
}

static const uint8_t NPC_AREA[GQ_NPC_COUNT] GC_ROM_ATTR = {
  GQ_AREA_VILLAGE, GQ_AREA_SHOP, GQ_AREA_VILLAGE, GQ_AREA_VILLAGE, GQ_AREA_TAVERN,
  GQ_AREA_SHORE, GQ_AREA_VILLAGE, GQ_AREA_FOREST, GQ_AREA_VILLAGE, GQ_AREA_VILLAGE
};
static int npc_area(int npc) { return npc >= 0 && npc < GQ_NPC_COUNT ? (int)GC_RD8(&NPC_AREA[npc]) : (int)GQ_AREA_VILLAGE; }

static const uint8_t MON_AREA[GQ_MON_COUNT] GC_ROM_ATTR = {
  GQ_AREA_FOREST, GQ_AREA_CAVE, GQ_AREA_SHORE, GQ_AREA_RUINS, GQ_AREA_RUINS
};
static int mon_area(int m) { return m >= 0 && m < GQ_MON_COUNT ? (int)GC_RD8(&MON_AREA[m]) : (int)GQ_AREA_FOREST; }

// Where an item comes from: area and activity. Goods 0..12 only; key items have no source.
static const uint8_t ITEM_SRC[13][2] GC_ROM_ATTR = {
  {GQ_AREA_VILLAGE, GQ_ACT_CHOP}, {GQ_AREA_CAVE, GQ_ACT_MINE}, {GQ_AREA_VILLAGE, GQ_ACT_FISH},
  {GQ_AREA_FOREST, GQ_ACT_FORAGE}, {GQ_AREA_FOREST, GQ_ACT_FORAGE}, {GQ_AREA_FOREST, GQ_ACT_FORAGE},
  {GQ_AREA_SHORE, GQ_ACT_FORAGE}, {GQ_AREA_CAVE, GQ_ACT_MINE}, {GQ_AREA_CAVE, GQ_ACT_MINE},
  {GQ_AREA_VILLAGE, GQ_ACT_GARDEN}, {GQ_AREA_SHOP, GQ_ACT_BUY}, {GQ_AREA_FOREST, GQ_ACT_FORAGE},
  {GQ_AREA_FOREST, GQ_ACT_FIGHT}
};

// 1 when the item can be obtained right now (area open, tool owned, season right).
static int item_obtainable(int item, int* area, int* act) {
  if (item < 0 || item > 12) { if (area) *area = GQ_AREA_VILLAGE; if (act) *act = GQ_ACT_NONE; return item >= 16 && item < GQ_ITEM_COUNT; }
  int a = GC_RD8(&ITEM_SRC[item][0]), x = GC_RD8(&ITEM_SRC[item][1]);
  if (area) *area = a;
  if (act) *act = x;
  if (!area_unlocked(a)) return 0;
  if ((item == GQ_ITEM_ORE || item == GQ_ITEM_GEM) && tool_lvl(GQ_TOOL_PICK) < 2) return 0;
  if (item == GQ_ITEM_CROP && !econ_project_built(QP_GARDEN)) return 0;
  int s = season();
  if (item == GQ_ITEM_MUSHROOM && s != GQ_SPRING && s != GQ_AUTUMN) return 0;
  if (item == GQ_ITEM_BERRY && s != GQ_SUMMER) return 0;
  if (item == GQ_ITEM_FLOWER && s != GQ_SPRING && s != GQ_SUMMER) return 0;
  return 1;
}

static int activity_area(int act) {
  switch (act) {
    case GQ_ACT_FORAGE: return GQ_AREA_FOREST;
    case GQ_ACT_MINE: return GQ_AREA_CAVE;
    case GQ_ACT_SELL: return GQ_AREA_SHOP;
    case GQ_ACT_EAT: return GQ_AREA_TAVERN;
    default: return GQ_AREA_VILLAGE;
  }
}

static int req_ok(int kind, int val) {
  switch (kind) {
    case QR_NONE: return 1;
    case QR_DAY: return G.s.day >= val;
    case QR_QUEST: return is_done(val);
    case QR_TOOL: return tool_lvl(val >> 4) >= (val & 15);
    case QR_FRIEND: return econ_friend(val >> 4) >= (val & 15) * 25;
    case QR_PROJECT: return econ_project_built(val);
    case QR_SEASON: return season() == val;
    default: return 0;
  }
}

static int step_feasible(int i) {
  int t = st_type(i);
  switch (t) {
    case QS_TALK: return area_unlocked(npc_area(st_a(i)));
    case QS_FETCH: return item_obtainable(st_a(i), 0, 0);
    case QS_DELIVER: return area_unlocked(npc_area(st_a(i))) && (st_b(i) >= 16 || item_obtainable(st_b(i), 0, 0) || econ_item(st_b(i)) >= st_c(i));
    case QS_GOTO: return area_unlocked(st_a(i));
    case QS_SLAY: return area_unlocked(mon_area(st_a(i)));
    case QS_DO: return area_unlocked(activity_area(st_a(i)));
    case QS_SCENE: return 1;
    default: return 1;
  }
}

static int quest_feasible(int q) {
  int f = qd_first(q), n = qd_n(q);
  for (int k = 0; k < n && f + k < QSTEP_COUNT; k++) if (!step_feasible(f + k)) return 0;
  return 1;
}

static int is_story(int q) { return q >= QUEST_STORY0 && q < QUEST_SIDE0; }
static int is_errand(int q) { return q >= QUEST_ERRAND0 && q < QUEST_LEGEND0; }
static int is_legend(int q) { return q >= QUEST_LEGEND0 && q < QUEST_COUNT; }

static int story_gate_ok(int k) {
  if (G.s.day < GC_RD8(&STORY_GATE[k][0])) return 0;
  int need = GC_RD8(&STORY_GATE[k][1]), val = GC_RD8(&STORY_GATE[k][2]);
  if (need == 1) return tool_lvl(val >> 4) >= (val & 15);
  if (need == 2) return econ_project_built(val);
  return 1;
}

// ----------------------------------------------------------------- active quest, validated
static int cur_index(void) {   // index into QSTEPS of the active step, or -1 (and the state is repaired)
  int q = G.s.q_active;
  if (q != NO_QUEST) {
    if (q < QUEST_COUNT && G.s.q_step < qd_n(q) && qd_first(q) + G.s.q_step < QSTEP_COUNT) return qd_first(q) + G.s.q_step;
    G.s.q_active = NO_QUEST; G.s.q_step = 0; G.s.q_count = 0;
    gq_touch();
  }
  return -1;
}

static int slay_needed(int i) {
  int n = st_b(i);
  if (n == 0) { n = 1 + G.s.day / 28; if (n > 12) n = 12; if (n < 2) n = 2; }
  return n;
}

static int do_event(int act) {
  switch (act) {
    case GQ_ACT_CHOP: return GQ_EV_CHOPPED;
    case GQ_ACT_FISH: return GQ_EV_CAUGHT;
    case GQ_ACT_FORAGE: return GQ_EV_FORAGED;
    case GQ_ACT_MINE: return GQ_EV_MINED;
    case GQ_ACT_GARDEN: return GQ_EV_HARVESTED;
    case GQ_ACT_SELL: return GQ_EV_SOLD;
    case GQ_ACT_EAT: return GQ_EV_ATE;
    case GQ_ACT_CHAT: return GQ_EV_TALKED;
    case GQ_ACT_GIFT: return GQ_EV_GIFTED;
    default: return -1;
  }
}


static void enter_step(int i) {
  int t = st_type(i);
  s_pend_hero = (t == QS_TALK || t == QS_DELIVER) ? 0xFF : (uint8_t)GC_RD8(&QSTEPS[i].txt_start);
}

static void pay_errand(void) {
  if (G.s.errands_today >= 3) return;
  G.s.errands_today++;
  if (G.s.stat[GQ_STAT_ERRANDS] < 65535) G.s.stat[GQ_STAT_ERRANDS]++;
  econ_earn(6 + (int)(gc_rnd() % 9), GQ_SRC_QUEST);
  gq_touch();
}

static void complete(void) {
  int q = G.s.q_active;
  if (q >= QUEST_COUNT) { cur_index(); return; }
  int giver = qd_giver(q);
  if (is_errand(q)) {
    pay_errand();
    for (int s = 0; s < 3; s++) if (s_err[s] == q - QUEST_ERRAND0 && !((s_err_done >> s) & 1)) { s_err_done = (uint8_t)(s_err_done | (1u << s)); break; }
  } else {
    int coins = GC_RD8(&QUESTS[q].reward_coins5) * 5;
    if (coins > 0) econ_earn(coins, GQ_SRC_QUEST);
  }
  int item = GC_RD8(&QUESTS[q].reward_item);
  if (item != 0xFF) econ_add_item(item, 1);
  if (q >= 1 && q <= 4) econ_add_item(GQ_ITEM_SHARD, 1);   // one shard per chapter 1..4
  econ_friend_add(giver, is_errand(q) ? 1 : (is_story(q) ? 3 : 5));
  if (q < QUEST_ERRAND0) set_done(q);
  if (q == 5) G.s.chapter = 6;
  if (is_legend(q)) G.s.year_legends = (uint8_t)(G.s.year_legends | (1u << (q - QUEST_LEGEND0)));
  G.s.q_active = NO_QUEST; G.s.q_step = 0; G.s.q_count = 0;
  s_pend_hero = 0xFF;
  s_thanks_npc = (uint8_t)giver;
  gq_touch();
  gq_emit(GQ_EV_QUEST_DONE, q, 0);
  select_quest();
}

static void advance(void) {
  int q = G.s.q_active;
  G.s.q_step++;
  G.s.q_count = 0;
  gq_touch();
  if (q >= QUEST_COUNT || G.s.q_step >= qd_n(q)) { complete(); return; }
  enter_step(qd_first(q) + G.s.q_step);
  gq_emit(GQ_EV_QUEST_STEP, q, G.s.q_step);
}

// Steps that finish by themselves because their condition already holds (holding the items).
static void settle(void) {
  for (int guard = 0; guard < 8; guard++) {
    int i = cur_index();
    if (i < 0 || st_type(i) != QS_FETCH) return;
    if (econ_item(st_a(i)) < st_b(i)) return;
    advance();
  }
}

static void start(int q) {
  G.s.q_active = (uint8_t)q; G.s.q_step = 0; G.s.q_count = 0;
  s_age = 0;
  s_thanks_npc = 0xFF;
  if (is_story(q)) G.s.chapter = (uint8_t)q;
  int i = qd_first(q);
  if (st_type(i) == QS_DELIVER && st_b(i) == GQ_ITEM_PARCEL) econ_add_item(GQ_ITEM_PARCEL, 1);
  enter_step(i);
  gq_touch();
  gq_emit(GQ_EV_QUEST_START, q, 0);
}

// -------------------------------------------------------------------------------- selection
static uint32_t day_hash(void) { return (uint32_t)G.s.day * 2654435761u; }

static void draw_errands(void) {
  s_err_day = G.s.day;
  s_err[0] = s_err[1] = s_err[2] = 0xFF;
  s_err_done = 0;
  for (int s = 0; s < 3 && s < G.s.errands_today; s++) s_err_done = (uint8_t)(s_err_done | (1u << s));
  int rot = (int)((day_hash() >> 16) & 7), n = 0;
  for (int j = 0; j < 8 && n < 3; j++) {
    int e = (rot + j) & 7, q = QUEST_ERRAND0 + e;
    if (req_ok(qd_req_kind(q), qd_req_val(q)) && quest_feasible(q)) s_err[n++] = (uint8_t)e;
  }
}

static void sync_legends(void) {
  int y = (G.s.day / 28) & 15;
  if ((G.s.year_legends >> 4) != y) { G.s.year_legends = (uint8_t)(y << 4); gq_touch(); }
}

static void select_quest(void) {
  int k = 0;
  while (k < 6 && is_done(k)) k++;
  int story_ready = k < 6 && story_gate_ok(k) && quest_feasible(k);
  if (cur_index() >= 0) {
    // the story preempts an errand or a side quest that has not really begun
    int q = G.s.q_active;
    if (!story_ready || is_story(q) || (!is_errand(q) && G.s.q_step > 0)) return;
    G.s.q_active = NO_QUEST; G.s.q_step = 0; G.s.q_count = 0; s_pend_hero = 0xFF;
    gq_touch();
  }
  // 1. story
  if (story_ready) { start(k); return; }
  // 2. side quests whose requirement holds
  for (int q = QUEST_SIDE0; q < QUEST_ERRAND0; q++) {
    if (is_done(q) || ((s_skip >> q) & 1)) continue;
    if (req_ok(qd_req_kind(q), qd_req_val(q)) && quest_feasible(q)) { start(q); return; }
  }
  // 3. legends, from year 2
  if (G.s.day >= 28) {
    sync_legends();
    int q = QUEST_LEGEND0 + season();
    if (!((G.s.year_legends >> (q - QUEST_LEGEND0)) & 1) && !((s_skip >> q) & 1) && quest_feasible(q)) { start(q); return; }
  }
  // 4. errands, drawn at 06:00, once the notice board stands
  if (G.s.errands_today < 3 && econ_project_built(QP_BOARD)) {
    if (s_err_day != G.s.day && G.s.minute >= 6 * 60) draw_errands();
    if (s_err_day == G.s.day) {
      for (int s = 0; s < 3; s++) {
        if (s_err[s] > 7 || ((s_err_done >> s) & 1)) continue;
        start(QUEST_ERRAND0 + s_err[s]);
        return;
      }
    }
  }
}

void quest_init(void) {
  s_err[0] = s_err[1] = s_err[2] = 0xFF;
  s_err_done = 0; s_err_day = 0xFFFF; s_age = 0; s_skip = 0; s_blocked_day = 0xFFFF;
  s_pend_hero = 0xFF; s_thanks_npc = 0xFF;
  if (G.s.chapter > 6) G.s.chapter = 6;
  if (G.s.q_count > 99) G.s.q_count = 99;
  cur_index();   // repairs an out-of-range q_active / q_step
}

void quest_debug_start(int q) {   // gc_debug(GQ_DBG_START_QUEST): replaces the active quest
  if (q < 0 || q >= QUEST_COUNT) return;
  G.s.q_active = NO_QUEST; G.s.q_step = 0; G.s.q_count = 0;
  start(q);
}

// ------------------------------------------------------------------------------------ events
static void on_new_day(void) {
  if (G.s.day % 7 == 0) s_skip = 0;
  int i = cur_index();
  if (i >= 0) {
    int q = G.s.q_active;
    if (s_age < 255) s_age++;
    if (st_type(i) == QS_WAIT_DAY) { advance(); }
    else if (is_errand(q)) {   // an errand that was not finished is dropped with the day
      G.s.q_active = NO_QUEST; G.s.q_step = 0; G.s.q_count = 0; s_pend_hero = 0xFF;
      gq_touch();
    } else if (!is_story(q) && s_age >= MAX_AGE_DAYS) {   // a side quest that does not progress is given up for a week
      s_skip |= 1u << q;
      G.s.q_active = NO_QUEST; G.s.q_step = 0; G.s.q_count = 0; s_pend_hero = 0xFF;
      gq_touch();
    }
  }
  select_quest();
}

void quest_on_event(int ev, int a, int b) {
  if (ev == GQ_EV_QUEST_DONE) {
    if (a == 0xFF) pay_errand();   // chunk 0 stopgap trip of the hero: an unnamed errand
    return;
  }
  if (ev == GQ_EV_QUEST_START || ev == GQ_EV_QUEST_STEP) return;
  if (ev == GQ_EV_NEW_DAY) { on_new_day(); return; }
  int i = cur_index();
  if (i >= 0) {
    int t = st_type(i);
    switch (ev) {
      case GQ_EV_TALKED:
        if (t == QS_TALK && a == st_a(i)) {
          int give = st_b(i);
          if (give >= 16) econ_add_item(give, 1);
          advance();
        } else if (t == QS_DELIVER && a == st_a(i)) {
          if (econ_item(st_b(i)) >= st_c(i) && econ_take_item(st_b(i), st_c(i))) advance();
        } else if (t == QS_DO && st_a(i) == GQ_ACT_CHAT) {
          if (++G.s.q_count >= st_b(i)) advance(); else gq_touch();
        }
        break;
      case GQ_EV_ARRIVED:
        if (t == QS_GOTO && a == st_a(i)) {
          int dx = (b % GC_COLS) - st_b(i), dy = (b / GC_COLS) - st_c(i);
          if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) advance();
        }
        break;
      case GQ_EV_SLAIN:
        if (t == QS_SLAY && a == st_a(i)) {
          if (G.s.q_count < 99) G.s.q_count++;
          if (G.s.q_count >= slay_needed(i)) advance(); else gq_touch();
        }
        break;
      case GQ_EV_SCENE_DONE:
        if (t == QS_SCENE && a == st_a(i) && (!st_c(i) || G.s.day % 7 == 6)) advance();
        break;
      case GQ_EV_FAINTED:
        if (t == QS_SLAY || t == QS_DO) { G.s.q_count = 0; s_blocked_day = G.s.day; gq_touch(); }
        break;
      default:
        if (t == QS_DO && ev == do_event(st_a(i)) && ev != GQ_EV_TALKED) {
          if (!st_c(i) || G.s.minute < 12 * 60) { if (++G.s.q_count >= st_b(i)) advance(); else gq_touch(); }
        }
        break;
    }
  }
  settle();
  if (ev == GQ_EV_HOUR || ev == GQ_EV_BUILT || ev == GQ_EV_BOUGHT) select_quest();
}

// -------------------------------------------------------------------------------------- need
static void need_set(GqNeed* n, int kind, int area) { *n = GqNeed(); n->kind = (uint8_t)kind; n->area = (uint8_t)area; n->tx = n->ty = 0xFF; n->npc = n->item = 0xFF; }

int quest_need(GqNeed* out) {
  GqNeed tmp;
  GqNeed* n = out ? out : &tmp;
  need_set(n, GQ_NEED_NONE, 0);
  select_quest();
  settle();
  int i = cur_index();
  if (i < 0) return 0;
  int t = st_type(i), a = st_a(i), b = st_b(i), c = st_c(i);
  int area, act;
  switch (t) {
    case QS_TALK:
      need_set(n, GQ_NEED_TALK, npc_area(a)); n->npc = (uint8_t)a;
      break;
    case QS_FETCH:
      need_set(n, GQ_NEED_ITEM, 0);
      item_obtainable(a, &area, &act);
      n->area = (uint8_t)area; n->activity = (uint8_t)act; n->item = (uint8_t)a; n->count = (uint8_t)b;
      break;
    case QS_DELIVER:
      if (econ_item(b) >= c) { need_set(n, GQ_NEED_TALK, npc_area(a)); n->npc = (uint8_t)a; n->item = (uint8_t)b; n->count = (uint8_t)c; }
      else { need_set(n, GQ_NEED_ITEM, 0); item_obtainable(b, &area, &act); n->area = (uint8_t)area; n->activity = (uint8_t)act; n->item = (uint8_t)b; n->count = (uint8_t)c; }
      break;
    case QS_GOTO:
      need_set(n, GQ_NEED_GOTO, a); n->tx = (uint8_t)b; n->ty = (uint8_t)c;
      break;
    case QS_SLAY: {
      int left = slay_needed(i) - G.s.q_count;
      need_set(n, GQ_NEED_SLAY, mon_area(a)); n->item = (uint8_t)a; n->count = (uint8_t)(left > 0 ? left : 1);
      break;
    }
    case QS_DO: {
      int left = b - G.s.q_count;
      need_set(n, GQ_NEED_DO, activity_area(a)); n->activity = (uint8_t)a; n->count = (uint8_t)(left > 0 ? left : 1);
      if (c && G.s.minute >= 12 * 60) n->kind = GQ_NEED_WAIT;   // "before noon" has passed
      break;
    }
    case QS_SCENE:
      need_set(n, GQ_NEED_GOTO, GQ_AREA_VILLAGE); n->tx = BELL_X; n->ty = BELL_Y; n->npc = (uint8_t)a;
      if (c && G.s.day % 7 != 6) n->kind = GQ_NEED_WAIT;     // the bell rings on a festival day
      break;
    default:
      need_set(n, GQ_NEED_WAIT, 0);
      break;
  }
  if ((t == QS_SLAY || t == QS_DO) && s_blocked_day == G.s.day) n->kind = GQ_NEED_WAIT;   // fainted: tomorrow
  if (n->kind != GQ_NEED_WAIT && !area_unlocked(n->area)) n->kind = GQ_NEED_WAIT;           // never point to a locked area
  if (n->kind == GQ_NEED_ITEM && !item_obtainable(n->item, 0, 0)) n->kind = GQ_NEED_WAIT;
  if (n->kind == GQ_NEED_WAIT) { n->area = n->tx = n->ty = n->npc = n->item = 0xFF; n->count = n->activity = 0; }
  return n->kind != GQ_NEED_NONE;
}

// ------------------------------------------------------------------------------ talking, band
// Kind of quest line for talk.cpp's answer of the hero: the step type, or an offer for the first step of a quest.
static int chat_kind_of(int t) {
  switch (t) {
    case QS_TALK: return G.s.q_step == 0 ? QK_OFFER : QK_REPORT;
    case QS_FETCH: return QK_FETCH;
    case QS_DELIVER: return QK_DELIVER;
    case QS_GOTO: return QK_GOTO;
    case QS_SLAY: return QK_SLAY;
    case QS_DO: return QK_DO;
    case QS_WAIT_DAY: return QK_WAIT;
    default: return QK_SCENE;
  }
}

static int say_step(int i, int npc, char* buf, int cap) {
  gq_text(GC_RD8(&QSTEPS[i].txt_start), buf, cap);
  s_chat_kind = (uint8_t)chat_kind_of(st_type(i));
  s_chat_npc = (uint8_t)npc;
  return buf[0] != 0;
}

int quest_talk(int npc, char* buf, int cap) {
  if (cap > 0) buf[0] = 0;
  if (npc < 0 || npc >= GQ_NPC_COUNT) return 0;
  if (npc == GQ_NPC_HERO) {
    if (s_pend_hero == 0xFF) return 0;
    gq_text(s_pend_hero, buf, cap);
    s_pend_hero = 0xFF;
    return buf[0] != 0;
  }
  int i = cur_index();
  if (i >= 0) {
    int t = st_type(i), q = G.s.q_active;
    if ((t == QS_TALK || t == QS_DELIVER) && st_a(i) == npc) return say_step(i, npc, buf, cap);
    if (qd_giver(q) == npc && t != QS_TALK && t != QS_DELIVER) return say_step(i, npc, buf, cap);   // a reminder of the task
  }
  if (s_thanks_npc == npc) {
    s_thanks_npc = 0xFF;
    gq_text(TX_THANKS0 + (int)(gc_rnd() % TX_THANKS_N), buf, cap);
    s_chat_kind = QK_THANKS; s_chat_npc = (uint8_t)npc;
    return buf[0] != 0;
  }
  return 0;
}

// For the dialogue engine (talk.cpp). 0: a quest of this villager is active, 1: a story quest is, 2: no quest is, 3: some other quest is.
int quest_chat_state(int npc) {
  int i = cur_index();
  if (i < 0) return 2;
  int q = G.s.q_active, t = st_type(i);
  if (qd_giver(q) == npc || ((t == QS_TALK || t == QS_DELIVER) && st_a(i) == npc)) return 0;
  return is_story(q) ? 1 : 3;
}

void quest_dbg_set_chat(int kind, int npc) { s_chat_kind = (uint8_t)kind; s_chat_npc = (uint8_t)npc; }   // tests

int quest_chat_kind(int* npc) {
  if (npc) *npc = s_chat_npc;
  return s_chat_kind;
}

int quest_band(char* buf, int cap, int* num, int* den) {
  if (cap > 0) buf[0] = 0;
  if (num) *num = 0;
  if (den) *den = 0;
  int i = cur_index();
  if (i < 0) return 0;
  int t = st_type(i), q = G.s.q_active, have = -1, need = 0;
  if (t == QS_FETCH) { have = econ_item(st_a(i)); need = st_b(i); }
  else if (t == QS_DELIVER) { have = econ_item(st_b(i)); need = st_c(i); }
  else if (t == QS_SLAY) { have = G.s.q_count; need = slay_needed(i); }
  else if (t == QS_DO) { have = G.s.q_count; need = st_b(i); }
  gq_text(GC_RD8(&QSTEPS[i].txt_band), s_tmp, 16);
  int len = str_len(s_tmp);
  if (have >= 0 && need > 1) {
    if (have > need) have = need;
    s_tmp[len++] = ' ';
    itoa_into(s_tmp + len, have); len = str_len(s_tmp);
    s_tmp[len++] = '/';
    itoa_into(s_tmp + len, need);
    if (num) *num = have;
    if (den) *den = need;
  } else {
    if (num) *num = G.s.q_step;
    if (den) *den = qd_n(q);
  }
  str_copy(buf, s_tmp, cap);
  return cap > 0 && buf[0] != 0;
}

int quest_title(char* buf, int cap) {
  if (cap > 0) buf[0] = 0;
  if (cur_index() < 0) return 0;
  gq_text(GC_RD8(&QUESTS[G.s.q_active].title_txt), buf, cap);
  return cap > 0 && buf[0] != 0;
}
