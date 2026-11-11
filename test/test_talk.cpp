// Dialogue engine (docs/DESIGN-R3.md 4.6 and 9.6, chunk D1): the compiled tables, the decoder against the source files,
// selection (every script reachable, specific beats generic, no repeats), the hero's solo lines and quest answers, and chats
// in the running game. Run from the repository root (it reads content/talk/*.txt).
#include "gq_test.h"
#include "../lib/gamecore/src/gq_mod.h"
#include "../lib/gamecore/src/quest_data.h"
#define TALK_DATA_IMPL
#include "../lib/gamecore/src/talk_data.h"
#include <string>
#include <vector>
#include <set>
#include <map>

int  talk_dbg_begin(int script, int npc);
int  talk_dbg_line(int line, char* buf, int cap, int npc);
int  talk_dbg_last(void);
int  talk_dbg_begins(void);
int  quest_chat_state(int npc);
int  quest_chat_kind(int* npc);
void quest_dbg_set_chat(int kind, int npc);

static int g_fail;
static int COND(int i) { return TALK_SCRIPTS[i].cond & ~TS_HERO; }
// number of the turn (file order) that starts at blob offset off
static int line_of_offset(int off) { int n = 0; for (int o = 0; o < off; o++) if (TALK_BLOB[o] == 0) n++; return n; }
#define EXPECT(cond, ...) do { if (!(cond)) { std::printf("FAIL %s:%d: %s  ", __FILE__, __LINE__, #cond); std::printf(__VA_ARGS__); std::printf("\n"); if (++g_fail > 25) std::exit(1); } } while (0)

static const char* GLYPHS = " !#%'()+,-./0123456789:<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ_";
static const char* BANNED[] = {"TELL ME MORE!", "I'LL KEEP WATCH.", "SOUNDS GOOD!", "SURE THING.", "I SEE!", "GOOD TO KNOW.", "REALLY? NICE!"};
static const int SLOT_MAX[8] = {0, 0, 4, 6, 6, 9, 8, 4};   // by slot code 2..7 (T W S G I N)
static const char SLOT_CH[6] = {'T', 'W', 'S', 'G', 'I', 'N'};

// a turn as the dialogue box shows it: 2 lines of 19, font glyphs only, no stray spaces
static int box_ok(const char* t, const char* what) {
  int n = (int)std::strlen(t), lines = 1, run = 0, ok = 1;
  if (n < 1 || n > 39) { std::printf("  %s: length %d: %s\n", what, n, t); return 0; }
  for (int i = 0; i <= n; i++) {
    char c = t[i];
    if (c == '|' || c == 0) {
      if (run < 1 || run > 19) { std::printf("  %s: line of %d: %s\n", what, run, t); ok = 0; }
      if (c == '|') lines++;
      run = 0;
      continue;
    }
    run++;
    if (!std::strchr(GLYPHS, c)) { std::printf("  %s: glyph %c: %s\n", what, c, t); ok = 0; }
    if (c == ' ' && (run == 1 || t[i + 1] == '|' || t[i + 1] == 0 || t[i + 1] == ' ')) { std::printf("  %s: stray space: %s\n", what, t); ok = 0; }
  }
  if (lines > 2) { std::printf("  %s: %d lines: %s\n", what, lines, t); ok = 0; }
  return ok;
}

// ------------------------------------------------------------------------------------------------ source files
struct Src { std::string text; bool hero; };
static std::vector<Src> g_src;   // every turn of content/talk in line id order (the order tools/gen_talk.py numbers them)
static int g_files_found;

static void load_sources() {
  static const char* FILES[] = {"mira", "bram", "pip", "odda", "finn", "tess", "luma", "wren", "miso", "_shared", "_hero"};
  for (const char* f : FILES) {
    std::string path = std::string("content/talk/") + f + ".txt";
    FILE* fp = std::fopen(path.c_str(), "r");
    if (!fp) continue;
    g_files_found++;
    char line[256];
    while (std::fgets(line, sizeof line, fp)) {
      std::string s(line);
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
      if (s.size() >= 3 && (s[0] == 'N' || s[0] == 'H') && s[1] == ':') {
        size_t p = 2;
        if (s[p] == ' ') p++;
        g_src.push_back({s.substr(p), s[0] == 'H'});
      }
    }
    std::fclose(fp);
  }
}

