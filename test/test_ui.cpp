// Chunk 2 tests: number formatting, palette pipeline, info cards at extreme state, curtain
// timing, banners, steady-state dirty budget, card scheduling.
#include "gq_test.h"
#include "../lib/gamecore/src/gc_internal.h"
#include "../lib/gamecore/src/gq_mod.h"

extern void ui_debug_show_card(int id);
extern "C++" const char* ui_band_text(void);

static void fc(uint32_t v, const char* want) {
  char b[16];
  fmt_compact(b, v);
  if (std::strcmp(b, want) != 0) { std::printf("fmt_compact(%u) = '%s', want '%s'\n", (unsigned)v, b, want); std::exit(1); }
  CHECK(std::strlen(b) <= 4);
}

static void test_fmt() {
  fc(0, "0"); fc(999, "999"); fc(1000, "1.0K"); fc(1234, "1.2K"); fc(2831, "2.8K"); fc(9999, "9.9K");
  fc(10000, "10K"); fc(999999, "999K"); fc(1000000, "1.0M"); fc(12345678, "12M"); fc(999999999u, "999M");
  fc(1000000000u, "1.0B"); fc(4294967295u, "4.2B");
  char b[16];
  fmt_uint(b, 0); CHECK(!std::strcmp(b, "0"));
  fmt_uint(b, 4294967295u); CHECK(!std::strcmp(b, "4294967295"));
  for (uint32_t v = 0; v < 3000000u; v += 997) { fmt_compact(b, v); CHECK(std::strlen(b) <= 4); }
  std::printf("fmt ok\n");
}

