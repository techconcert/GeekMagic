// Scene glue: init, tick and render order of the village scene (design 6.3, 10.4),
// event queue, save export/import, gc_query and gc_debug.
#include "world_priv.h"

GqState G;

// Chunk 2 / chunk 5 debug hooks (declared here because gq_mod.h is frozen).
void ui_debug_show_card(int id);     // ui.cpp
int  ui_card_id(void);               // ui.cpp
// quest.cpp (chunk 5) provides the real one; this weak default keeps this tree linking until it lands.
__attribute__((weak)) void quest_debug_start(int q) { (void)q; }

static uint32_t s_play_ms;

// ------------------------------------------------------------------ events
struct QEv { int16_t ev, a, b; };
static QEv     s_q[8];
static uint8_t s_qh, s_qn, s_busy;
static uint16_t s_dropped;

void gq_touch(void) { G.serial++; }

static void dispatch(int ev, int a, int b) {
  econ_on_event(ev, a, b);
  quest_on_event(ev, a, b);
  cal_on_event(ev, a, b);
  world_on_event(ev, a, b);
  ui_on_event(ev, a, b);
}

// Delivers queued events one at a time. Events raised by a handler wait in the queue
// (8 deep, a full queue drops the event and counts it) and are delivered after the
// current one. The loop is bounded: at most 64 deliveries per call.
void gq_pump(void) {
  if (s_busy) return;
  s_busy = 1;
  for (int guard = 0; s_qn && guard < 64; guard++) {
    QEv e = s_q[s_qh];
    s_qh = (uint8_t)((s_qh + 1) & 7);
    s_qn--;
    dispatch(e.ev, e.a, e.b);
  }
  if (s_qn) { s_dropped = (uint16_t)(s_dropped + s_qn); s_qn = 0; }
  s_busy = 0;
}

void gq_emit(int ev, int a, int b) {
  if (ev < 0 || ev >= GQ_EV_COUNT) return;
  if (s_qn >= 8) { if (s_dropped < 65535) s_dropped++; return; }
  QEv* e = &s_q[(s_qh + s_qn) & 7];
  e->ev = (int16_t)ev; e->a = (int16_t)a; e->b = (int16_t)b;
  s_qn++;
  gq_pump();
}

// ------------------------------------------------------------- village scene
static void apply_look(void) {
  GqLook look;
  cal_look(&look);
  gfx_set_look(&look);
}

static void init_modules(void) {
  cal_init();
  econ_init();
  quest_init();
  world_init();
  ui_init();
  apply_look();
}

void village_init(uint32_t seed) {
  (void)seed;
  gq_save_defaults(&G.s);
  G.presence = GQ_PRES_SPRITE;
  s_play_ms = 0;
  s_qh = s_qn = s_busy = 0;
  init_modules();
}

void village_tick(uint32_t dt) {
  s_play_ms += dt;
  while (s_play_ms >= 60000) {
    s_play_ms -= 60000;
    if (G.s.play_min < 0xFFFFFFFFu) G.s.play_min++;
  }
  cal_tick(dt);
  gq_pump();
  apply_look();
  weather_tick(dt);
  world_tick(dt);
  ui_tick(dt);
  gq_pump();
}

void village_render(int s, uint16_t* out) {
  if (ui_card_covers(s)) { ui_render(s, out); return; }
  if (ui_curtain_covers(s)) {
    for (int i = 0; i < GC_STRIP_PIXELS; i++) out[i] = 0;
  } else {
    world_render(s, out);
    weather_render(s, out);
  }
  ui_render(s, out);
}

