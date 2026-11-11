// Chunk 5 tests (design 11.7, 13.1): table integrity, text rules against the real font, reachability,
// scripted playthroughs of every quest, story pacing, random event storms, save round trip, and a
// story-completion soak over many simulated days (module level for hundreds of days, whole game for
// a month and more).   Usage: test_quest [whole_game_days]
#include "gq_test.h"
#include "gq_sanitize.h"
#include "../lib/gamecore/src/gq_mod.h"
#include "../lib/gamecore/src/assets_gen.h"
#include "../lib/gamecore/src/text_data.h"
#include "../lib/gamecore/src/quest_data.h"
#include "../lib/gamecore/src/talk_data.h"   // enums only
int quest_chat_state(int npc);   // quest.cpp, for talk.cpp
int quest_chat_kind(int* npc);

void quest_debug_start(int q);   // quest.cpp (for gc_debug START_QUEST, wired by the integration pass)

// ------------------------------------------------------------------------------------ helpers
static int g_fail = 0;
#define EXPECT(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); if (++g_fail > 20) std::exit(1); } } while (0)

[[maybe_unused]] static const char* txt(int id, char* buf, int cap) { return gq_text(id, buf, cap); }

// A glyph that is drawn: inside the font range, and not the "unknown" glyph (which is a copy of '?')
static int glyph_ok(char ch) {
  if (ch == ' ') return 1;
  if (ch < GC_FONT_FIRST || ch > GC_FONT_LAST) return 0;
  const uint8_t* g = GC_FONT[ch - GC_FONT_FIRST];
  const uint8_t* q = GC_FONT['?' - GC_FONT_FIRST];
  int blank = 1, same = 1;
  for (int i = 0; i < 7; i++) { if (g[i]) blank = 0; if (g[i] != q[i]) same = 0; }
  return !blank && (ch == '?' || !same);
}

// 2 lines of at most 19 characters, `|` separates them; `maxlines`/`maxlen` tighten for bands and titles
static void check_text(int id, int maxlines, int maxlen, const char* what) {
  char b[128];
  gq_text(id, b, 128);
  int len = 0, lines = 1, linelen = 0;
  EXPECT(b[0] != 0, "%s id %d empty", what, id);
  for (; b[len]; len++) {
    if (b[len] == '|') {
      EXPECT(linelen > 0, "%s id %d empty line: %s", what, id, b);
      lines++; linelen = 0;
      continue;
    }
    EXPECT(glyph_ok(b[len]), "%s id %d char '%c' (%d) has no glyph: %s", what, id, b[len], b[len], b);
    linelen++;
    EXPECT(linelen <= maxlen, "%s id %d line too long (%d): %s", what, id, linelen, b);
  }
  EXPECT(linelen > 0, "%s id %d ends with a break: %s", what, id, b);
  EXPECT(lines <= maxlines, "%s id %d has %d lines: %s", what, id, lines, b);
  EXPECT(len <= 39, "%s id %d longer than the 40-byte dialogue copy: %s", what, id, b);
}

static int unlocked(int area) {
  switch (area) {
    case GQ_AREA_SHORE: return (G.s.projects >> QP_BRIDGE) & 1;
    case GQ_AREA_CAVE: return G.s.tool[GQ_TOOL_PICK] >= 1;
    case GQ_AREA_RUINS: return G.s.tool[GQ_TOOL_LANTERN] >= 1;
    default: return area >= 0 && area < GQ_AREA_COUNT;
  }
}

static int ev_for_act(int act) {
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
    default: return GQ_EV_HOUR;
  }
}

// The story goals of design 3.5 as the scripted "everything is buyable" player gets them (day = earliest day)
struct Goal { int day, kind, id, level; };   // kind 0 tool, 1 project
static const Goal GOALS[] = {
  {1, 0, GQ_TOOL_PURSE, 1}, {2, 1, QP_BOARD, 0}, {3, 0, GQ_TOOL_AXE, 1}, {4, 1, QP_GARDEN, 0}, {5, 0, GQ_TOOL_ROD, 1},
  {7, 1, QP_BRIDGE, 0}, {9, 0, GQ_TOOL_PICK, 1}, {12, 1, QP_WELL, 0}, {14, 0, GQ_TOOL_LANTERN, 1},
  {16, 0, GQ_TOOL_BOOTS, 1}, {18, 1, QP_STALL, 0}, {20, 1, QP_LANTERNS, 0}, {21, 0, GQ_TOOL_SWORD, 1},
  {23, 0, GQ_TOOL_PICK, 2}, {24, 1, QP_FLOWERBEDS, 0}, {42, 1, QP_STAGE, 0}, {50, 1, QP_FOUNTAIN, 0},
};