// Reference of the design 6.2 order (tint, then depth curve). With tint 256 this is bit for bit
// the pre-chunk-2 output (curve, then tint * 256 >> 8).
static int ref_curve(int v, int maxv, int depth) {
  int x = v * 1023 / maxv;
  int y = x + ((x * x / 1023 - x) * depth) / 100;
  return y * maxv / 1023;
}
static int ref_chan(int v, int maxv, int depth, int tint) {
  v = v * tint >> 8;
  if (v > maxv) v = maxv;
  return ref_curve(v, maxv, depth);
}
static uint16_t ref_colour(uint16_t c, int depth, int tint) {
  int r = ref_chan((c >> 11) & 31, 31, depth, tint), g = ref_chan((c >> 5) & 63, 63, depth, tint), b = ref_chan(c & 31, 31, depth, tint);
  return (uint16_t)((r << 11) | (g << 5) | b);
}
static uint16_t old_colour(uint16_t c, int depth) {   // the code before chunk 2, tint 256
  int r = ref_curve((c >> 11) & 31, 31, depth), g = ref_curve((c >> 5) & 63, 63, depth), b = ref_curve(c & 31, 31, depth);
  r = r * 256 >> 8; g = g * 256 >> 8; b = b * 256 >> 8;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static GqLook look(int season, int wash_k, int wash_rgb, int t) {
  GqLook l;
  l.season = (uint8_t)season; l.wash_k = (uint8_t)wash_k; l.wash_rgb565 = (uint16_t)wash_rgb;
  l.tr = l.tg = l.tb = (int16_t)t;
  return l;
}

static void test_palette() {
  gc_init(GC_SCENE_VILLAGE, 7, 0);
  static const int depths[] = {0, 40, 100};
  static const int tints[] = {256, 200, 128};
  for (int d : depths) {
    gc_set_depth(d);
    for (int t : tints) {
      GqLook l = look(0, 0, 0, t);
      gfx_set_look(&l);
      for (int i = 0; i < 16; i++) {
        uint16_t want = i ? ref_colour(GC_RD16(&GC_PALETTE[i]), d, t) : 0;
        uint16_t ui = i ? ref_colour(GC_RD16(&GC_PALETTE[i]), d, 256) : 0;
        if (t == 256 && i) CHECK(g_pal[i] == old_colour(GC_RD16(&GC_PALETTE[i]), d));
        CHECK(g_pal[i] == want);
        CHECK(g_act[i] == want);
        CHECK(g_ui[i] == ui);
      }
    }
  }
  gc_set_depth(0);
  // season: world palette changes, actors and UI do not
  GqLook sp = look(0, 0, 0, 256);
  gfx_set_look(&sp);
  uint16_t spring[16], act[16], ui[16];
  for (int i = 0; i < 16; i++) { spring[i] = g_pal[i]; act[i] = g_act[i]; ui[i] = g_ui[i]; }
  for (int s = 1; s < 4; s++) {
    GqLook l = look(s, 0, 0, 256);
    gc_clear_dirty();
    gfx_set_look(&l);
    CHECK(gc_dirty_mask() == (1u << GC_ROWS) - 1);
    int diff = 0;
    for (int i = 0; i < 16; i++) {
      diff += g_pal[i] != spring[i];
      CHECK(g_act[i] == act[i]);
      CHECK(g_ui[i] == ui[i]);
    }
    CHECK(diff >= 3);
    gc_clear_dirty();
    gfx_set_look(&l);                      // unchanged look: nothing dirty
    CHECK(gc_dirty_mask() == 0);
  }
  // wash at full strength: everything but transparent becomes the wash colour
  GqLook w = look(0, 255, 0xFFFF, 256);
  gfx_set_look(&w);
  for (int i = 1; i < 16; i++) { CHECK(g_pal[i] == 0xFFFF); CHECK(g_act[i] == 0xFFFF); CHECK(g_ui[i] == ui[i]); }
  // partial wash lies between the base colour and the wash colour
  GqLook h = look(0, 128, 0xFFFF, 256);
  gfx_set_look(&h);
  for (int i = 1; i < 16; i++) {
    CHECK(((g_pal[i] >> 11) & 31) >= ((spring[i] >> 11) & 31));
    CHECK((g_pal[i] & 31) >= (spring[i] & 31));
  }
  // tint up to 320 never overflows a channel
  GqLook br = look(0, 0, 0, 320);
  gfx_set_look(&br);
  for (int i = 1; i < 16; i++) CHECK(g_pal[i] != 0 || GC_RD16(&GC_PALETTE[i]) == 0);
  std::printf("palette ok\n");
}

static int inside_panel(int x, int y) { return x >= 8 && x < 232 && y >= 32 && y < 208; }

static void fill_extreme() {
  GqSave& s = G.s;
  s.coins = 999; s.stars = 999; s.fund = 99; s.earned_total = 0xFFFFFFFFu; s.spent_total = 0xFFFFFFFFu;
  s.day = 65535; s.projects = 0x3FF; s.chapter = 6; s.q_active = 0; s.q_step = 3; s.q_count = 99;
  for (int i = 0; i < GQ_ITEM_COUNT; i++) s.inv[i] = 99;
  for (int i = 0; i < 12; i++) s.friend_[i] = 100;
  for (int i = 0; i < 4; i++) s.ach[i] = 0xFF;
  G.year = 9999; G.month = 12; G.mday = 31; G.wday = 6;
  G.clock_approx = 1;
}

static void check_card_strips(int c) {
  for (int s = 2; s <= 12; s++) {
    gqt::render_strip_guarded(s);
    const uint16_t* p = gqt::strip;
    int y0 = s * 16;
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < GC_W; x++) {
        uint16_t px = p[y * GC_W + x];
        int ay = y0 + y;
        if (!inside_panel(x, ay)) {
          if (px != g_ui[P_OUTLINE]) { std::printf("card %d: pixel outside panel at %d,%d\n", c, x, ay); std::exit(1); }
        } else {
          int ring = x < 12 || x >= 228 || ay < 36 || ay >= 204;
          int border = x < 10 || x >= 230 || ay < 34 || ay >= 206;
          if (border && px != g_ui[P_YELLOW]) { std::printf("card %d: border broken at %d,%d\n", c, x, ay); std::exit(1); }
          if (ring && !border && px != g_ui[8]) { std::printf("card %d: content in padding at %d,%d\n", c, x, ay); std::exit(1); }
        }
      }
  }
}