// --------------------------------------------------------------- persistence
extern "C" int gc_save_export(uint8_t* buf, int cap) {
  if (gc_scene_id() != GC_SCENE_VILLAGE || !buf || cap < GQ_SAVE_SIZE) return 0;
  GqSave tmp = G.s;
  gq_save_sanitize(&tmp);
  tmp.seq = G.s.seq + 1;
  tmp.rng = gc_rng_get();
  const uint8_t* p = (const uint8_t*)&tmp;
  for (int i = 0; i < GQ_SAVE_SIZE; i++) buf[i] = p[i];
  tmp.crc32 = gq_crc32(buf + 12, GQ_SAVE_SIZE - 12);
  buf[8] = (uint8_t)tmp.crc32; buf[9] = (uint8_t)(tmp.crc32 >> 8);
  buf[10] = (uint8_t)(tmp.crc32 >> 16); buf[11] = (uint8_t)(tmp.crc32 >> 24);
  G.s.seq = tmp.seq;
  return GQ_SAVE_SIZE;
}

extern "C" int gc_save_check(const uint8_t* buf, int len) { return gq_save_validate(buf, len); }

extern "C" int gc_save_import(const uint8_t* buf, int len) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return 0;
  if (gq_save_validate(buf, len) != GC_SAVE_OK) return 0;
  GqSave tmp;
  uint8_t* d = (uint8_t*)&tmp;
  for (int i = 0; i < GQ_SAVE_SIZE; i++) d[i] = buf[i];
  gq_save_sanitize(&tmp);
  G.s = tmp;                 // from here on nothing can fail
  G.presence = GQ_PRES_SPRITE;
  s_play_ms = 0;
  s_qh = s_qn = s_busy = 0;
  init_modules();
  gc_rng_set(tmp.rng);       // the modules consumed random numbers; resume exactly where the save was
  gc_mark_all_dirty();
  return 1;
}

extern "C" uint32_t gc_save_serial(void) { return G.serial; }

extern "C" void gc_reset_game(void) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return;
  village_init(0);
  gq_touch();
  gc_mark_all_dirty();
}

// --------------------------------------------------------------- environment
extern "C" void gc_set_date(int year, int month, int day, int weekday, uint32_t epoch) {
  if (year < 0 || year > 9999) year = 0;
  if (month < 1 || month > 12) month = 1;
  if (day < 1 || day > 31) day = 1;
  if (weekday < 0 || weekday > 6) weekday = 0;
  G.year = (int16_t)year; G.month = (uint8_t)month; G.mday = (uint8_t)day; G.wday = (uint8_t)weekday;
  G.s.epoch = year ? epoch : 0;
}

extern "C" void gc_set_clock_quality(int approximate) { G.clock_approx = approximate ? 1 : 0; }

extern "C" void gc_set_net(int state) {
  if (state < GC_NET_ONLINE || state > GC_NET_AP_ONLY) state = GC_NET_OFFLINE;
  G.net = (uint8_t)state;
}