static void apply_goals(int day) {
  for (unsigned i = 0; i < sizeof(GOALS) / sizeof(GOALS[0]); i++) {
    const Goal& g = GOALS[i];
    if (day < g.day) continue;
    if (g.kind == 0) { if (G.s.tool[g.id] < g.level) G.s.tool[g.id] = (uint8_t)g.level; }
    else G.s.projects = (uint16_t)(G.s.projects | (1u << g.id));
  }
  // bell tower repair: day 27, needs the four shards (taken by the purchase)
  if (day >= 27 && !((G.s.projects >> QP_BELL) & 1) && G.s.inv[GQ_ITEM_SHARD] >= 4) {
    G.s.inv[GQ_ITEM_SHARD] = (uint8_t)(G.s.inv[GQ_ITEM_SHARD] - 4);
    G.s.projects = (uint16_t)(G.s.projects | (1u << QP_BELL));
  }
}

static void all_unlocked(void) {
  for (unsigned i = 0; i < sizeof(GOALS) / sizeof(GOALS[0]); i++) {
    const Goal& g = GOALS[i];
    if (g.kind == 0) G.s.tool[g.id] = (uint8_t)g.level; else G.s.projects = (uint16_t)(G.s.projects | (1u << g.id));
  }
  G.s.projects = (uint16_t)(G.s.projects | (1u << QP_BELL));
  for (int n = 0; n < 12; n++) G.s.friend_[n] = 100;
}

static long g_talk_lines = 0;

// The hero's side of one need: returns 1 if something was done.
static int act_on(const GqNeed& n) {
  char line[48];
  switch (n.kind) {
    case GQ_NEED_TALK: {
      EXPECT(n.npc > 0 && n.npc < GQ_NPC_COUNT, "talk npc %d", n.npc);
      if (quest_talk(n.npc, line, 48)) { g_talk_lines++; EXPECT(line[0], "empty talk line"); }
      gq_emit(GQ_EV_TALKED, n.npc, 0);
      return 1;
    }
    case GQ_NEED_ITEM:
      EXPECT(n.item < GQ_ITEM_COUNT && n.count > 0, "item need %d x%d", n.item, n.count);
      econ_add_item(n.item, n.count);
      gq_emit(ev_for_act(n.activity), n.item, 0);
      return 1;
    case GQ_NEED_GOTO:
      if (n.npc != 0xFF) { gq_emit(GQ_EV_SCENE_DONE, n.npc, 0); return 1; }
      gq_emit(GQ_EV_ARRIVED, n.area, n.ty * GC_COLS + n.tx);
      return 1;
    case GQ_NEED_SLAY:
      for (int k = 0; k < n.count && k < 20; k++) gq_emit(GQ_EV_SLAIN, n.item, 0);
      return 1;
    case GQ_NEED_DO:
      for (int k = 0; k < n.count && k < 20; k++) gq_emit(ev_for_act(n.activity), 0xFF, 0);
      return 1;
    default: return 0;
  }
}

static void check_need_invariants(const GqNeed& n) {
  if (n.kind == GQ_NEED_NONE || n.kind == GQ_NEED_WAIT) return;
  EXPECT(n.area < GQ_AREA_COUNT, "need area %d", n.area);
  EXPECT(unlocked(n.area), "need points to locked area %d (kind %d)", n.area, n.kind);
}

static void check_state(void) {
  EXPECT(G.s.q_active == 0xFF || G.s.q_active < QUEST_COUNT, "q_active %d", G.s.q_active);
  if (G.s.q_active != 0xFF) EXPECT(G.s.q_step < quest_steps(G.s.q_active), "q_step %d of quest %d", G.s.q_step, G.s.q_active);
  EXPECT(G.s.q_count <= 99, "q_count %d", G.s.q_count);
  EXPECT(G.s.chapter <= 6, "chapter %d", G.s.chapter);
  EXPECT(G.s.errands_today <= 3, "errands_today %d", G.s.errands_today);
  EXPECT(G.s.coins <= econ_coin_cap(), "coins over cap");
}