// does the decoded text fit the source once every {X} is replaced by some value of at most its maximum length?
static int slot_index(char c) { for (int i = 0; i < 6; i++) if (SLOT_CH[i] == c) return i + 2; return -1; }
static bool match(const std::string& src, const char* dec) {
  std::string d(dec);
  size_t sp = 0, dp = 0;
  while (sp < src.size()) {
    if (src[sp] == '{' && sp + 2 < src.size() + 0 && src[sp + 2] == '}' && slot_index(src[sp + 1]) > 0) {
      int mx = SLOT_MAX[slot_index(src[sp + 1])];
      sp += 3;
      // the literal text up to the next slot
      size_t e = sp;
      while (e < src.size() && src[e] != '{') e++;
      std::string lit = src.substr(sp, e - sp);
      bool found = false;
      for (int len = 1; len <= mx && dp + len <= d.size(); len++) {
        if (d.compare(dp + len, lit.size(), lit) == 0 && (e < src.size() || dp + len + lit.size() == d.size())) { dp += len; found = true; break; }
      }
      if (!found) return false;
    } else {
      if (dp >= d.size() || d[dp] != src[sp]) return false;
      dp++; sp++;
    }
  }
  return dp == d.size();
}

// ---------------------------------------------------------------------------------------------------- state
enum { MIRA_FIRST_NPC = 1 };
static void baseline(void) {
  gc_init(GC_SCENE_VILLAGE, 4242, 0);
  G.s.day = 2; G.s.minute = 12 * 60; G.s.weather = GQ_W_CLEAR; G.s.event_today = 0; G.s.chapter = 6; G.s.q_active = 0xFF; G.s.q_step = 0;
  for (int i = 0; i < 12; i++) G.s.friend_[i] = 0;
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  talk_init();
}

static int live_works(void) {   // W2 (calendar.cpp) provides the live temperature; before it merges the stubs say no
  int c;
  gc_set_live_weather(GQ_W_CLEAR, 215, 0);
  int ok = cal_temp_c(&c) && (c == 21 || c == 22);   // 21.5 C rounds half away from zero: 22 (accept 21 too)
  gc_set_live_weather(GC_WX_NONE, 0, 0);
  return ok;
}

static const int DID_EV[13] = {0, GQ_EV_CHOPPED, GQ_EV_CAUGHT, GQ_EV_FORAGED, GQ_EV_MINED, GQ_EV_SLAIN, GQ_EV_FAINTED, GQ_EV_SOLD,
                               GQ_EV_BOUGHT, GQ_EV_BUILT, GQ_EV_ATE, GQ_EV_GIFTED, GQ_EV_WOKE};