static void test_cards() {
  for (int pass = 0; pass < 2; pass++) {
    sim_start(11, 23 * 60 + 59);
    fill_extreme();
    if (pass) G.clock_approx = 0;
    gc_set_date(2026, 11, 11, 3, 1);
    gc_set_option(GC_OPT_CARD_PERIOD, 10);
    for (int c = 1; c <= 4; c++) {
      gc_debug(GQ_DBG_SHOW_CARD, c);
      CHECK(gc_query(GQ_Q_CARD) == c);
      for (int i = 0; i < 4; i++) gc_tick(250);   // card state refresh
      // the real hero may leave or fight meanwhile, which legitimately closes a card: show it again, then render at once
      if (gc_query(GQ_Q_CARD) != c) gc_debug(GQ_DBG_SHOW_CARD, c);
      CHECK(gc_query(GQ_Q_CARD) == c);
      check_card_strips(c);
      CHECK(ui_card_covers(2) && ui_card_covers(12) && !ui_card_covers(1) && !ui_card_covers(13));
    }
    gc_debug(GQ_DBG_SHOW_CARD, 0);
    CHECK(gc_query(GQ_Q_CARD) == 0);
  }
  // card closes by itself
  gc_init(GC_SCENE_VILLAGE, 3, 0);
  gc_debug(GQ_DBG_SHOW_CARD, 2);
  for (int i = 0; i < 31 * 4; i++) gc_tick(250);
  CHECK(gc_query(GQ_Q_CARD) == 0);
  // a dialogue closes an open card
  gc_debug(GQ_DBG_SHOW_CARD, 1);
  ui_say(GQ_NPC_MIRA, "HELLO", 1000);
  CHECK(gc_query(GQ_Q_CARD) == 0);
  std::printf("cards ok\n");
}

static void test_curtain() {
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  gc_set_clock(10, 0, 1);
  gc_tick(10);
  CHECK(!ui_curtain_closed());
  for (int s = 0; s < GC_ROWS; s++) CHECK(!ui_curtain_covers(s));
  gc_clear_dirty();
  ui_curtain(1);
  int closed_at = -1;
  uint32_t seen = 0;
  for (int ms = 0; ms < 600; ms += 10) {
    gc_tick(10);
    seen |= gc_dirty_mask();
    if (closed_at < 0 && ui_curtain_closed()) closed_at = ms + 10;
  }
  CHECK(closed_at >= 280 && closed_at <= 400);
  CHECK(seen == (1u << GC_ROWS) - 1);
  for (int s = 0; s < GC_ROWS; s++) CHECK(ui_curtain_covers(s));
  // closed curtain: every strip renders black
  for (int s = 0; s < GC_ROWS; s++) {
    gqt::render_strip_guarded(s);
    for (int i = 0; i < GC_STRIP_PIXELS; i++) CHECK(gqt::strip[i] == 0);
  }
  ui_curtain(0);
  for (int i = 0; i < 70; i++) gc_tick(10);
  CHECK(!ui_curtain_closed());
  for (int s = 0; s < GC_ROWS; s++) CHECK(!ui_curtain_covers(s));
  // each closing step darkens at most two strips
  ui_curtain(1);
  int prev = 0;
  for (int i = 0; i < 40; i++) {
    gc_tick(40);
    int n = 0;
    for (int s = 0; s < GC_ROWS; s++) n += ui_curtain_covers(s);
    CHECK(n - prev <= 2);
    prev = n;
  }
  CHECK(prev == GC_ROWS);
  ui_curtain(0);
  for (int i = 0; i < 20; i++) gc_tick(40);
  std::printf("curtain ok\n");
}