// ------------------------------------------------------------------------------ 1. text and tables
static void test_text_rules(void) {
  EXPECT(sizeof(TEXT_BLOB) <= 12288, "blob %d", (int)sizeof(TEXT_BLOB));
  EXPECT(TX_COUNT > 190, "TX_COUNT %d", TX_COUNT);   // names, quest titles, steps, bands, thanks (ambient and reply lines moved to talk_data.h in R3)
  int prev = -1;
  for (int id = 0; id < TX_COUNT; id++) {
    int o = TEXT_OFF[id];
    EXPECT(o > prev && o < (int)sizeof(TEXT_BLOB), "offset of %d", id);
    prev = o;
  }
  for (int id = 0; id < TX_NAME0 + GQ_NPC_COUNT; id++) check_text(id, 1, 8, "name");
  for (int id = TX_THANKS0; id < TX_COUNT; id++) check_text(id, 2, 19, "line");
  for (int q = 0; q < QUEST_COUNT; q++) {
    check_text(QUESTS[q].title_txt, 1, 20, "title");
    for (int k = 0; k < QUESTS[q].n_steps; k++) {
      const QuestStep& s = QSTEPS[QUESTS[q].first_step + k];
      check_text(s.txt_start, 2, 19, "step");
      check_text(s.txt_band, 1, 14, "band");
    }
  }
  // gq_text is safe for any id and any capacity
  char b[8];
  gq_text(-1, b, 8); EXPECT(b[0] == 0, "negative id");
  gq_text(TX_COUNT, b, 8); EXPECT(b[0] == 0, "id past the end");
  gq_text(100000, b, 8); EXPECT(b[0] == 0, "huge id");
  gq_text(TX_THANKS0, b, 1); EXPECT(b[0] == 0, "cap 1");
  gq_text(TX_THANKS0, b, 0);   // must not write
  gq_text(TX_THANKS0 + 3, b, 6); EXPECT(b[5] == 0 || b[6 - 1] == 0, "cap 6 terminated");
  for (int n = 0; n < GQ_NPC_COUNT; n++) EXPECT(gq_npc_name(n)[0] != 0, "name %d", n);
  EXPECT(gq_npc_name(-1)[0] == 0 && gq_npc_name(99)[0] == 0, "bad npc name");
  std::printf("text: %d strings, blob %d of 12288 bytes\n", TX_COUNT, (int)sizeof(TEXT_BLOB));
}

static const uint8_t MON_AREA[5] = {GQ_AREA_FOREST, GQ_AREA_CAVE, GQ_AREA_SHORE, GQ_AREA_RUINS, GQ_AREA_RUINS};

static void test_tables(void) {
  EXPECT(QUEST_COUNT >= 28 && QUEST_COUNT <= 32, "quests %d", QUEST_COUNT);
  EXPECT(QSTEP_COUNT <= 80, "steps %d", QSTEP_COUNT);
  EXPECT(quest_count() == QUEST_COUNT, "quest_count");
  int expect_first = 0, story_steps = 0;
  for (int q = 0; q < QUEST_COUNT; q++) {
    const QuestDef& d = QUESTS[q];
    EXPECT(d.first_step == expect_first, "quest %d first_step %d", q, d.first_step);
    EXPECT(d.n_steps >= 1, "quest %d has no steps", q);
    expect_first += d.n_steps;
    EXPECT(quest_steps(q) == d.n_steps, "quest_steps");
    if (q < 6) story_steps += d.n_steps;
    EXPECT(d.giver > 0 && d.giver < GQ_NPC_COUNT, "giver %d", d.giver);
    EXPECT(d.title_txt < TX_COUNT, "title");
    EXPECT(d.req_kind <= QR_SEASON, "req kind");
    switch (d.req_kind) {
      case QR_DAY: break;
      case QR_QUEST: EXPECT(d.req_val < q, "quest %d requires a later quest", q); break;
      case QR_TOOL: EXPECT((d.req_val >> 4) < GQ_TOOL_COUNT && (d.req_val & 15) >= 1 && (d.req_val & 15) <= 3, "tool req"); break;
      case QR_FRIEND: EXPECT((d.req_val >> 4) < 12 && (d.req_val & 15) >= 1 && (d.req_val & 15) <= 4, "friend req"); break;
      case QR_PROJECT: EXPECT(d.req_val < 10, "project req"); break;
      case QR_SEASON: EXPECT(d.req_val < 4, "season req"); break;
      default: break;
    }
    EXPECT(d.reward_item == 0xFF || (d.reward_item >= GQ_ITEM_MAP && d.reward_item < GQ_ITEM_COUNT), "reward item");
    if (q >= QUEST_ERRAND0 && q < QUEST_LEGEND0) EXPECT(d.reward_coins5 == 0, "errands pay 6..14 at random");
    for (int k = 0; k < d.n_steps; k++) {
      const QuestStep& s = QSTEPS[d.first_step + k];
      EXPECT(s.txt_start < TX_COUNT && s.txt_band < TX_COUNT, "step text ids");
      switch (s.type) {
        case QS_TALK: EXPECT(s.a > 0 && s.a < GQ_NPC_COUNT && (s.b == 0 || (s.b >= 16 && s.b < GQ_ITEM_COUNT)), "talk"); break;
        case QS_FETCH: EXPECT(s.a <= GQ_ITEM_GEM + 4 && s.b >= 1 && s.b <= econ_item_cap(s.a), "fetch item %d x%d", s.a, s.b); break;
        case QS_DELIVER: EXPECT(s.a > 0 && s.a < GQ_NPC_COUNT && s.b < GQ_ITEM_COUNT && s.c >= 1 && s.c <= econ_item_cap(s.b), "deliver"); break;
        case QS_GOTO: EXPECT(s.a < GQ_AREA_COUNT && s.b < GC_COLS && s.c < GC_ROWS, "goto"); break;
        case QS_SLAY: EXPECT(s.a < GQ_MON_COUNT && s.b <= 12 && s.c == MON_AREA[s.a], "slay"); break;
        case QS_DO: EXPECT(s.a > 0 && s.a < GQ_ACT_COUNT && s.b >= 1, "do"); break;
        case QS_WAIT_DAY: case QS_SCENE: break;
        default: EXPECT(0, "step type %d", s.type);
      }
    }
  }
  EXPECT(expect_first == QSTEP_COUNT, "steps do not add up");
  EXPECT(story_steps == 28, "story has %d steps", story_steps);
  EXPECT(STORY_GATE[0][0] == 1 && STORY_GATE[1][0] == 4 && STORY_GATE[2][0] == 9 && STORY_GATE[3][0] == 15 &&
         STORY_GATE[4][0] == 21 && STORY_GATE[5][0] == 27, "story days");
  std::printf("tables: %d quests, %d steps (%d bytes of tables)\n", QUEST_COUNT, QSTEP_COUNT,
              (int)(sizeof(QUESTS) + sizeof(QSTEPS) + sizeof(STORY_GATE)));
}