// ---------------------------------------------------------------------------------------------------- tests
static void test_tables(void) {
  EXPECT(TALK_NSCRIPTS > 0 && TALK_NSCRIPTS <= 255, "script count %d", TALK_NSCRIPTS);
  EXPECT(TALK_BLOB_BYTES <= 10240, "blob %d", TALK_BLOB_BYTES);
  for (int g = 0; g < 12; g++) EXPECT(TALK_FIRST[g] <= TALK_FIRST[g + 1], "TALK_FIRST not monotone at %d", g);
  EXPECT(TALK_FIRST[12] == TALK_NSCRIPTS, "TALK_FIRST end");
  int lines = 0;
  for (int i = 0; i < TALK_NSCRIPTS; i++) {
    int cond = COND(i), p = TALK_SCRIPTS[i].pack, turns = (p >> 14) + 1, first = line_of_offset(p & 0x3FFF);
    int group = 0;
    while (group < 11 && i >= TALK_FIRST[group + 1]) group++;
    EXPECT(cond <= TC_QREPLY, "cond %d", cond);
    EXPECT(first + turns <= TALK_NLINES, "script %d lines out of range", i);
    if (group <= 9) EXPECT(turns >= 2 && turns <= 4, "script %d has %d turns", i, turns);
    else EXPECT(turns == 1, "solo script %d has %d turns", i, turns);
    if (group <= 9) EXPECT(cond != TC_BED && cond != TC_WAKE && cond != TC_SOLODID && cond != TC_QREPLY, "solo condition in a chat group %d", i);
    if (group == 10) EXPECT(cond == TC_BED || cond == TC_WAKE || cond == TC_SOLODID, "group 10 cond %d", cond);
    if (group == 11) EXPECT(cond == TC_QREPLY && TALK_SCRIPTS[i].arg < QK_COUNT, "group 11");
    if (cond == TC_FRIEND) EXPECT(TALK_SCRIPTS[i].arg >= 1 && TALK_SCRIPTS[i].arg <= 3, "friend tier");
    if (cond == TC_DID || cond == TC_SOLODID) EXPECT(TALK_SCRIPTS[i].arg >= 1 && TALK_SCRIPTS[i].arg < TD_COUNT, "did kind");
    if (cond == TC_QUEST) EXPECT(TALK_SCRIPTS[i].arg <= 2, "quest arg");
    if (cond == TC_TIME) EXPECT(TALK_SCRIPTS[i].arg <= 3, "time arg");
    if (cond == TC_WX) EXPECT(TALK_SCRIPTS[i].arg < GQ_W_COUNT, "wx arg");
    if (cond == TC_EVENT) EXPECT(TALK_SCRIPTS[i].arg >= 1 && TALK_SCRIPTS[i].arg <= 12, "event arg");
    lines += turns;
  }
  EXPECT(lines == TALK_NLINES, "turns %d vs lines %d", lines, TALK_NLINES);
  for (int d = 0; d < TALK_DICT_N; d++) EXPECT(TALK_DOFF[d + 1] - TALK_DOFF[d] >= 2 && TALK_DOFF[d + 1] - TALK_DOFF[d] <= 8, "dictionary entry %d length", d);
  std::printf("tables: %d scripts, %d lines, blob %d bytes, dictionary %d entries\n", TALK_NSCRIPTS, TALK_NLINES, TALK_BLOB_BYTES, TALK_DICT_N);
}

static void test_sources_and_decoder(void) {
  load_sources();
  EXPECT(g_files_found >= 3, "content/talk files found: %d (run the test from the repository root)", g_files_found);
  EXPECT((int)g_src.size() == TALK_NLINES, "sources have %d turns, the header %d: run python3 tools/gen_talk.py", (int)g_src.size(), TALK_NLINES);
  if ((int)g_src.size() != TALK_NLINES) return;
  // rules of 4.6 on the sources: no line twice, no banned or short hero line
  std::set<std::string> seen;
  for (size_t i = 0; i < g_src.size(); i++) {
    std::string flat = g_src[i].text;
    for (char& c : flat) if (c == '|') c = ' ';
    EXPECT(seen.insert(flat).second, "line occurs twice: %s", g_src[i].text.c_str());
    if (g_src[i].hero) {
      for (const char* b : BANNED) EXPECT(flat != b, "banned hero line %s", b);
      EXPECT(flat.size() >= 8, "hero line too short: %s", g_src[i].text.c_str());
    }
  }
  // the decoder against the sources, under many situations (every slot takes several values)
  int ok = 1, nlive = live_works();
  static const int TEMPS[6] = {-999, -50, 0, 231, 999, 1000};
  char buf[48];
  for (int sc = 0; sc < 14; sc++) {
    baseline();
    G.s.day = (uint16_t)(sc * 5 + 1);
    G.s.weather = (uint8_t)(sc % GQ_W_COUNT);
    gc_set_live_weather(sc % 3 == 2 ? GC_WX_NONE : sc % GQ_W_COUNT, TEMPS[sc % 6], sc % 2 ? GC_WXF_FAHRENHEIT : 0);
    if (sc & 4) G.s.tool[GQ_TOOL_AXE] = 3;
    if (sc & 8) G.s.projects = 0x3FF;
    for (int line = 0; line < TALK_NLINES; line++) {
      int npc = 1 + (line + sc) % 9;
      int n = talk_dbg_line(line, buf, sizeof buf, npc);
      EXPECT(n == (int)std::strlen(buf), "length mismatch");
      if (!match(g_src[line].text, buf)) { std::printf("  decode mismatch, line %d: src `%s` got `%s`\n", line, g_src[line].text.c_str(), buf); ok = 0; }
      if (!box_ok(buf, "decoded")) ok = 0;
      if (!ok) break;
    }
    if (!ok) break;
  }
  EXPECT(ok, "decoder");
  // worst case widths explicitly: the longest value of every slot in one scenario
  baseline();
  gc_set_live_weather(GQ_W_STORM, -999, 0);
  for (int line = 0; line < TALK_NLINES; line++) { talk_dbg_line(line, buf, sizeof buf, GQ_NPC_MIRA); EXPECT(box_ok(buf, "widest"), "widest line %d", line); }
  // capacity: never writes past cap-1, always terminates
  static char guard[64];
  for (int cap = 1; cap < 46; cap++) {
    for (int line = 0; line < TALK_NLINES; line += 3) {
      std::memset(guard, 0x7E, sizeof guard);
      talk_dbg_line(line, guard, cap, 3);
      EXPECT(guard[cap] == 0x7E || cap >= 63, "write past cap %d", cap);
      EXPECT((int)std::strlen(guard) <= cap - 1 || guard[0] == 0x7E, "unterminated at cap %d", cap);
    }
  }
  talk_dbg_line(-1, buf, 8, 1); EXPECT(buf[0] == 0, "bad line");
  talk_dbg_line(TALK_NLINES, buf, 8, 1); EXPECT(buf[0] == 0, "line past the end");
  std::printf("decoder: %d turns match the sources in 14 situations (live temperature %s)\n", TALK_NLINES, nlive ? "tested" : "NOT TESTED: W2 not merged");
}

