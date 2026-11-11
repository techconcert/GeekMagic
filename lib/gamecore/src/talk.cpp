// Dialogue engine (docs/DESIGN-R3.md 4, chunk D1).
//
// A script is a small conversation of 2 to 4 turns written as a unit (content/talk/*.txt, compiled by tools/gen_talk.py into
// talk_data.h). Each script carries one condition; talk_begin picks among the scripts of the villager and of the shared pool whose
// condition is true, weighted by how specific the condition is, never one of the last 8 used and never one already used with the
// same villager today. Turns are stored in a compact dictionary encoding and expanded (slots filled in) one turn at a time.
// The same tables hold the hero's solo remarks (bed, wake, after an action) and his answers after a quest line.
// No heap, no libc, bounded loops, every table read through GC_RD8 / GC_RD16.
#include "world_priv.h"
#define TALK_DATA_IMPL
#include "talk_data.h"

// quest.cpp (same chunk): what the quest system looks like to a chat
int quest_chat_state(int npc);        // 0 a quest of this villager is active, 1 a story quest is active, 2 none, 3 another quest
int quest_chat_kind(int* npc);        // QK_* of the last quest line the villager spoke, and to whom

enum { MAX_RANGE = 48, RING = 8, TODAY = 12, DID_MINUTES = 30 };

static uint8_t  s_ring[RING];            // script index + 1 of the last scripts used, 0 = empty
static uint8_t  s_ring_pos;
static uint8_t  s_today_s[TODAY];        // script index + 1 used today ...
static uint8_t  s_today_n[TODAY];        // ... with this villager
static uint8_t  s_today_cnt;
static uint16_t s_first;                 // first line id of the running script
static uint8_t  s_turns, s_hero_opens, s_npc;
static uint8_t  s_did[2], s_did_said[2];
static uint16_t s_did_t[2];              // game minute (mod 65536) when the hero did it
static uint8_t  s_solo_last[3];          // last script index + 1 per solo kind
static uint8_t  s_qreply_last;
static uint8_t  s_last_idx, s_last_level;   // what talk_begin chose last and how hard it had to look (tests)
static uint16_t s_begins;                    // chats started (tests)
static int16_t  s_force = -1;                // tests and preview: always play this script (talk_dbg_force)

void talk_init(void) {
  for (int i = 0; i < RING; i++) s_ring[i] = 0;
  for (int i = 0; i < TODAY; i++) s_today_s[i] = s_today_n[i] = 0;
  s_ring_pos = s_today_cnt = 0;
  s_first = 0; s_turns = s_hero_opens = 0; s_npc = 0;
  s_did[0] = s_did[1] = s_did_said[0] = s_did_said[1] = 0;
  s_did_t[0] = s_did_t[1] = 0;
  s_solo_last[0] = s_solo_last[1] = s_solo_last[2] = 0;
  s_qreply_last = 0; s_last_idx = s_last_level = 0; s_begins = 0;
}

// ------------------------------------------------------------------------------------ what the hero just did
static uint16_t now_minute(void) { return (uint16_t)((uint32_t)G.s.day * 1440u + G.s.minute); }

static void did_remember(int td) {
  if (s_did[0] != td) {
    s_did[1] = s_did[0]; s_did_said[1] = s_did_said[0]; s_did_t[1] = s_did_t[0];
    s_did[0] = (uint8_t)td;
  }
  s_did_said[0] = 0;
  s_did_t[0] = now_minute();
}

void talk_on_event(int ev, int a, int b) {
  (void)a; (void)b;
  int td = 0;
  switch (ev) {
    case GQ_EV_NEW_DAY: s_today_cnt = 0; return;
    case GQ_EV_CHOPPED:   td = TD_CHOPPED; break;
    case GQ_EV_CAUGHT:    td = TD_FISHED; break;
    case GQ_EV_FORAGED: case GQ_EV_HARVESTED: td = TD_FORAGED; break;
    case GQ_EV_MINED:     td = TD_MINED; break;
    case GQ_EV_SLAIN:     td = TD_FOUGHT; break;
    case GQ_EV_FAINTED:   td = TD_FAINTED; break;
    case GQ_EV_SOLD:      td = TD_SOLD; break;
    case GQ_EV_BOUGHT:    td = TD_BOUGHT; break;
    case GQ_EV_BUILT:     td = TD_BUILT; break;
    case GQ_EV_ATE:       td = TD_ATE; break;
    case GQ_EV_GIFTED:    td = TD_GIFTED; break;
    case GQ_EV_WOKE:      td = TD_SLEPT; break;
    default: return;
  }
  did_remember(td);
}