// 28 definitions are reachable: a fixed point over the requirements, starting on day 1 with every goal buyable
static void test_reachability(void) {
  uint32_t reached = 0;
  int tool_lvl[8] = {1, 1, 2, 3, 1, 1, 1, 0};
  for (int pass = 0; pass < 40; pass++) {
    uint32_t before = reached;
    for (int q = 0; q < QUEST_COUNT; q++) {
      const QuestDef& d = QUESTS[q];
      int ok = 1;
      switch (d.req_kind) {
        case QR_QUEST: ok = (reached >> d.req_val) & 1; break;
        case QR_TOOL: ok = tool_lvl[d.req_val >> 4] >= (d.req_val & 15); break;
        case QR_FRIEND: ok = (d.req_val & 15) <= 4; break;
        default: break;
      }
      if (q < 6) {
        if (q > 0 && !((reached >> (q - 1)) & 1)) ok = 0;
        if (STORY_GATE[q][1] == 1 && tool_lvl[STORY_GATE[q][2] >> 4] < (STORY_GATE[q][2] & 15)) ok = 0;
        if (q == 5 && !(((reached >> 1) & 15) == 15)) ok = 0;   // the bell tower needs four shards
      }
      if (ok) reached |= 1u << q;
    }
    if (reached == before) break;
  }
  EXPECT(reached == 0xFFFFFFFFu || QUEST_COUNT < 32, "unreachable quests, mask %08x", (unsigned)reached);
  int n = 0;
  for (int q = 0; q < QUEST_COUNT; q++) n += (reached >> q) & 1;
  EXPECT(n == QUEST_COUNT, "%d of %d reachable", n, QUEST_COUNT);
}

// ----------------------------------------------------------------------- 2. every quest plays out
static void test_every_quest(void) {
  for (int q = 0; q < QUEST_COUNT; q++) {
    gc_init(GC_SCENE_VILLAGE, 1000 + q, 0);
    all_unlocked();
    G.s.day = 112 + 6;   // a festival day (6 mod 7), in year 5, in season (118/7)%4 = 0
    G.s.minute = 8 * 60;
    // legends need their season
    if (q >= QUEST_LEGEND0) G.s.day = (uint16_t)(7 * 4 * 4 + 7 * (q - QUEST_LEGEND0));
    G.s.errands_today = 0;
    G.s.inv[GQ_ITEM_SHARD] = 4;
    if (q >= QUEST_SIDE0) { G.s.q_done[0] = 0x3F; G.s.chapter = 6; }   // the story is finished, it would preempt
    quest_debug_start(q);
    CHECK(G.s.q_active == q);
    long coins0 = (long)G.s.earned_total;
    int steps_taken = 0, last_step = -1;
    for (int it = 0; it < 80 && G.s.q_active == q; it++) {
      GqNeed n;
      int has = quest_need(&n);
      check_need_invariants(n);
      if (G.s.q_active != q) break;
      if (has && n.kind != GQ_NEED_WAIT) { act_on(n); }
      else { gc_debug(GQ_DBG_NEXT_DAY, 0); G.s.minute = 8 * 60; }
      if ((int)G.s.q_step != last_step) { last_step = G.s.q_step; steps_taken++; }
      check_state();
    }
    EXPECT(G.s.q_active != q, "quest %d did not finish (step %d)", q, G.s.q_step);
    if (q < QUEST_ERRAND0) {
      if (q < QUEST_LEGEND0) EXPECT((G.s.q_done[q >> 3] >> (q & 7)) & 1, "quest %d not marked done", q);
      else EXPECT((G.s.year_legends >> (q - QUEST_LEGEND0)) & 1, "legend %d not marked", q);
      EXPECT((long)G.s.earned_total - coins0 >= (long)QUESTS[q].reward_coins5 * 5 || G.s.coins >= econ_coin_cap(), "quest %d paid nothing", q);
    }
    if (q >= 1 && q <= 4) EXPECT(G.s.inv[GQ_ITEM_SHARD] >= 4, "shard of chapter %d", q);   // 4 granted at the start, cap 4
    (void)steps_taken;
  }
}