static int group_has_chat_scripts(int npc) { return TALK_FIRST[npc] > TALK_FIRST[npc - 1]; }

// Puts the game in a state where condition (cond, arg) is true for villager npc. Returns 0 if that cannot be done here.
static int apply(int cond, int arg, int npc, int live) {
  baseline();
  switch (cond) {
    case TC_ANY: return 1;
    case TC_TIME: G.s.minute = (uint16_t)((arg == 0 ? 7 : arg == 1 ? 12 : arg == 2 ? 18 : 23) * 60); return 1;
    case TC_WX: G.s.weather = (uint8_t)arg; return cal_weather_shown() == arg;
    case TC_TEMP: if (!live) return 0; gc_set_live_weather(GQ_W_CLEAR, arg ? 320 : -80, 0); return 1;
    case TC_LIVE: if (!live) return 0; gc_set_live_weather(GQ_W_CLEAR, 150, 0); return 1;
    case TC_SEASON: G.s.day = (uint16_t)(arg * 7 + 2); return cal_season() == arg;
    case TC_FEST:
      G.s.day = 6; G.s.minute = (uint16_t)(arg ? 18 * 60 : 10 * 60);
      return arg ? cal_festival() >= 0 : cal_festival() < 0;
    case TC_DID: return 1;   // remembered by the caller after every talk_init
    case TC_QUEST:
      if (arg == 2) { G.s.q_active = 0xFF; return quest_chat_state(npc) == 2; }
      for (int q = 0; q < QUEST_COUNT; q++) {
        if (arg == 0 && GC_RD8(&QUESTS[q].giver) != npc) continue;
        if (arg == 1 && (q > 5 || GC_RD8(&QUESTS[q].giver) == npc)) continue;
        gc_debug(GQ_DBG_START_QUEST, q);
        if (quest_chat_state(npc) == arg) return 1;
      }
      return 0;
    case TC_CHAPTER: G.s.chapter = (uint8_t)arg; return 1;
    case TC_FRIEND: G.s.friend_[npc] = (uint8_t)(arg * 25); return 1;
    case TC_GOAL: {
      for (int t = 0; t < GQ_TOOL_COUNT; t++) G.s.tool[t] = arg ? 9 : 0;
      G.s.projects = 0; G.s.day = 60;
      GqGoal g;
      return econ_goal(&g) && g.is_project == arg;
    }
    case TC_EVENT: G.s.event_today = (uint8_t)arg; return 1;
    default: return 0;
  }
}

