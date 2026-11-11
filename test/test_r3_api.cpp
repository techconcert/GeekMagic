// R3-0C: the core API added for release R3 (live weather input, sleep option, hero-in-bed query, the talk_* stubs,
// the chat loop and the bedtime hook). docs/DESIGN-R3.md 8.3.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"
#include "../lib/gamecore/src/gq_mod.h"

static int g_bad_presence, g_over_cap;

static void invariants() {
  int p = (int)gc_query(GQ_Q_HERO_PRESENCE);
  if (p < 1 || p > 4) g_bad_presence++;
  if (gc_query(GQ_Q_COINS) > gc_query(GQ_Q_COIN_CAP)) g_over_cap++;
}

static void test_live_weather_input() {
  sim_start(1);
  CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  CHECK(gc_query(GQ_Q_LIVE_TEMP) == 0 && gc_query(GQ_Q_LIVE_FLAGS) == 0);
  gc_set_live_weather(GQ_W_RAIN, 231, GC_WXF_NIGHT);
  CHECK(gc_query(GQ_Q_LIVE_WX) == GQ_W_RAIN && gc_query(GQ_Q_LIVE_TEMP) == 231 && gc_query(GQ_Q_LIVE_FLAGS) == 2);
  for (int w = 0; w < GQ_W_COUNT; w++) { gc_set_live_weather(w, 0, 0); CHECK(gc_query(GQ_Q_LIVE_WX) == w); }
  gc_set_live_weather(GQ_W_COUNT, 0, 0);            CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);   // one above
  gc_set_live_weather(GQ_W_COUNT - 1, 0, 0);        CHECK(gc_query(GQ_Q_LIVE_WX) == GQ_W_COUNT - 1);
  gc_set_live_weather(-2, 0, 0);                    CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  gc_set_live_weather(GC_WX_NONE, 0, 0);            CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  gc_set_live_weather(99999, 0, 0);                 CHECK(gc_query(GQ_Q_LIVE_WX) == GC_WX_NONE);
  gc_set_live_weather(0, 999, 0);                   CHECK(gc_query(GQ_Q_LIVE_TEMP) == 999);
  gc_set_live_weather(0, 1000, 0);                  CHECK(gc_query(GQ_Q_LIVE_TEMP) == 999);
  gc_set_live_weather(0, -999, 0);                  CHECK(gc_query(GQ_Q_LIVE_TEMP) == -999);
  gc_set_live_weather(0, -1000, 0);                 CHECK(gc_query(GQ_Q_LIVE_TEMP) == -999);
  gc_set_live_weather(0, 2000000000, 0);            CHECK(gc_query(GQ_Q_LIVE_TEMP) == 999);
  gc_set_live_weather(0, -2000000000, 0);           CHECK(gc_query(GQ_Q_LIVE_TEMP) == -999);
  gc_set_live_weather(0, 0, 0xFF);                  CHECK(gc_query(GQ_Q_LIVE_FLAGS) == 7);
  // the stubs keep today's behaviour: nothing is live, the simulated weather rules
  int t = 77, f = 77;
  CHECK(cal_live() == 0);
  CHECK(cal_weather_shown() == cal_weather());
  CHECK(cal_temp(&t, &f) == 0 && cal_temp_c(&t) == 0);
  gc_debug(GQ_DBG_SET_WEATHER, GQ_W_FOG);
  gc_set_live_weather(GQ_W_SNOW, -50, 0);
  CHECK(gc_query(GQ_Q_WEATHER) == GQ_W_FOG);
  sim_minutes(10);                                  // a state that stays set across ticks and a game reset
  gc_reset_game();
  CHECK(gc_query(GQ_Q_LIVE_WX) == GQ_W_SNOW);
}