static int did_slot(int td) {   // slot of a remark the hero made in the last 30 game minutes, or -1
  for (int k = 0; k < 2; k++)
    if (s_did[k] == td && td != 0 && (uint16_t)(now_minute() - s_did_t[k]) <= DID_MINUTES) return k;
  return -1;
}

// -------------------------------------------------------------------------------------------- conditions
static int cond_ok(int cond, int arg, int npc) {
  int c;
  switch (cond) {
    case TC_ANY: return 1;
    case TC_TIME: {
      int h = (G.s.minute / 60) % 24;
      int blk = (h >= 6 && h < 10) ? 0 : (h >= 10 && h < 15) ? 1 : (h >= 15 && h < 22) ? 2 : 3;
      return blk == arg;
    }
    case TC_WX: return cal_weather_shown() == arg;
    case TC_TEMP: if (!cal_temp_c(&c)) return 0; return arg == 0 ? c <= 5 : c >= 28;
    case TC_LIVE: return cal_temp_c(&c);
    case TC_SEASON: return cal_season() == arg;
    case TC_FEST:
      if (arg == 1) return cal_festival() >= 0;
      return G.s.day % 7 == 6 && cal_festival() < 0 && G.s.minute < 16 * 60;
    case TC_DID: return did_slot(arg) >= 0;
    case TC_QUEST: return quest_chat_state(npc) == arg;
    case TC_CHAPTER: return arg >= 5 ? G.s.chapter >= 5 : G.s.chapter == arg;
    case TC_FRIEND: { int t = econ_friend(npc) / 25; if (t > 3) t = 3; return t == arg; }
    case TC_GOAL: { GqGoal g; return econ_goal(&g) && g.is_project == arg; }
    case TC_EVENT: return cal_event() == arg;
    default: return 0;
  }
}

static int weight_of(int cond) { return cond == TC_ANY ? 1 : (cond == TC_TIME || cond == TC_SEASON) ? 2 : 4; }

static int sc_cond(int i) { return GC_RD8(&TALK_SCRIPTS[i].cond) & ~TS_HERO; }
static int sc_arg(int i) { return GC_RD8(&TALK_SCRIPTS[i].arg); }
static int sc_pack(int i) { return GC_RD16(&TALK_SCRIPTS[i].pack); }

static int in_ring(int idx) { for (int i = 0; i < RING; i++) if (s_ring[i] == idx + 1) return 1; return 0; }
static int used_today(int idx, int npc) {
  for (int i = 0; i < TODAY && i < s_today_cnt; i++) if (s_today_s[i] == idx + 1 && s_today_n[i] == npc) return 1;
  return 0;
}

// Picks a script for villager npc (1..9) and starts it. Returns its index or -1.
static int choose_chat(int npc) {
  int r0 = GC_RD8(&TALK_FIRST[npc - 1]), r1 = GC_RD8(&TALK_FIRST[npc]);
  int s0 = 0, s1 = 0;
  if (npc != GQ_NPC_MISO) { s0 = GC_RD8(&TALK_FIRST[9]); s1 = GC_RD8(&TALK_FIRST[10]); }   // the cat does not use the shared pool
  for (int level = 0; level < 3; level++) {
    uint8_t cand[MAX_RANGE];
    uint8_t w[MAX_RANGE];
    int n = 0, total = 0;
    for (int part = 0; part < 2; part++) {
      int lo = part ? s0 : r0, hi = part ? s1 : r1;
      for (int i = lo; i < hi && n < MAX_RANGE; i++) {
        int cd = sc_cond(i);
        if (!cond_ok(cd, sc_arg(i), npc)) continue;
        if (level < 2 && used_today(i, npc)) continue;
        if (level < 1 && in_ring(i)) continue;
        cand[n] = (uint8_t)i; w[n] = (uint8_t)weight_of(cd); total += w[n]; n++;
      }
    }
    if (total <= 0) continue;
    s_last_level = (uint8_t)level;
    int r = (int)(gc_rnd() % (uint32_t)total);
    for (int k = 0; k < n; k++) {
      if (r < w[k]) return cand[k];
      r -= w[k];
    }
    return cand[n - 1];
  }
  return -1;
}

static void start_script(int idx) {
  int p = sc_pack(idx);
  s_first = (uint16_t)(p & 0x3FFF);
  s_turns = (uint8_t)((p >> 14) + 1);
  s_hero_opens = (GC_RD8(&TALK_SCRIPTS[idx].cond) & TS_HERO) ? 1 : 0;
}