// Every chat script can be chosen (and the hook says which one it was); BED, WAKE, DID solo and QREPLY have their own test.
static void test_reachability(void) {
  int live = live_works(), checked = 0, skipped = 0;
  for (int npc = 1; npc <= 9; npc++) {
    if (!group_has_chat_scripts(npc)) continue;
    int ranges[2][2] = {{TALK_FIRST[npc - 1], TALK_FIRST[npc]}, {TALK_FIRST[9], TALK_FIRST[10]}};
    for (int part = 0; part < (npc == GQ_NPC_MISO ? 1 : 2); part++)
      for (int idx = ranges[part][0]; idx < ranges[part][1]; idx++) {
        int cond = COND(idx), arg = TALK_SCRIPTS[idx].arg;
        if (!apply(cond, arg, npc, live)) {
          if ((cond == TC_TEMP || cond == TC_LIVE) && !live) { skipped++; continue; }
          EXPECT(0, "cannot set up cond %d arg %d for npc %d", cond, arg, npc);
          continue;
        }
        int hit = 0;
        for (int k = 0; k < 4000 && !hit; k++) {
          talk_init();
          if (cond == TC_DID) talk_on_event(DID_EV[arg], 0, 0);
          int turns = talk_begin(npc);
          EXPECT(turns >= 2 && turns <= 4, "turns %d", turns);
          if ((talk_dbg_last() & 0xFF) == idx) hit = 1;
        }
        EXPECT(hit, "script %d (cond %d arg %d) of npc %d is never chosen", idx, cond, arg, npc);
        checked++;
      }
  }
  std::printf("reachability: %d scripts chosen when their condition holds, %d skipped (live temperature needs W2)\n", checked, skipped);
}

static void test_selection(void) {
  // specific beats generic: in rain Mira's rain script is chosen far more often than one of her ANY scripts
  baseline();
  G.s.weather = GQ_W_RAIN;
  int rain_idx = -1, any_idx = -1;
  for (int i = TALK_FIRST[0]; i < TALK_FIRST[1]; i++) {
    if (COND(i) == TC_WX && TALK_SCRIPTS[i].arg == GQ_W_RAIN) rain_idx = i;
    if (COND(i) == TC_ANY && any_idx < 0) any_idx = i;
  }
  if (rain_idx >= 0 && any_idx >= 0) {
    int rain = 0, any = 0, other_weather = 0;
    for (int k = 0; k < 6000; k++) {
      talk_init();
      talk_begin(GQ_NPC_MIRA);
      int idx = talk_dbg_last() & 0xFF;
      if (idx == rain_idx) rain++;
      if (idx == any_idx) any++;
      if (COND(idx) == TC_WX && TALK_SCRIPTS[idx].arg != GQ_W_RAIN) other_weather++;
    }
    EXPECT(rain > any * 2, "rain script %d times, ANY script %d times", rain, any);
    EXPECT(other_weather == 0, "a script for another weather was chosen");
    std::printf("selection: in rain, Mira's rain script %d / ANY script %d of 6000\n", rain, any);
  }
  // a script whose condition is false is never chosen: scan all conditions of Mira at the baseline and check against the hook
  baseline();
  for (int k = 0; k < 3000; k++) {
    talk_init();
    talk_begin(GQ_NPC_MIRA);
    int idx = talk_dbg_last() & 0xFF, c = COND(idx);
    EXPECT(c != TC_WX || TALK_SCRIPTS[idx].arg == GQ_W_CLEAR, "weather script chosen in clear weather");
    EXPECT(c != TC_DID && c != TC_FEST && c != TC_TEMP && c != TC_LIVE && c != TC_FRIEND && c != TC_EVENT, "condition %d chosen but false", c);
    EXPECT(c != TC_SEASON || TALK_SCRIPTS[idx].arg == 0, "wrong season");
    EXPECT(c != TC_TIME || TALK_SCRIPTS[idx].arg == 1, "wrong time block");
  }
  // an action the hero did 31 game minutes ago no longer counts; 29 still does
  int did_idx = -1;
  for (int i = TALK_FIRST[0]; i < TALK_FIRST[1]; i++) if (COND(i) == TC_DID && TALK_SCRIPTS[i].arg == TD_SOLD) did_idx = i;
  if (did_idx >= 0) {
    baseline();
    talk_on_event(GQ_EV_SOLD, 3, 1);
    G.s.minute += 29;
    int hit = 0;
    for (int k = 0; k < 4000 && !hit; k++) { talk_init(); talk_on_event(GQ_EV_SOLD, 3, 1); G.s.minute = 12 * 60 + 29; talk_begin(GQ_NPC_MIRA); hit |= (talk_dbg_last() & 0xFF) == did_idx; G.s.minute = 12 * 60; }
    EXPECT(hit, "a sale 29 minutes ago is not mentioned");
    for (int k = 0; k < 3000; k++) {
      talk_init(); talk_on_event(GQ_EV_SOLD, 3, 1); G.s.minute = 12 * 60 + 31;
      talk_begin(GQ_NPC_MIRA);
      EXPECT((talk_dbg_last() & 0xFF) != did_idx, "a sale 31 minutes ago is mentioned");
      G.s.minute = 12 * 60;
    }
  }
  // robustness: bad villagers and turn indexes
  EXPECT(talk_begin(0) == 0 && talk_begin(10) == 0 && talk_begin(-3) == 0 && talk_begin(1000) == 0, "bad npc");
  char b[48];
  EXPECT(talk_turn(0, b, 48) == 0 && b[0] == 0, "turn without chat");
  talk_begin(GQ_NPC_MIRA);
  EXPECT(talk_turn(-1, b, 48) == 0 && b[0] == 0, "turn -1");
  EXPECT(talk_turn(9, b, 48) == 0 && b[0] == 0, "turn 9");
  EXPECT(talk_turn(0, b, 0) == 0, "cap 0");
  EXPECT(talk_turn(0, b, 1) == 0 && b[0] == 0, "cap 1");
}