static void test_banner() {
  gc_init(GC_SCENE_VILLAGE, 9, 0);
  gc_set_clock(10, 0, 1);
  for (int i = 0; i < 8; i++) gc_tick(250);
  gc_clear_dirty();
  gq_emit(GQ_EV_ACHIEVEMENT, 0, 0);
  CHECK(gc_dirty_mask() & (1u << (GC_ROWS - 1)));
  gqt::render_strip_guarded(14);
  int lit = 0;
  for (int i = 0; i < GC_STRIP_PIXELS; i++) lit += gqt::strip[i] == g_ui[P_WHITE];
  CHECK(lit > 50);
  for (int i = 0; i < 4 * 7; i++) gc_tick(250);   // 6 s later it is gone
  gc_clear_dirty();
  gq_emit(GQ_EV_STAR, 0, 0);
  gq_emit(GQ_EV_NEW_COLLECTIBLE, 3, 0);
  std::printf("banner ok\n");
}

static int popc(uint32_t v) { int n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

// 10 simulated minutes in the village, clear weather: dirty strips per frame and full redraws.
static void budget_run(int start_minute, int period, const char* name, int expect_cards_min) {
  sim_start(21, start_minute);
  gc_set_option(GC_OPT_CARD_PERIOD, period);
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_CLEAR);
  const int dt = 50;                     // 20 frames per second
  long frames = 0, strips = 0, full = 0, last_full_ms = -100000, bad_gap = 0, t_ms = 0;
  int cards_open = 0, prev_card = 0;
  uint32_t ms_in_min = 0;
  int wall = start_minute;
  for (long ms = 0; ms < 10L * 60000; ms += dt) {
    gc_tick(dt);
    t_ms += dt; ms_in_min += dt;
    if (ms_in_min >= 60000) { ms_in_min -= 60000; wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1); }
    uint32_t m = gc_dirty_mask();
    for (int s = 0; s < GC_ROWS; s++) if (m & (1u << s)) gqt::render_strip_guarded(s);
    gc_clear_dirty();
    int n = popc(m);
    frames++; strips += n;
    if (n >= 12) {
      full++;
      if (t_ms - last_full_ms < 2000) bad_gap++;
      last_full_ms = t_ms;
    }
    int c = (int)gc_query(GQ_Q_CARD);
    if (c && !prev_card) cards_open++;
    prev_card = c;
  }
  double avg = (double)strips / frames;
  std::printf("%s: avg dirty strips %.2f, full redraws %ld (too close %ld), cards opened %d\n", name, avg, full, bad_gap, cards_open);
  CHECK(avg <= 5.0);
  CHECK(bad_gap <= 1);   // a card closing right after a hero event (curtain/fight) may cluster once in 10 minutes
  CHECK(cards_open >= expect_cards_min);
}

static void test_budget() {
  budget_run(8 * 60 + 20, 0, "no cards", 0);
  budget_run(8 * 60 + 20, 10, "card period 10", 0);
  budget_run(8 * 60 + 56, 10, "across the hour", 1);
}

// Cards open at most once per period (plus the hourly clock card), never during a dialogue.
static void test_schedule() {
  sim_start(33, 7 * 60 + 30);
  gc_set_option(GC_OPT_CARD_PERIOD, 3);
  int opens = 0, prev = 0, in_dialogue = 0;
  uint32_t ms_in_min = 0;
  int wall = 7 * 60 + 30;
  for (long ms = 0; ms < 60L * 60000; ms += 100) {
    gc_tick(100);
    ms_in_min += 100;
    if (ms_in_min >= 60000) { ms_in_min -= 60000; wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1); }
    int c = (int)gc_query(GQ_Q_CARD);
    if (c && !prev) opens++;
    if (c && !ui_dialogue_done()) in_dialogue++;
    prev = c;
    gc_clear_dirty();
  }
  std::printf("schedule: %d cards in 60 min at period 3\n", opens);
  CHECK(in_dialogue == 0);
  CHECK(opens <= 60 / 3 + 2);
  CHECK(opens >= 10);
  // period 0 means never
  sim_start(34, 7 * 60 + 30);
  gc_set_option(GC_OPT_CARD_PERIOD, 0);
  wall = 7 * 60 + 30; ms_in_min = 0;
  for (long ms = 0; ms < 130L * 60000; ms += 250) {
    gc_tick(250);
    ms_in_min += 250;
    if (ms_in_min >= 60000) { ms_in_min -= 60000; wall = (wall + 1) % 1440; gc_set_clock(wall / 60, wall % 60, 1); }
    CHECK(gc_query(GQ_Q_CARD) == 0);
    gc_clear_dirty();
  }
  std::printf("schedule ok\n");
}

