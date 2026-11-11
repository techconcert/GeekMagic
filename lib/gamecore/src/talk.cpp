// Dialogue engine, R3-0C stub (docs/DESIGN-R3.md 4.4). Behaves like the game before R3: two turns made of one ordinary
// villager line and one hero reply. Chunk D1 replaces this file with the script engine; the signatures stay.
#include "world_priv.h"

static char s_line[2][40];

void talk_init(void) { s_line[0][0] = s_line[1][0] = 0; }
void talk_on_event(int ev, int a, int b) { (void)ev; (void)a; (void)b; }

int talk_begin(int npc) {
  quest_ambient_line(npc, s_line[0], 40);
  quest_reply_line(s_line[1], 40);
  return 2;
}

int talk_turn(int i, char* buf, int cap) {
  if (cap < 1) return 0;
  i = i == 0 ? 0 : 1;
  str_copy(buf, s_line[i], cap);
  return i;
}

int talk_quest_reply(char* buf, int cap) {
  if (cap < 1) return 0;
  quest_reply_line(buf, cap);
  return buf[0] != 0;
}

int talk_solo(int kind, char* buf, int cap) {
  (void)kind;
  if (cap > 0) buf[0] = 0;
  return 0;
}