static void test_turn_order(void) {
  // speakers alternate from the opener; the hero opens exactly the scripts the source says
  for (int idx = 0; idx < TALK_FIRST[10]; idx++) {
    int p = TALK_SCRIPTS[idx].pack, turns = (p >> 14) + 1, first = line_of_offset(p & 0x3FFF), hero = (TALK_SCRIPTS[idx].cond & TS_HERO) ? 1 : 0;
    int npc = 1;
    while (npc < 9 && idx >= TALK_FIRST[npc]) npc++;
    if (idx >= TALK_FIRST[9]) npc = 1;
    EXPECT(talk_dbg_begin(idx, npc) == turns, "turns of script %d", idx);
    char b[48];
    for (int i = 0; i < turns; i++) {
      int h = talk_turn(i, b, 48);
      EXPECT(h == (hero ? !(i & 1) : (i & 1)), "speaker of turn %d of script %d", i, idx);
      if ((int)g_src.size() == TALK_NLINES) EXPECT(h == (int)g_src[first + i].hero, "script %d turn %d: source says hero=%d", idx, i, (int)g_src[first + i].hero);
      EXPECT(b[0] != 0, "empty turn");
    }
    EXPECT(talk_turn(turns, b, 48) == 0 && b[0] == 0, "turn after the end");
  }
}