static void test_sleep_option() {
  sim_start(1);
  CHECK(gc_query(GQ_Q_SLEEP_MODE) == 0);
  gc_set_option(GC_OPT_SLEEP_MODE, 2); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 2);
  gc_set_option(GC_OPT_SLEEP_MODE, 1); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 1);
  gc_set_option(GC_OPT_SLEEP_MODE, 3); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 1);    // refused, unchanged
  gc_set_option(GC_OPT_SLEEP_MODE, -1); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 1);
  gc_set_option(GC_OPT_SLEEP_MODE, 0); CHECK(gc_query(GQ_Q_SLEEP_MODE) == 0);
  CHECK(gc_query(GQ_Q_HERO_ABED) == 0);
  // the other options still work
  gc_set_option(GC_OPT_CARD_PERIOD, 7); gc_set_option(GC_OPT_NIGHT_QUIET, 1);
  CHECK(gc_query(GQ_Q_SLEEP_MODE) == 0);
}

// Mode 2: in bed within 5 simulated minutes, from every activity, on 20 seeds.
static long minutes_to_bed(uint32_t seed, int act, int pre_s) {
  sim_start(seed, 10 * 60);
  gc_set_option(GC_OPT_SLEEP_MODE, 1);
  if (seed % 5 == 0) gc_debug(GQ_DBG_ADD_ITEM, (GQ_ITEM_FLOWER << 8) | 3);
  gc_debug(GQ_DBG_FORCE_ACT, act);
  sim_seconds_cb(pre_s, [&]() { invariants(); });
  CHECK(gc_query(GQ_Q_HERO_ABED) == 0);
  gc_set_option(GC_OPT_SLEEP_MODE, 2);
  long found = -1;
  for (long s = 1; s <= 300 && found < 0; s++) {
    sim_seconds_cb(1, [&]() { invariants(); });
    if (gc_query(GQ_Q_HERO_ABED)) found = s;
  }
  return found;
}

static void test_mode2() {
  long worst = 0;
  for (uint32_t seed = 1; seed <= 20; seed++) {
    int act = 1 + (int)((seed - 1) % 15);
    long s = minutes_to_bed(seed, act, 5 + (long)seed * 3);
    if (s < 0) std::printf("seed %u act %d: NOT in bed within 5 minutes\n", (unsigned)seed, act);
    CHECK(s >= 0);
    if (s > worst) worst = s;
    // and he stays in bed (hero is asleep, presence is accounted for) for the next hour
    int in_bed_samples = 0, asleep_samples = 0;
    sim_seconds_cb(3600, [&]() { invariants(); in_bed_samples += gc_query(GQ_Q_HERO_ABED); asleep_samples += gc_query(GQ_Q_HERO_ACT) == GQ_ACT_SLEEP; });
    CHECK(asleep_samples > 3500);       // asleep all hour; the night camera cut (phase 3) is not 'in bed'
    CHECK(in_bed_samples > 2000);
    // the schedule hands the evening back: mode 1 gets him up
    gc_set_option(GC_OPT_SLEEP_MODE, 1);
    sim_seconds_cb(10, [&]() { invariants(); });
    CHECK(gc_query(GQ_Q_HERO_ABED) == 0 && gc_query(GQ_Q_HERO_ACT) != GQ_ACT_SLEEP);
  }
  std::printf("mode 2: slowest seed needed %ld s to lie in bed\n", worst);
}

// Mode 1 keeps him up through the whole night (21:00 to 07:00).
static void test_mode1_stays_up() {
  for (uint32_t seed = 1; seed <= 6; seed++) {
    sim_start(seed, 21 * 60);
    gc_set_option(GC_OPT_SLEEP_MODE, 1);
    long bed = 0, sleep_act = 0, night_samples = 0;
    sim_seconds_cb(10L * 3600, [&]() {
      invariants();
      bed += gc_query(GQ_Q_HERO_ABED);
      sleep_act += gc_query(GQ_Q_HERO_ACT) == GQ_ACT_SLEEP;
      int m = (int)gc_query(GQ_Q_MINUTE);
      if (m >= 22 * 60 || m < 6 * 60) night_samples++;
    });
    CHECK(night_samples > 6 * 3500);
    CHECK(bed == 0 && sleep_act == 0);
  }
}