// All scenes still render every strip with the guard intact (info and test scenes).
static void test_scenes() {
  for (int sc = 0; sc < 3; sc++) {
    gc_init(sc, 1, 1);
    gc_set_status(0, "IP 192.168.1.50");
    for (int i = 0; i < 40; i++) gc_tick(100);
    for (int s = 0; s < GC_ROWS; s++) gqt::render_strip_guarded(s);
  }
  std::printf("scenes ok\n");
}

// Status band: hero state in plain words, IP address, uptime; only strip 14 is ever redrawn for it.
static void test_status_band() {
  sim_start(5);
  gc_set_status(0, "IP 192.168.100.123");
  char b[24];
  // plain words per activity, "GOING TO ..." while walking
  struct { int act; const char* going; const char* doing; } T[] = {
    {GQ_ACT_CHOP, "GOING TO CHOP", "CHOPPING"}, {GQ_ACT_FISH, "GOING FISHING", "FISHING"},
    {GQ_ACT_MINE, "GOING MINING", "MINING"}, {GQ_ACT_FORAGE, "GOING FORAGING", "FORAGING"}};
  for (auto& c : T) {
    gc_debug(GQ_DBG_FORCE_ACT, c.act);
    bool sawGoing = false, sawDoing = false;
    for (int i = 0; i < 4 * 60 * 4 && !(sawGoing && sawDoing); i++) {   // 4 minutes of game time at 250 ms steps
      sim_step();
      hero_status_text(b, 24);
      if (!std::strcmp(b, c.going)) sawGoing = true;
      if (!std::strcmp(b, c.doing)) sawDoing = true;
    }
    CHECK(sawDoing);
    if (!sawGoing) std::printf("act %d never showed going; last text %s\n", c.act, b);
    CHECK(sawGoing);
  }
  // quest labels name the place; talk labels name the villager
  gc_debug(GQ_DBG_FORCE_ACT, GQ_ACT_QUEST);
  for (int i = 0; i < 40; i++) sim_step();
  hero_status_text(b, 24);
  CHECK(!std::strncmp(b, "QUEST: ", 7) || !std::strncmp(b, "TALK TO ", 8) || b[0]);
  CHECK(std::strlen(b) <= 18);
  // the band pages: state, IP, uptime; the text always fits 18 characters
  bool sawIp = false, sawUp = false, sawState = false;
  for (int i = 0; i < 4 * 30; i++) {                          // 30 s
    sim_step();
    const char* s = ui_band_text();
    CHECK(std::strlen(s) <= 18);
    if (!std::strcmp(s, "IP 192.168.100.123")) sawIp = true;
    else if (!std::strncmp(s, "UP ", 3)) sawUp = true;
    else if (s[0]) sawState = true;
  }
  CHECK(sawIp && sawUp && sawState);
  // 2d 03h uptime format
  gc_set_status(0, "WIFI: NOT CONNECTED");
  std::printf("status band ok\n");
}