static void test_solo_and_quest(void) {
  char b[48];
  int live = live_works();
  // bed and wake lines: always a line, varied, never the same twice in a row
  baseline();
  std::set<std::string> bed, wake;
  std::string prev;
  for (int k = 0; k < 200; k++) {
    EXPECT(talk_solo(TALK_SOLO_BED, b, 48) == 1 && b[0], "bed line");
    EXPECT(box_ok(b, "bed"), "bed text");
    EXPECT(prev != b, "bed line twice in a row");
    prev = b; bed.insert(b);
  }
  EXPECT(bed.size() >= 5, "only %d bed lines", (int)bed.size());
  prev.clear();
  for (int k = 0; k < 200; k++) {
    EXPECT(talk_solo(TALK_SOLO_WAKE, b, 48) == 1 && b[0], "wake line");
    EXPECT(box_ok(b, "wake"), "wake text");
    EXPECT(prev != b, "wake line twice in a row");
    prev = b; wake.insert(b);
  }
  EXPECT(wake.size() >= 3, "only %d wake lines", (int)wake.size());
  if (live) {   // with a live temperature the three {T} lines join in; without it none of them may appear
    gc_set_live_weather(GQ_W_CLEAR, 231, 0);
    std::set<std::string> wl;
    for (int k = 0; k < 400; k++) { talk_solo(TALK_SOLO_WAKE, b, 48); wl.insert(b); }
    EXPECT(wl.size() >= 5, "only %d wake lines with a temperature", (int)wl.size());
    int with_t = 0;
    for (auto& s : wl) if (s.find("23C") != std::string::npos) with_t++;
    EXPECT(with_t >= 2, "wake lines with the temperature: %d", with_t);
  } else {
    for (auto& s : wake) EXPECT(s.find('{') == std::string::npos && s.find("--") == std::string::npos, "temperature line without data: %s", s.c_str());
  }
  // after-action remarks: one per kind, within 30 game minutes, once
  for (int td = 1; td < TD_COUNT; td++) {
    baseline();
    EXPECT(talk_solo(TALK_SOLO_DID, b, 48) == 0 && b[0] == 0, "remark without action");
    talk_on_event(DID_EV[td], 1, 1);
    EXPECT(talk_solo(TALK_SOLO_DID, b, 48) == 1 && b[0], "no remark for kind %d", td);
    EXPECT(box_ok(b, "did"), "did text");
    EXPECT(talk_solo(TALK_SOLO_DID, b, 48) == 0, "remark twice for one action (kind %d)", td);
    talk_on_event(DID_EV[td], 1, 1);
    G.s.minute += 31;
    EXPECT(talk_solo(TALK_SOLO_DID, b, 48) == 0, "remark after 31 minutes (kind %d)", td);
  }
  EXPECT(talk_solo(99, b, 48) == 0 && b[0] == 0, "bad solo kind");
  EXPECT(talk_solo(TALK_SOLO_BED, b, 0) == 0, "cap 0");
  // quest answers: every kind has lines, and the hero's line fits the kind
  std::map<int, std::set<std::string>> per_kind;
  for (int kind = 0; kind < QK_COUNT; kind++)
    for (int k = 0; k < 100; k++) {
      baseline();
      quest_dbg_set_chat(kind, 3);
      EXPECT(talk_quest_reply(b, 48) == 1 && b[0], "no quest answer for kind %d", kind);
      EXPECT(box_ok(b, "qreply"), "qreply text");
      per_kind[kind].insert(b);
    }
  for (int kind = 0; kind < QK_COUNT; kind++) EXPECT(per_kind[kind].size() >= 1, "kind %d", kind);
  for (int a = 0; a < QK_COUNT; a++)
    for (int c = a + 1; c < QK_COUNT; c++)
      for (auto& s : per_kind[a]) EXPECT(per_kind[c].count(s) == 0, "answer shared by kinds %d and %d: %s", a, c, s.c_str());
  // a real quest chat: offer from the giver, then the hero's answer for an offer
  baseline();
  gc_debug(GQ_DBG_START_QUEST, 1);
  char q[48];
  EXPECT(quest_talk(GQ_NPC_LUMA, q, 48) && q[0], "giver speaks");
  int npc = 0;
  EXPECT(quest_chat_kind(&npc) == QK_OFFER && npc == GQ_NPC_LUMA, "kind of the first quest line");
  EXPECT(talk_quest_reply(b, 48) && per_kind[QK_OFFER].count(b), "answer to an offer: %s", b);
}

