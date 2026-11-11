#include "slotlog.h"
#include "gq_crc32.h"

static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint32_t rd16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static void wr32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

int sl_classify(const uint8_t* slot, uint32_t* seq) {
  int blank = 1;
  for (uint32_t i = 0; i < SL_SLOT_BYTES; i++) if (slot[i] != 0xFF) { blank = 0; break; }
  if (blank) return SL_BLANK;
  if (rd32(slot) != SL_MAGIC || rd16(slot + 8) != SL_FMT || rd16(slot + 10) != SL_PAYLOAD_BYTES) return SL_JUNK;
  if (rd32(slot + 4) != gq_crc32(slot + 8, SL_SLOT_BYTES - 8)) return SL_JUNK;
  if (seq) *seq = rd32(slot + 12);
  return SL_VALID;
}

void sl_seal(uint8_t* slot, uint32_t seq) {
  wr32(slot, SL_MAGIC);
  slot[8] = (uint8_t)SL_FMT; slot[9] = (uint8_t)(SL_FMT >> 8);
  slot[10] = (uint8_t)SL_PAYLOAD_BYTES; slot[11] = (uint8_t)(SL_PAYLOAD_BYTES >> 8);
  wr32(slot + 12, seq);
  wr32(slot + 4, gq_crc32(slot + 8, SL_SLOT_BYTES - 8));
}

void sl_build(uint8_t* slot, uint32_t seq, const uint8_t* payload) {
  for (uint32_t i = 0; i < SL_PAYLOAD_BYTES; i++) slot[16 + i] = payload[i];
  sl_seal(slot, seq);
}

void sl_plan(const SlScan* s, SlPlan* p) {
  int newest = -1;
  for (uint32_t i = 0; i < SL_SLOTS; i++)
    if (s->state[i] == SL_VALID && (newest < 0 || s->seq[i] > s->seq[newest])) newest = (int)i;
  p->load = newest;
  p->erase_sector = -1;
  p->next_seq = newest < 0 ? 1u : s->seq[newest] + 1u;
  uint32_t start = newest < 0 ? 0u : (uint32_t)newest + 1u;
  for (uint32_t k = 0; k < SL_SLOTS; k++) {
    uint32_t i = (start + k) % SL_SLOTS;
    if (s->state[i] == SL_BLANK) { p->write_slot = (int)i; return; }
  }
  // no blank slot: erase the sector after the one holding the newest record (or sector 0 when nothing is valid)
  uint32_t sec = newest < 0 ? 0u : ((uint32_t)newest / SL_SLOTS_PER_SECTOR + 1u) % SL_SECTORS;
  p->erase_sector = (int)sec;
  p->write_slot = (int)(sec * SL_SLOTS_PER_SECTOR);
}

void sl_applied(SlScan* s, const SlPlan* p, int ok) {
  if (p->erase_sector >= 0)
    for (uint32_t i = 0; i < SL_SLOTS_PER_SECTOR; i++) s->state[(uint32_t)p->erase_sector * SL_SLOTS_PER_SECTOR + i] = SL_BLANK;
  const uint32_t w = (uint32_t)p->write_slot;
  if (ok) { s->state[w] = SL_VALID; s->seq[w] = p->next_seq; }
  else s->state[w] = SL_JUNK;
}