// ------------------------------------------------------------------------- 3. story pacing, long
struct DriverStats { int ch6_day, side_done_day, max_errands_per_day, legends_years_ok; };

// Module-level soak: no rendering, one simulated day per iteration; `actions_per_day` limits a lazy hero;
// `skip_pct` makes the hero ignore a need now and then. Returns the day on which chapter 6 was reached.
static DriverStats play_days(uint32_t seed, int days, int actions_per_day, int skip_pct) {
  gc_init(GC_SCENE_VILLAGE, seed, 0);
  uint32_t rng = seed * 2654435761u + 7;
  DriverStats st = {-1, -1, 0, 0};
  int errands_prev = 0, errands_day_total = 0, legends_ok = 0;
  for (int day = 0; day < days; day++) {
    G.s.minute = 6 * 60 + 5;
    apply_goals(G.s.day);
    for (int n = 0; n < 12; n++) econ_friend_add(n, 4);   // chats over the day
    errands_prev = G.s.stat[GQ_STAT_ERRANDS];
    int actions = 0;
    for (int it = 0; it < 60 && actions < actions_per_day; it++) {
      G.s.minute = (uint16_t)(6 * 60 + 5 + it * 10);
      GqNeed n;
      quest_need(&n);
      check_need_invariants(n);
      check_state();
      EXPECT(G.s.chapter <= 1 + G.s.day / 5, "chapter %d on day %d", G.s.chapter, G.s.day);
      if (n.kind == GQ_NEED_NONE || n.kind == GQ_NEED_WAIT) break;
      rng = rng * 1664525u + 1013904223u;
      if ((int)((rng >> 16) % 100) < skip_pct) { actions++; continue; }
      if (act_on(n)) actions++;
      apply_goals(G.s.day);
      check_state();
    }
    errands_day_total = G.s.stat[GQ_STAT_ERRANDS] - errands_prev;
    if (errands_day_total > st.max_errands_per_day) st.max_errands_per_day = errands_day_total;
    EXPECT(errands_day_total <= 3, "%d paid errands on day %d", errands_day_total, G.s.day);
    if (G.s.chapter == 6 && st.ch6_day < 0) st.ch6_day = G.s.day;
    if (G.s.chapter < 6 && G.s.day < 27) EXPECT(!((G.s.q_done[0] >> 5) & 1), "chapter 5 done before day 27");
    if (G.s.day >= 56 && G.s.day % 28 == 27 && (G.s.year_legends & 15) == 15) legends_ok++;
    gc_debug(GQ_DBG_NEXT_DAY, 0);
  }
  st.legends_years_ok = legends_ok;
  return st;
}

static void test_story_pacing(void) {
  // an eager hero: story ends on day 27, the first festival day the bell can ring, and not a day sooner
  DriverStats a = play_days(1, 420, 60, 0);
  std::printf("eager hero: chapter 6 on day %d, max %d errands/day, legend years complete %d\n", a.ch6_day, a.max_errands_per_day, a.legends_years_ok);
  EXPECT(a.ch6_day == 27, "eager hero finished on day %d", a.ch6_day);
  EXPECT(a.legends_years_ok >= 10, "legends done in %d years", a.legends_years_ok);
  EXPECT(a.max_errands_per_day == 3, "errand cap not reached (%d)", a.max_errands_per_day);
  // lazy and distracted heroes: never earlier than 27, and the village does finish
  for (uint32_t seed = 2; seed <= 41; seed++) {
    int actions = 1 + (int)(seed % 4), skip = (int)((seed * 7) % 60);
    DriverStats s = play_days(seed, 300, actions, skip);
    EXPECT(s.ch6_day >= 27, "seed %u finished on day %d", (unsigned)seed, s.ch6_day);
    EXPECT(s.ch6_day <= 160, "seed %u (actions %d, skip %d%%) needed until day %d", (unsigned)seed, actions, skip, s.ch6_day);
    EXPECT(s.max_errands_per_day <= 3, "errands");
  }
  // the ideal hero but without ever buying the bell tower: the story waits for it and does not break
  gc_init(GC_SCENE_VILLAGE, 5, 0);
  for (int day = 0; day < 60; day++) {
    G.s.minute = 7 * 60;
    for (unsigned i = 0; i < sizeof(GOALS) / sizeof(GOALS[0]); i++) {
      if (GOALS[i].day > G.s.day) continue;
      if (GOALS[i].kind == 0) G.s.tool[GOALS[i].id] = (uint8_t)GOALS[i].level; else G.s.projects = (uint16_t)(G.s.projects | (1u << GOALS[i].id));
    }
    for (int n = 0; n < 12; n++) econ_friend_add(n, 4);
    for (int it = 0; it < 60; it++) { GqNeed n; quest_need(&n); if (n.kind == GQ_NEED_NONE || n.kind == GQ_NEED_WAIT) break; act_on(n); }
    gc_debug(GQ_DBG_NEXT_DAY, 0);
  }
  EXPECT(G.s.chapter == 4 && !((G.s.q_done[0] >> 5) & 1), "chapter %d without a bell tower", G.s.chapter);
}