extern "C" void gc_set_option(int key, int value) {
  if (key == GC_OPT_CARD_PERIOD) G.card_period = (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
  else if (key == GC_OPT_NIGHT_QUIET) G.night_quiet = value ? 1 : 0;
}

// ------------------------------------------------------------ introspection
static int popcount8(const uint8_t* p, int n) {
  int c = 0;
  for (int i = 0; i < n; i++) for (int b = 0; b < 8; b++) c += (p[i] >> b) & 1;
  return c;
}

static int32_t sat32(uint32_t v) { return v > 0x7FFFFFFFu ? 0x7FFFFFFF : (int32_t)v; }

extern "C" int32_t gc_query(int key) {
  const GqSave& s = G.s;
  if (key >= GQ_Q_ITEM_BASE && key < GQ_Q_ITEM_BASE + GQ_ITEM_COUNT) return s.inv[key - GQ_Q_ITEM_BASE];
  if (key >= GQ_Q_TOOL_BASE && key < GQ_Q_TOOL_BASE + GQ_TOOL_COUNT) return s.tool[key - GQ_Q_TOOL_BASE];
  if (key >= GQ_Q_STAT_BASE && key < GQ_Q_STAT_BASE + GQ_STAT_COUNT) return s.stat[key - GQ_Q_STAT_BASE];
  if (key >= GQ_Q_FRIEND_BASE && key < GQ_Q_FRIEND_BASE + GQ_NPC_COUNT) return s.friend_[key - GQ_Q_FRIEND_BASE];
  GqGoal g;
  switch (key) {
    case GQ_Q_COINS: return s.coins;
    case GQ_Q_COIN_CAP: return econ_coin_cap();
    case GQ_Q_STARS: return s.stars;
    case GQ_Q_FUND: return s.fund;
    case GQ_Q_EARNED_TOTAL: return sat32(s.earned_total);
    case GQ_Q_SPENT_TOTAL: return sat32(s.spent_total);
    case GQ_Q_DAY: return s.day;
    case GQ_Q_MINUTE: return s.minute;
    case GQ_Q_SEASON: return cal_season();
    case GQ_Q_WEATHER: return cal_weather();
    case GQ_Q_FESTIVAL: return cal_festival();
    case GQ_Q_EVENT: return cal_event();
    case GQ_Q_HERO_PRESENCE: return G.presence;
    case GQ_Q_HERO_AREA: return world_area();
    case GQ_Q_HERO_ACT: return hero_activity();
    case GQ_Q_HERO_HEARTS: return s.hero_hearts;
    case GQ_Q_HERO_TILE: {
      int x = tile_x(&A[A_HERO]), y = tile_y(&A[A_HERO]);
      if (x < 0) x = 0;
      if (x >= GC_COLS) x = GC_COLS - 1;
      if (y < 0) y = 0;
      if (y >= GC_ROWS) y = GC_ROWS - 1;
      return y * GC_COLS + x;
    }
    case GQ_Q_GOAL: return s.goal;
    case GQ_Q_GOAL_PRICE: return econ_goal(&g) ? g.price : 0;
    case GQ_Q_PROJECTS: return s.projects;
    case GQ_Q_CHAPTER: return s.chapter;
    case GQ_Q_QUEST: return s.q_active;
    case GQ_Q_QUEST_STEP: return s.q_step;
    case GQ_Q_QUEST_COUNT: return s.q_count;
    case GQ_Q_TILL_LEFT: return s.till_left;
    case GQ_Q_ERRANDS_TODAY: return s.errands_today;
    case GQ_Q_ACH_COUNT: return popcount8(s.ach, 4);
    case GQ_Q_SEEN_COUNT: return popcount8(s.seen, 8);
    case GQ_Q_CARD: return ui_card_id();
    case GQ_Q_ACTORS_VISIBLE: return world_actor_count();
    default: return 0;
  }
}

static void new_day(void) {
  if (G.s.day < 65535) G.s.day++;
  gq_touch();
  gq_emit(GQ_EV_NEW_DAY, G.s.day & 0xFF, cal_season());
}

extern "C" void gc_debug(int cmd, int32_t arg) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return;
  switch (cmd) {
    case GQ_DBG_ADD_COINS: econ_earn(arg, GQ_SRC_DEBUG); break;
    case GQ_DBG_SET_DAY:
      G.s.day = (uint16_t)(arg < 0 ? 0 : (arg > 65535 ? 65535 : arg));
      gq_touch();
      gq_emit(GQ_EV_NEW_DAY, G.s.day & 0xFF, cal_season());
      break;
    case GQ_DBG_SET_WEATHER:
      if (arg >= 0 && arg < GQ_W_COUNT) { G.s.weather = (uint8_t)arg; gq_touch(); gq_emit(GQ_EV_WEATHER, arg, 0); }
      break;
    case GQ_DBG_ADD_ITEM: econ_add_item((arg >> 8) & 0xFF, arg & 0xFF); gq_touch(); break;
    case GQ_DBG_FORCE_ACT: hero_force_activity(arg); break;
    case GQ_DBG_BUY_GOAL: econ_buy_goal(); break;
    case GQ_DBG_NEXT_DAY: new_day(); break;
    case GQ_DBG_START_QUEST: quest_debug_start(arg); break;
    case GQ_DBG_SHOW_CARD: ui_debug_show_card(arg); break;
    default: break;
  }
}