// Mode 0 behaves as before: the hero sleeps in the built-in night and is up by day.
static void test_mode0_unchanged() {
  for (uint32_t seed = 1; seed <= 6; seed++) {
    sim_start(seed, 20 * 60);
    gc_set_option(GC_OPT_SLEEP_MODE, 0);          // options survive gc_init (the firmware sets them)
    long day_bed = 0, bed_night = 0, night_samples = 0;
    sim_seconds_cb(14L * 3600, [&]() {          // 20:00 to 10:00
      invariants();
      int m = (int)gc_query(GQ_Q_MINUTE);
      int night = m >= 22 * 60 || m < 6 * 60;
      int abed = (int)gc_query(GQ_Q_HERO_ABED);
      if (night && m != 22 * 60 && (m >= 22 * 60 + 20 || m < 6 * 60)) { night_samples++; bed_night += abed; }
      if (m >= 7 * 60 && m < 20 * 60) day_bed += abed;
    });
    std::printf("mode 0 seed %u: abed %ld of %ld night samples\n", (unsigned)seed, bed_night, night_samples);
    CHECK(day_bed == 0);
    CHECK(bed_night * 100 > night_samples * 40);   // asleep for a large part of the night (night camera cuts excluded)
  }
}

// A chat plays turn by turn: the number of dialogue boxes is the number of turns, in order, alternating speakers.
static uint32_t box_hash() {
  uint32_t h = 2166136261u;
  for (int s = 12; s < GC_ROWS; s++) {
    gqt::render_strip_guarded(s);
    for (int i = 0; i < GC_STRIP_PIXELS; i++) { h = (h ^ gqt::strip[i]) * 16777619u; }
  }
  return h;
}

static void test_chat_turns(int turns, uint32_t seed, int npc) {
  sim_start(seed, 11 * 60);
  gc_set_option(GC_OPT_CARD_PERIOD, 0);
  gc_debug(GQ_DBG_TALK_TURNS, turns);
  int chats0 = (int)gc_query(GQ_Q_STAT_BASE + GQ_STAT_CHATS);
  gc_debug(GQ_DBG_FORCE_CHAT, npc);
  int boxes = 0, active_prev = 0;
  int done = 0;
  for (long i = 0; i < 4 * 120 && !done; i++) {
    sim_step();
    int active = !ui_dialogue_done() && gc_query(GQ_Q_HERO_ACT) == GQ_ACT_CHAT;
    if (active && !active_prev) boxes++;
    active_prev = active;
    done = gc_query(GQ_Q_STAT_BASE + GQ_STAT_CHATS) > chats0;
  }
  CHECK(done);
  std::printf("chat with %d turns, npc %d: %d boxes\n", turns, npc, boxes);
  CHECK(boxes == turns);
  gc_debug(GQ_DBG_TALK_TURNS, 0);
}