// ------------------------------------------------------------------------------- 4. event storms
static uint32_t s_rng = 12345;
static uint32_t rnd32(void) { s_rng = s_rng * 1664525u + 1013904223u; return s_rng ^ (s_rng >> 15); }

static void test_storm(void) {
  gc_init(GC_SCENE_VILLAGE, 77, 0);
  char buf[48];
  for (long i = 0; i < 1000000; i++) {
    uint32_t r = rnd32();
    int ev = (int)(r % (GQ_EV_COUNT + 3)) - 1;       // includes two invalid event numbers
    int a = (int)((r >> 8) % 300) - 20, b = (int)((r >> 18) % 300) - 20;
    if (r % 7 == 0) { a = (int)(rnd32() % 10); b = (int)(rnd32() % 225); }
    if (r % 97 == 0) { a = (int)rnd32(); b = (int)rnd32(); }
    quest_on_event(ev, a, b);
    if (i % 17 == 0) {
      uint32_t k = rnd32();
      switch (k % 12) {
        case 0: G.s.day = (uint16_t)(k >> 8); break;
        case 1: G.s.minute = (uint16_t)((k >> 8) % 1440); break;
        case 2: G.s.tool[(k >> 8) % 8] = (uint8_t)((k >> 12) % 4); break;
        case 3: G.s.projects = (uint16_t)(k >> 8) & 0x3FF; break;
        case 4: G.s.friend_[(k >> 8) % 12] = (uint8_t)((k >> 12) % 101); break;
        case 5: econ_add_item((int)((k >> 8) % 24), (int)((k >> 16) % 10)); break;
        case 6: econ_take_item((int)((k >> 8) % 24), (int)((k >> 16) % 5)); break;
        case 7: quest_debug_start((int)((k >> 8) % 40) - 4); break;
        case 8: { GqNeed n; quest_need(&n); check_need_invariants(n); break; }
        case 9: quest_talk((int)((k >> 8) % 14) - 2, buf, 48); talk_begin((int)((k >> 8) % 14) - 2); talk_turn((int)((k >> 12) % 6) - 1, buf, 48); break;
        case 10: { int nu, de; quest_band(buf, 48, &nu, &de); quest_title(buf, 48); talk_quest_reply(buf, 48); break; }
        default:
          // corrupt the quest fields on purpose: every use must validate
          G.s.q_active = (uint8_t)(k >> 8); G.s.q_step = (uint8_t)(k >> 16); G.s.q_count = (uint8_t)(k >> 24);
          break;
      }
    }
    if (i % 1009 == 0) {
      if (G.s.q_active != 0xFF && G.s.q_active < QUEST_COUNT && G.s.q_step < quest_steps(G.s.q_active)) check_state();
    }
    if (i % 50000 == 0) { G.s.coins = 0; G.s.errands_today = 0; }   // the till reset, so payments keep being exercised
  }
  // after the storm, one need call repairs any corrupted state
  GqNeed n; quest_need(&n);
  check_state();
  std::printf("storm: 1000000 random events survived\n");
}

// --------------------------------------------------------------------------------- 5. save round trip
static void test_save_mid_quest(void) {
  gc_init(GC_SCENE_VILLAGE, 31, 0);
  all_unlocked();
  G.s.day = 40; G.s.minute = 9 * 60;
  quest_debug_start(1);
  GqNeed n;
  quest_need(&n); act_on(n);          // talk to Luma
  quest_need(&n); act_on(n);          // herbs
  quest_need(&n); act_on(n);          // clearing
  quest_need(&n);
  CHECK(n.kind == GQ_NEED_SLAY);
  gq_emit(GQ_EV_SLAIN, GQ_MON_SLIME, 0);
  CHECK(G.s.q_active == 1 && G.s.q_step == 3 && G.s.q_count == 1);
  // a damaged save must not leave an invalid quest behind
  GqSave bad = G.s;
  bad.q_active = 200; bad.q_step = 250; bad.q_count = 250; bad.chapter = 99;
  gq_save_sanitize(&bad);
  EXPECT(bad.q_active == 0xFF && bad.q_step == 0 && bad.q_count <= 99 && bad.chapter <= 6, "sanitize");
}

