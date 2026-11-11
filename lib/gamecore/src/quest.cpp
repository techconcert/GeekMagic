// Quests and text: working stubs (chunk 5 replaces the bodies, signatures stay).
// Chunk 0 only adds the paid-errand rule: a finished errand (QUEST_DONE with a == 0xFF)
// pays 6..14 coins, at most 3 times a day.
#include "world_priv.h"

static char s_name[8];

void quest_init(void) {}
int  quest_count(void) { return 0; }
int  quest_steps(int quest) { (void)quest; return 0; }

void quest_on_event(int ev, int a, int b) {
  (void)b;
  if (ev == GQ_EV_QUEST_DONE && a == 0xFF) {
    if (G.s.errands_today < 3) {
      G.s.errands_today++;
      if (G.s.stat[GQ_STAT_ERRANDS] < 65535) G.s.stat[GQ_STAT_ERRANDS]++;
      econ_earn(6 + (int)(gc_rnd() % 9), GQ_SRC_QUEST);
      gq_touch();
    }
  }
}

int  quest_need(GqNeed* out) { if (out) *out = GqNeed(); return 0; }
int  quest_talk(int npc, char* buf, int cap) { (void)npc; if (cap > 0) buf[0] = 0; return 0; }
void quest_ambient_line(int npc, char* buf, int cap) { (void)npc; if (cap > 0) buf[0] = 0; }
void quest_reply_line(char* buf, int cap) { if (cap > 0) buf[0] = 0; }
int  quest_band(char* buf, int cap, int* num, int* den) { if (cap > 0) buf[0] = 0; if (num) *num = 0; if (den) *den = 0; return 0; }
int  quest_title(char* buf, int cap) { if (cap > 0) buf[0] = 0; return 0; }
const char* gq_text(int id, char* buf, int cap) { (void)id; if (cap > 0) buf[0] = 0; return buf; }

const char* gq_npc_name(int npc) {
  static const char* const NAMES[GQ_NPC_COUNT] = {"HERO", "MIRA", "BRAM", "PIP", "ODDA", "FINN", "TESS", "LUMA", "WREN", "MISO"};
  str_copy(s_name, npc >= 0 && npc < GQ_NPC_COUNT ? NAMES[npc] : "", 8);
  return s_name;
}