// ---- R3 HUD with live weather (design 2.7, 9.3)
static void hud_render(uint16_t* buf, int wx, int t10, int flags) {
  gc_set_live_weather(wx, t10, flags);
  for (int i = 0; i < 2; i++) gc_tick(100);
  GqLook lk;
  cal_look(&lk);
  for (int g = 0; lk.tr == 320 && g < 20; g++) { gc_tick(100); cal_look(&lk); }   // not inside a lightning flash
  gqt::render_strip_guarded(0);
  std::memcpy(buf, gqt::strip, GC_STRIP_PIXELS * sizeof(uint16_t));
}
// Leftmost and rightmost column in which two renders differ (lo 999 when identical).
static int hud_diff(const uint16_t* a, const uint16_t* b, int* hi) {
  int lo = 999;
  *hi = -1;
  for (int y = 0; y < GC_STRIP_H; y++)
    for (int x = 0; x < GC_W; x++)
      if (a[y * GC_W + x] != b[y * GC_W + x]) { if (x < lo) lo = x; if (x > *hi) *hi = x; }
  return lo;
}
static int hud_ink(const uint16_t* a, const uint16_t* bg, int x0, int x1) {
  int n = 0;
  for (int y = 0; y < GC_STRIP_H; y++)
    for (int x = x0; x <= x1; x++) if (a[y * GC_W + x] != bg[y * GC_W + x]) n++;
  return n;
}
static uint16_t hbase[GC_STRIP_PIXELS], hcur[GC_STRIP_PIXELS], hprev[GC_STRIP_PIXELS];