// ------------------------------------------------------------------------------ 6. text API behaviour
static void test_text_api(void) {
  gc_init(GC_SCENE_VILLAGE, 9, 0);
  char buf[48];
  // the ordinary chat lines moved to the dialogue scripts (talk.cpp, test_talk.cpp); here: the quest side of a chat
  quest_talk(77, buf, 48); EXPECT(buf[0] == 0, "bad npc");
  int npc = -1, kind = quest_chat_kind(&npc);
  EXPECT(kind >= 0 && kind < QK_COUNT, "chat kind range %d", kind);
  EXPECT(quest_chat_state(GQ_NPC_PIP) == 2, "no quest: state 2");
  for (int k = 0; k < 50; k++) { talk_quest_reply(buf, 48); EXPECT(buf[0] == 0 || std::strlen(buf) < 40, "reply fits"); }
  all_unlocked();
  // an active story quest: the giver's own state is 0, the others see a story quest (1)
  quest_debug_start(1);
  EXPECT(G.s.q_active == 1, "story quest 1 started");
  EXPECT(quest_chat_state(GQ_NPC_LUMA) == 0, "giver sees his quest");
  EXPECT(quest_chat_state(GQ_NPC_PIP) == 1, "others see a story quest");
  EXPECT(quest_talk(GQ_NPC_LUMA, buf, 48) && buf[0], "luma speaks of the quest");
  kind = quest_chat_kind(&npc);
  EXPECT(npc == GQ_NPC_LUMA && kind == QK_OFFER, "first step is an offer, got %d", kind);
  // the hero's answer is quest aware and never empty once the data exists
  talk_quest_reply(buf, 48); EXPECT(buf[0] != 0, "quest answer");
  // band: counts and progress; title
  gc_init(GC_SCENE_VILLAGE, 11, 0);
  all_unlocked();
  int nu, de;
  EXPECT(!quest_band(buf, 48, &nu, &de) && buf[0] == 0, "no band without quest");
  EXPECT(!quest_title(buf, 48), "no title without quest");
  quest_debug_start(8);   // Pip vs slimes
  CHECK(quest_title(buf, 48) && std::strcmp(buf, "PIP VS SLIMES") == 0);
  GqNeed n; quest_need(&n); act_on(n);                 // talk
  CHECK(quest_band(buf, 48, &nu, &de));
  EXPECT(std::strcmp(buf, "SLIMES 0/2") == 0 && nu == 0 && de == 2, "band '%s' %d/%d", buf, nu, de);
  gq_emit(GQ_EV_SLAIN, GQ_MON_SLIME, 0);
  quest_band(buf, 48, &nu, &de);
  EXPECT(std::strcmp(buf, "SLIMES 1/2") == 0 && nu == 1 && de == 2, "band '%s'", buf);
  EXPECT((int)std::strlen(buf) <= 20, "band longer than the 20 characters of the band");
  quest_band(buf, 4, &nu, &de); EXPECT(std::strlen(buf) <= 3, "band respects cap");
  // the hero's own line for the step that just started is delivered once
  CHECK(quest_talk(GQ_NPC_HERO, buf, 48) || true);
  EXPECT(!quest_talk(GQ_NPC_HERO, buf, 48), "hero line is one-shot");
  // wrong-monster events do nothing; a faint postpones the step to tomorrow
  gq_emit(GQ_EV_SLAIN, GQ_MON_BAT, 0);
  EXPECT(G.s.q_count == 1, "bat counted for a slime step");
  gq_emit(GQ_EV_FAINTED, 0, 0);
  CHECK(G.s.q_count == 0);
  quest_need(&n); EXPECT(n.kind == GQ_NEED_WAIT, "fainted hero waits until tomorrow (kind %d)", n.kind);
  gc_debug(GQ_DBG_NEXT_DAY, 0);
  quest_need(&n); EXPECT(n.kind == GQ_NEED_SLAY && n.item == GQ_MON_SLIME && n.count == 2, "slay need after a night's sleep");
  // locked areas are never pointed at: the shore without the bridge
  gc_init(GC_SCENE_VILLAGE, 12, 0);
  G.s.day = 30;
  quest_debug_start(2);
  quest_need(&n);   // talk to Finn, who lives at the shore
  EXPECT(n.kind == GQ_NEED_WAIT, "Finn is unreachable without the bridge (kind %d area %d)", n.kind, n.area);
  G.s.projects |= 1u << QP_BRIDGE;
  quest_need(&n);
  EXPECT(n.kind == GQ_NEED_TALK && n.npc == GQ_NPC_FINN && n.area == GQ_AREA_SHORE, "talk to Finn at the shore");
  // errands: need the notice board, never more than three paid per day
  gc_init(GC_SCENE_VILLAGE, 13, 0);
  all_unlocked();
  G.s.q_done[0] = 0xFF; G.s.q_done[1] = 0xFF; G.s.q_done[2] = 0x0F;   // story and side quests finished
  G.s.chapter = 6;
  G.s.projects = 0; G.s.day = 12; G.s.minute = 7 * 60;
  quest_need(&n); EXPECT(n.kind == GQ_NEED_NONE && G.s.q_active == 0xFF, "no errand without a notice board");
  G.s.projects = 1u << QP_BOARD;
  G.s.minute = 5 * 60;
  quest_need(&n); EXPECT(G.s.q_active == 0xFF, "errands are drawn at 06:00, not before");
  G.s.minute = 6 * 60;
  quest_need(&n); EXPECT(G.s.q_active >= QUEST_ERRAND0 && G.s.q_active < QUEST_LEGEND0, "an errand starts at 06:00");
  int paid = 0;
  for (int k = 0; k < 40; k++) {
    int e0 = G.s.stat[GQ_STAT_ERRANDS];
    quest_need(&n);
    if (n.kind == GQ_NEED_NONE || n.kind == GQ_NEED_WAIT) break;
    act_on(n);
    paid += G.s.stat[GQ_STAT_ERRANDS] - e0;
  }
  EXPECT(paid == 3 && G.s.errands_today == 3 && G.s.q_active == 0xFF, "three paid errands, then none (%d)", paid);
  gq_emit(GQ_EV_QUEST_DONE, 0xFF, 0);
  EXPECT(G.s.errands_today == 3, "the stopgap trip cannot exceed the daily cap");
}