int talk_begin(int npc) {
  s_turns = 0;
  if (npc < GQ_NPC_MIRA || npc > GQ_NPC_MISO) return 0;
  s_npc = (uint8_t)npc;
  int idx = (s_force >= 0 && s_force < TALK_NSCRIPTS) ? s_force : choose_chat(npc);
  if (idx < 0) return 0;
  start_script(idx);
  s_last_idx = (uint8_t)idx; s_begins++;
  s_ring[s_ring_pos] = (uint8_t)(idx + 1);
  s_ring_pos = (uint8_t)((s_ring_pos + 1) % RING);
  if (s_today_cnt >= TODAY) {   // forget the oldest
    for (int i = 1; i < TODAY; i++) { s_today_s[i - 1] = s_today_s[i]; s_today_n[i - 1] = s_today_n[i]; }
    s_today_cnt = TODAY - 1;
  }
  s_today_s[s_today_cnt] = (uint8_t)(idx + 1); s_today_n[s_today_cnt] = (uint8_t)npc; s_today_cnt++;
  if (s_turns < 2) s_turns = 0;   // chat scripts always have 2..4 turns; anything else is a data error
  return s_turns;
}

// ------------------------------------------------------------------------------------------------ text
static const char WORD_WX[GQ_W_COUNT][7] GC_ROM_ATTR = {"SUN", "CLOUDS", "RAIN", "STORM", "FOG", "SNOW", "WIND"};
static const char WORD_SEASON[4][7] GC_ROM_ATTR = {"SPRING", "SUMMER", "AUTUMN", "WINTER"};
static const char WORD_ITEM[13][9] GC_ROM_ATTR = {"WOOD", "STONE", "FISH", "MUSHROOM", "BERRY", "FLOWER", "SHELL", "ORE", "GEM",
                                                  "CROP", "SEED", "HERB", "GEL"};

static int put(char* buf, int n, int cap, char c) { if (n < cap - 1) buf[n++] = c; return n; }
static int put_rom(char* buf, int n, int cap, const char* rom, int maxlen) {
  for (int i = 0; i < maxlen; i++) {
    char c = (char)GC_RD8(&rom[i]);
    if (!c) break;
    n = put(buf, n, cap, c);
  }
  return n;
}
static int put_ram(char* buf, int n, int cap, const char* s) { for (int i = 0; i < 12 && s[i]; i++) n = put(buf, n, cap, s[i]); return n; }

static int put_slot(int code, char* buf, int n, int cap, int npc) {
  char tmp[12];
  switch (code) {
    case 2: {   // {T}: live temperature with unit, 4 characters at most
      int v, f;
      if (!cal_temp(&v, &f)) return put_ram(buf, n, cap, "--");
      if (v < -99) v = -99;
      if (v > 199) v = 199;
      if (v < 0) { n = put(buf, n, cap, '-'); v = -v; }
      itoa_into(tmp, v);
      n = put_ram(buf, n, cap, tmp);
      return put(buf, n, cap, f ? 'F' : 'C');
    }
    case 3: { int w = cal_weather_shown(); if (w < 0 || w >= GQ_W_COUNT) w = 0; return put_rom(buf, n, cap, WORD_WX[w], 7); }
    case 4: return put_rom(buf, n, cap, WORD_SEASON[cal_season() & 3], 7);
    case 5: {
      GqGoal g;
      if (!econ_goal(&g)) return put_ram(buf, n, cap, "FUND");
      econ_goal_name(g.index, tmp, 12);
      return put_ram(buf, n, cap, tmp);
    }
    case 6: { int it = econ_focus_item(); if (it < 0 || it >= 13) it = 0; return put_rom(buf, n, cap, WORD_ITEM[it], 9); }
    default: return put_ram(buf, n, cap, gq_npc_name(npc));
  }
}

// Expands the turn that starts at blob offset o into buf (at most cap-1 characters, '|' between the two lines of the box).
static int decode_at(int o, char* buf, int cap, int npc) {
  if (cap < 1) return 0;
  int n = 0;
  for (int g = 0; g < 96 && o + g < TALK_BLOB_BYTES; g++) {
    int b = GC_RD8(&TALK_BLOB[o + g]);
    if (!b) break;
    if (b >= 0x60) {
      int d = b - 0x60;
      if (d >= TALK_DICT_N) continue;
      int e = GC_RD16(&TALK_DOFF[d + 1]);
      for (int k = GC_RD16(&TALK_DOFF[d]); k < e; k++) n = put(buf, n, cap, (char)GC_RD8(&TALK_DICT[k]));
    } else if (b == 1) n = put(buf, n, cap, '|');
    else if (b < 8) n = put_slot(b, buf, n, cap, npc);
    else n = put(buf, n, cap, (char)b);
  }
  buf[n] = 0;
  return n;
}