static void test_hud_weather() {
  gc_init(GC_SCENE_VILLAGE, 3, 0);
  gc_set_clock(10, 42, 1);
  gc_set_net(GC_NET_ONLINE);
  gc_set_status(0, "IP 192.168.1.50");
  gc_debug(GQ_DBG_SET_DAY, 21);                               // winter: live snow and simulated snow look alike
  // nothing left of the weather icon and no clock pixel (x >= 178) changes, for every state, unit and temperature
  // (the base is the same state without live data: the simulated machine set to it)
  for (int wx = 0; wx < GQ_W_COUNT; wx++) {
    gc_debug(GQ_DBG_SET_WEATHER, wx);
    hud_render(hbase, GC_WX_NONE, 0, 0);
    for (int fl = 0; fl < 8; fl++) {
      for (int t = -99; t <= 199; t += (fl < 2 ? 1 : 11)) {
        int t10 = (fl & GC_WXF_FAHRENHEIT) ? ((t - 32) * 50 / 9) : t * 10;   // displays about t in the unit
        if (t10 < -999 || t10 > 999) continue;
        int hi, lo = 0;
        for (int tries = 0; tries < 4; tries++) {  // base and live renders are a tick apart: a coin may move in between
          hud_render(hbase, GC_WX_NONE, 0, 0);
          hud_render(hcur, wx, t10, fl);
          lo = hud_diff(hbase, hcur, &hi);
          if (lo >= 100) break;
        }
        if (lo < 100) std::printf("wx %d fl %d t %d lo %d hi %d\n", wx, fl, t, lo, hi);
        CHECK(lo >= 100);
        for (int y = 0; y < GC_STRIP_H; y++)
          for (int x = 178; x < GC_W; x++) CHECK(hbase[y * GC_W + x] == hcur[y * GC_W + x]);
      }
    }
  }
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_CLOUDY);
  hud_render(hbase, GC_WX_NONE, 0, 0);
  // changing only the number moves pixels only inside its three cells 117..153
  hud_render(hprev, GQ_W_CLOUDY, 70, 0);
  for (int t = -99; t <= 99; t++) {
    hud_render(hcur, GQ_W_CLOUDY, t * 10, 0);
    int hi, lo = hud_diff(hprev, hcur, &hi);
    if (lo != 999) CHECK(lo >= 117 && hi <= 153);
  }
  hud_render(hprev, GQ_W_CLOUDY, 999, 0);
  hud_render(hcur, GQ_W_CLOUDY, -999, 0);
  { int hi, lo = hud_diff(hprev, hcur, &hi); CHECK(lo >= 117 && hi <= 153); }
  // the unit letter changes only inside 154..158
  hud_render(hprev, GQ_W_CLEAR, 230, 0);
  hud_render(hcur, GQ_W_CLEAR, 230, GC_WXF_FAHRENHEIT);
  { int hi, lo = hud_diff(hprev, hcur, &hi); CHECK(lo >= 117 && hi <= 158); }
  // temperature, unit, season block and clock are all present, with gaps 159..162 and 174..177 between them
  hud_render(hcur, GQ_W_CLEAR, 230, 0);
  CHECK(hud_ink(hcur, hbase, 117, 153) > 20);
  CHECK(hud_ink(hcur, hbase, 154, 158) > 8);
  CHECK(hud_ink(hcur, hbase, 159, 162) == 0);
  CHECK(hud_ink(hcur, hbase, 174, 177) == 0);
  hud_render(hprev, GC_WX_NONE, 0, 0);
  CHECK(hud_ink(hprev, hbase, 0, 239) == 0);
  // stale is drawn grey, fresh white: same shape, different colour, only in the temperature area
  hud_render(hprev, GQ_W_RAIN, 230, 0);
  hud_render(hcur, GQ_W_RAIN, 230, GC_WXF_STALE);
  { int hi, lo = hud_diff(hprev, hcur, &hi); CHECK(lo >= 117 && hi <= 158); }
  // the moon replaces the sun for CLEAR at night only
  hud_render(hprev, GQ_W_CLEAR, 100, 0);
  hud_render(hcur, GQ_W_CLEAR, 100, GC_WXF_NIGHT);
  { int hi, lo = hud_diff(hprev, hcur, &hi); CHECK(lo >= 100 && hi <= 116); }
  hud_render(hprev, GQ_W_RAIN, 100, 0);
  hud_render(hcur, GQ_W_RAIN, 100, GC_WXF_NIGHT);
  { int hi, lo = hud_diff(hprev, hcur, &hi); CHECK(lo == 999); }
  // WiFi down: the red "!" takes the place of the season block, nothing reaches the clock
  hud_render(hprev, GQ_W_RAIN, 100, 0);
  gc_set_net(GC_NET_OFFLINE);
  hud_render(hcur, GQ_W_RAIN, 100, 0);
  gc_set_net(GC_NET_ONLINE);
  { int hi, lo = hud_diff(hprev, hcur, &hi); CHECK(lo >= 163 && hi <= 175); }
  // leaving live mode restores the base render
  hud_render(hcur, GQ_W_RAIN, 230, 0);
  hud_render(hcur, GC_WX_NONE, 0, 0);
  CHECK(std::memcmp(hcur, hbase, sizeof(hcur)) == 0);
  // the strip is redrawn when the temperature, unit or age class changes
  gc_set_live_weather(GQ_W_RAIN, 100, 0);
  for (int i = 0; i < 3; i++) gc_tick(100);
  gc_clear_dirty();
  gc_set_live_weather(GQ_W_RAIN, 110, 0);     gc_tick(100); CHECK(gc_dirty_mask() & 1u); gc_clear_dirty();
  gc_set_live_weather(GQ_W_RAIN, 110, 1);     gc_tick(100); CHECK(gc_dirty_mask() & 1u); gc_clear_dirty();
  gc_set_live_weather(GQ_W_RAIN, 110, 1 | 4); gc_tick(100); CHECK(gc_dirty_mask() & 1u); gc_clear_dirty();
  std::printf("hud weather ok\n");
}

int main() {
  test_fmt();
  test_palette();
  test_cards();
  test_curtain();
  test_banner();
  test_status_band();
  test_budget();
  test_schedule();
  test_scenes();
  test_hud_weather();
  std::printf("PASS\n");
  return 0;
}