// ------------------------------------------------------------------- 7. the whole game, many days
static int g_game_days = 34;

static void soak_whole_game(uint32_t seed) {
  sim_start(seed);
  int ch6_day = -1;
  int last_day = -1, errands_at_day_start = 0;
  uint32_t said = 0;
  for (int hour = 0; hour < g_game_days * 24; hour++) {
    sim_minutes(60);
    int day = gc_query(GQ_Q_DAY);
    if (day != last_day) {
      if (last_day >= 0) EXPECT(G.s.stat[GQ_STAT_ERRANDS] - errands_at_day_start <= 3 + 3, "errands per day");   // a day boundary may fall inside an hour
      last_day = day;
      errands_at_day_start = G.s.stat[GQ_STAT_ERRANDS];
      apply_goals(day);
      for (int n = 0; n < 12; n++) econ_friend_add(n, 4);
    }
    // the hero's quest trips: a few needs per simulated hour, shown in the real dialogue box
    for (int k = 0; k < 3; k++) {
      GqNeed n;
      quest_need(&n);
      check_need_invariants(n);
      if (n.kind == GQ_NEED_NONE || n.kind == GQ_NEED_WAIT) break;
      if (n.kind == GQ_NEED_TALK) {
        char line[48];
        if (quest_talk(n.npc, line, 48)) { ui_say(n.npc, line, 800); said++; }
      }
      act_on(n);
      apply_goals(day);
    }
    check_state();
    EXPECT(gc_query(GQ_Q_CHAPTER) <= 1 + gc_query(GQ_Q_DAY) / 5, "chapter %d on day %d", (int)gc_query(GQ_Q_CHAPTER), (int)gc_query(GQ_Q_DAY));
    EXPECT(gc_query(GQ_Q_HERO_PRESENCE) != 0, "presence");
    if (gc_query(GQ_Q_CHAPTER) == 6 && ch6_day < 0) ch6_day = gc_query(GQ_Q_DAY);
    if (hour % 24 == 12) {   // the status band text is always short, upper case words
      char st[24];
      hero_status_text(st, 24);
      EXPECT(st[0] && std::strlen(st) <= 19, "status text '%s'", st);
    }
  }
  std::printf("whole game, seed %u: %d days, chapter 6 on day %d, %u spoken quest lines, errands %d, quests done mask %02x%02x%02x%02x\n",
              (unsigned)seed, g_game_days, ch6_day, (unsigned)said, (int)G.s.stat[GQ_STAT_ERRANDS],
              G.s.q_done[3], G.s.q_done[2], G.s.q_done[1], G.s.q_done[0]);
  if (g_game_days >= 34) {
    EXPECT(ch6_day >= 27 && ch6_day <= 34, "story ended on day %d", ch6_day);
    EXPECT(G.s.q_done[0] == 0xFF && G.s.q_done[1] == 0xFF && (G.s.q_done[2] & 0x0F) == 0x0F, "all 6 story and 14 side quests done");
  }
}

int main(int argc, char** argv) {
  if (argc > 1) g_game_days = std::atoi(argv[1]);
  test_text_rules();
  test_tables();
  test_reachability();
  test_every_quest();
  test_text_api();
  test_save_mid_quest();
  test_story_pacing();
  test_storm();
  std::printf("module-level checks done, %ld talk lines, starting whole-game soak\n", g_talk_lines);
  for (uint32_t seed = 1; seed <= 2; seed++) soak_whole_game(seed);
  if (g_fail) { std::printf("FAILED: %d checks\n", g_fail); return 1; }
  std::printf("PASS\n");
  return 0;
}