// Offset of the i-th turn after the one at offset o (turns are consecutive and end with a 0 byte).
static int skip_turns(int o, int i) {
  for (; i > 0; i--) {
    int g = 0;
    while (g < 96 && o < TALK_BLOB_BYTES && GC_RD8(&TALK_BLOB[o])) { o++; g++; }
    o++;
  }
  return o;
}

int talk_turn(int i, char* buf, int cap) {
  if (cap < 1) return 0;
  buf[0] = 0;
  if (i < 0 || i >= s_turns) return 0;
  decode_at(skip_turns(s_first, i), buf, cap, s_npc);
  return s_hero_opens ? !(i & 1) : (i & 1);
}

// -------------------------------------------------------------------------------- hero alone, quest answers
// Does script i of a solo group answer this request? (BED, WAKE with or without a temperature, a kind of DID, a kind of QREPLY)
static int solo_match(int i, int cond, int arg) {
  if (sc_cond(i) != cond) return 0;
  int a = sc_arg(i);
  if (cond == TC_QREPLY || cond == TC_SOLODID) return a == arg;
  if (cond == TC_WAKE && a == 1) { int c; return cal_temp_c(&c); }
  return 1;
}

// Picks one of the scripts of group g (10 solo, 11 quest answers) that match; avoids the last one used. Returns its index or -1.
static int solo_pick(int group, int cond, int arg, uint8_t* last) {
  int lo = GC_RD8(&TALK_FIRST[group]), hi = GC_RD8(&TALK_FIRST[group + 1]);
  for (int pass = 0; pass < 2; pass++) {
    int n = 0;
    for (int i = lo; i < hi; i++) if (solo_match(i, cond, arg) && (pass || *last != i + 1)) n++;
    if (n <= 0) continue;
    int r = (int)(gc_rnd() % (uint32_t)n);
    for (int i = lo; i < hi; i++) {
      if (!solo_match(i, cond, arg) || (!pass && *last == i + 1)) continue;
      if (r-- == 0) { *last = (uint8_t)(i + 1); return i; }
    }
  }
  return -1;
}

static int say_script(int idx, char* buf, int cap, int npc) {
  if (cap < 1) return 0;
  buf[0] = 0;
  if (idx < 0) return 0;
  decode_at(sc_pack(idx) & 0x3FFF, buf, cap, npc);
  return buf[0] != 0;
}

int talk_quest_reply(char* buf, int cap) {
  int npc = 0;
  int kind = quest_chat_kind(&npc);
  return say_script(solo_pick(11, TC_QREPLY, kind, &s_qreply_last), buf, cap, npc);
}

int talk_solo(int kind, char* buf, int cap) {
  if (cap < 1) return 0;
  buf[0] = 0;
  switch (kind) {
    case TALK_SOLO_BED: return say_script(solo_pick(10, TC_BED, 0, &s_solo_last[0]), buf, cap, 0);
    case TALK_SOLO_WAKE: return say_script(solo_pick(10, TC_WAKE, 0, &s_solo_last[1]), buf, cap, 0);
    case TALK_SOLO_DID: {
      int k = did_slot(s_did[0]);
      if (k < 0 || s_did_said[0]) { k = did_slot(s_did[1]); if (k < 0 || s_did_said[1]) return 0; }
      s_did_said[k] = 1;
      return say_script(solo_pick(10, TC_SOLODID, s_did[k], &s_solo_last[2]), buf, cap, 0);
    }
    default: return 0;
  }
}

// ------------------------------------------------------------------------------- hooks for the host tests
// Not called by the game; the linker drops them from the firmware (sections are garbage-collected).
int talk_dbg_begin(int script, int npc) {
  if (script < 0 || script >= TALK_NSCRIPTS) return 0;
  s_npc = (uint8_t)npc;
  start_script(script);
  return s_turns;
}
int talk_dbg_last(void) { return s_last_idx | (s_last_level << 8); }   // script index, level of the search in bits 8..
void talk_dbg_force(int script) { s_force = (int16_t)script; }   // -1 = normal selection
int talk_dbg_begins(void) { return s_begins; }
int talk_dbg_line(int line, char* buf, int cap, int npc) { return line < 0 || line >= TALK_NLINES ? (buf[0] = 0) : decode_at(skip_turns(0, line), buf, cap, npc); }   // line = turn number in file order