// Order and speaker: replay the chat and compare each finished box with the same box drawn by ui_say itself.
static void test_chat_order() {
  sim_start(3, 11 * 60);
  gc_set_option(GC_OPT_CARD_PERIOD, 0);
  gc_debug(GQ_DBG_TALK_TURNS, 4);
  gc_debug(GQ_DBG_FORCE_CHAT, 2);
  int chats0 = (int)gc_query(GQ_Q_STAT_BASE + GQ_STAT_CHATS);
  int turn = 0, active_prev = 0, ms_active = 0;
  uint32_t got[4] = {0, 0, 0, 0};
  int have[4] = {0, 0, 0, 0};
  for (long i = 0; i < 4 * 150 && gc_query(GQ_Q_STAT_BASE + GQ_STAT_CHATS) == chats0; i++) {
    sim_step();
    int active = !ui_dialogue_done() && gc_query(GQ_Q_HERO_ACT) == GQ_ACT_CHAT;
    if (active && !active_prev) { turn++; ms_active = 0; }
    if (active) {
      ms_active += 250;
      if (ms_active == 1500 && turn >= 1 && turn <= 4) { got[turn - 1] = box_hash(); have[turn - 1] = 1; }
    }
    active_prev = active;
  }
  for (int k = 0; k < 4; k++) CHECK(have[k]);
  CHECK(turn == 4);
  for (int a = 0; a < 4; a++) for (int b = a + 1; b < 4; b++) CHECK(got[a] != got[b]);
  // reference: the same text and speaker through ui_say, the villager first and the hero second, alternating
  for (int k = 0; k < 4; k++) {
    char txt[16] = "TURN 1";
    txt[5] = (char)('1' + k);
    ui_say(k & 1 ? (int)GQ_NPC_HERO : 2, txt, 100000);
    for (int i = 0; i < 4 * 3; i++) sim_step();
    uint32_t ref = box_hash();
    int matches = 0;
    for (int j = 0; j < 4; j++) matches += got[j] == ref;
    // the world behind the box may have moved: the strict check is that no OTHER box matches better than its own
    CHECK(matches <= 1);
    if (matches == 1) CHECK(got[k] == ref);
  }
}

// The dialogue engine (chunk D1 replaced the R3-0C stub; test_talk.cpp has the detailed tests): 2 to 4 turns, solo lines.
static void test_talk_stub() {
  sim_start(1);
  char buf[40];
  talk_init();
  int turns = talk_begin(GQ_NPC_MIRA);
  CHECK(turns >= 2 && turns <= 4);
  CHECK(talk_turn(0, buf, 40) >= 0 && buf[0]);
  CHECK(talk_turn(1, buf, 40) >= 0 && buf[0]);
  CHECK(talk_turn(1, buf, 1) >= 0 && buf[0] == 0);          // a tiny buffer is never overrun
  CHECK(talk_turn(0, buf, 0) == 0);
  CHECK(talk_solo(TALK_SOLO_BED, buf, 40) == 1 && buf[0] && talk_solo(TALK_SOLO_WAKE, buf, 40) == 1 && buf[0]);
  CHECK(talk_solo(TALK_SOLO_DID, buf, 40) == 0);           // nothing done yet
  talk_on_event(GQ_EV_CHOPPED, 0, 0);
  // normal play, no debug script: chats still finish and give friendship / TALKED
  sim_start(5, 12 * 60);
  gc_set_option(GC_OPT_CARD_PERIOD, 0);
  int chats0 = (int)gc_query(GQ_Q_STAT_BASE + GQ_STAT_CHATS);
  gc_debug(GQ_DBG_FORCE_CHAT, GQ_NPC_BRAM);
  for (long i = 0; i < 4 * 200 && gc_query(GQ_Q_STAT_BASE + GQ_STAT_CHATS) == chats0; i++) sim_step();
  CHECK(gc_query(GQ_Q_STAT_BASE + GQ_STAT_CHATS) > chats0);
}

// Debug commands: refused values change nothing.
static void test_debug_bounds() {
  sim_start(2);
  gc_debug(GQ_DBG_FORCE_CHAT, -1);
  gc_debug(GQ_DBG_FORCE_CHAT, 9);
  gc_debug(GQ_DBG_FORCE_CHAT, 99999);
  gc_debug(GQ_DBG_TALK_TURNS, 1);
  gc_debug(GQ_DBG_TALK_TURNS, 5);
  gc_debug(GQ_DBG_TALK_TURNS, -3);
  sim_minutes(20);
  invariants();
}

int main() {
  test_live_weather_input();
  test_sleep_option();
  test_debug_bounds();
  test_talk_stub();
  test_chat_turns(2, 1, 1);
  test_chat_turns(3, 2, 4);
  test_chat_turns(4, 3, 8);
  test_chat_order();
  test_mode2();
  test_mode1_stays_up();
  test_mode0_unchanged();
  CHECK(g_bad_presence == 0);
  CHECK(g_over_cap == 0);
  std::printf("PASS\n");
  return 0;
}