static void test_variety(void) {
  // 30 days of random situations: villagers and states change, chats are drawn the way the hero does it
  std::set<int> scripts;
  std::set<std::string> hero_lines;
  int chats = 0, fallbacks = 0, repeats = 0;
  for (int seed = 1; seed <= 3; seed++) {
    baseline();
    gc_init(GC_SCENE_VILLAGE, (uint32_t)seed * 7919u, 0);
    talk_init();
    int hist[8], hn = 0;
    uint32_t r = (uint32_t)seed * 2654435761u;
    for (int day = 0; day < 30; day++) {
      G.s.day = (uint16_t)(day + 1);
      talk_on_event(GQ_EV_NEW_DAY, day, 0);
      for (int c = 0; c < 14; c++) {
        r = r * 1664525u + 1013904223u;
        G.s.minute = (uint16_t)((6 * 60 + (r >> 8) % (16 * 60)) % 1440);
        G.s.weather = (uint8_t)((r >> 4) % GQ_W_COUNT);
        gc_set_live_weather((r >> 20) & 1 ? (int)((r >> 5) % GQ_W_COUNT) : GC_WX_NONE, (int)((r >> 9) % 400) - 100, (r >> 13) & 1 ? GC_WXF_FAHRENHEIT : 0);
        G.s.event_today = (uint8_t)((r >> 11) % 13);
        for (int i = 1; i < 10; i++) G.s.friend_[i] = (uint8_t)((r >> (i % 8)) % 101);
        if ((r >> 25) & 1) talk_on_event(DID_EV[1 + (r >> 26) % 12], 0, 0);
        G.s.q_active = (r >> 17) % 3 == 0 ? (uint8_t)((r >> 18) % 6) : 0xFF; G.s.q_step = 0;
        int npc = 1 + (int)((r >> 14) % 9);
        if (!group_has_chat_scripts(npc)) continue;
        int turns = talk_begin(npc);
        EXPECT(turns >= 2 && turns <= 4, "chat with %d turns", turns);
        char b[48];
        int idx = talk_dbg_last() & 0xFF, level = talk_dbg_last() >> 8;
        chats++;
        if (level) fallbacks++;
        for (int h = 0; h < hn; h++) if (hist[h] == idx) { repeats++; EXPECT(level > 0, "script %d repeated within 8 chats at level 0", idx); }
        if (hn < 8) hist[hn++] = idx; else { for (int i = 1; i < 8; i++) hist[i - 1] = hist[i]; hist[7] = idx; }
        scripts.insert(idx);
        for (int i = 0; i < turns; i++) {
          int hero = talk_turn(i, b, 48);
          EXPECT(b[0] && std::strlen(b) <= 39, "turn text");
          if (hero) hero_lines.insert(b);
        }
      }
    }
  }
  int complete = 1;
  for (int n = 1; n <= 9; n++) if (TALK_FIRST[n] - TALK_FIRST[n - 1] < (n <= 8 ? 20 : 6)) complete = 0;
  std::printf("variety: %d chats, %d different scripts, %d different hero lines, %d searches had to relax the repeat rules, %d repeats%s\n",
              chats, (int)scripts.size(), (int)hero_lines.size(), fallbacks, repeats, complete ? "" : " (content incomplete: relaxed thresholds)");
  if (complete) { EXPECT(scripts.size() >= 120, "only %d scripts seen", (int)scripts.size()); EXPECT(hero_lines.size() >= 100, "only %d hero lines", (int)hero_lines.size()); }
  else { EXPECT(scripts.size() >= 25, "only %d scripts seen", (int)scripts.size()); EXPECT(hero_lines.size() >= 25, "only %d hero lines", (int)hero_lines.size()); }
}

// The running game: 3 seeds x 30 days; every chat the hero starts has 2 to 4 turns that fit the box.
static void test_game(int seed) {
  sim_start((uint32_t)seed);
  int last = talk_dbg_begins(), chats = 0, bad = 0, hero_turns = 0;
  int hist[8], hn = 0, repeats = 0;
  std::set<int> distinct;
  sim_seconds_cb(30L * 86400L, [&]() {
    int now = talk_dbg_begins();
    if (now == last) return;
    last = now;
    chats++;
    int idx = talk_dbg_last() & 0xFF, level = talk_dbg_last() >> 8;
    int n = 0;
    char b[48];
    for (int i = 0; i < 6; i++) {
      int h = talk_turn(i, b, 48);
      if (!b[0]) break;
      n++;
      hero_turns += h;
      if (!box_ok(b, "game chat")) bad++;
    }
    if (n < 2 || n > 4) { bad++; std::printf("  chat with %d turns\n", n); }
    for (int h = 0; h < hn; h++) if (hist[h] == idx && level == 0) repeats++;
    if (hn < 8) hist[hn++] = idx; else { for (int i = 1; i < 8; i++) hist[i - 1] = hist[i]; hist[7] = idx; }
    distinct.insert(idx);
  });
  EXPECT(bad == 0, "seed %d: %d bad turns", seed, bad);
  EXPECT(repeats == 0, "seed %d: %d repeats within 8 chats", seed, repeats);
  std::printf("game seed %d: %d scripted chats in 30 days, %d different scripts, %d hero turns\n", seed, chats, (int)distinct.size(), hero_turns);
  EXPECT(chats >= 20, "seed %d: only %d scripted chats in 30 days", seed, chats);
}

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  // `test_talk N` runs only the 30-day game of seed N (so the three can run in parallel); `test_talk q` runs everything but the games
  int quick = argc > 1 && argv[1][0] == 'q';
  int only_game = argc > 1 && !quick ? std::atoi(argv[1]) : 0;
  if (only_game) test_game(only_game);
  else {
    test_tables();
    test_sources_and_decoder();
    test_turn_order();
    test_reachability();
    test_selection();
    test_solo_and_quest();
    test_variety();
    if (!quick) for (int seed = 1; seed <= 3; seed++) test_game(seed);
  }
  if (g_fail) { std::printf("test_talk: %d FAILURES\n", g_fail); return 1; }
  std::printf("test_talk OK\n");
  return 0;
}
