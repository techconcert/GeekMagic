// Save format helpers: CRC32, validate, sanitize, defaults (design 10.3, 8.10 rule 4).
// Pure functions of their arguments (plus the econ/quest tables for the item and quest limits).
#include "world_priv.h"

uint32_t gq_crc32(const uint8_t* p, int n) {
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

static uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// Checks length, magic, version, size field and CRC. No side effects.
int gq_save_validate(const uint8_t* buf, int len) {
  if (!buf || len != GQ_SAVE_SIZE) return GC_SAVE_ERR_LEN;
  if (rd32(buf) != GQ_SAVE_MAGIC) return GC_SAVE_ERR_MAGIC;
  if (rd16(buf + 4) != GQ_SAVE_VERSION) return GC_SAVE_ERR_VERSION;
  if (rd16(buf + 6) != GQ_SAVE_SIZE) return GC_SAVE_ERR_LEN;
  if (rd32(buf + 8) != gq_crc32(buf + 12, GQ_SAVE_SIZE - 12)) return GC_SAVE_ERR_CRC;
  return GC_SAVE_OK;
}

static uint8_t clamp8(uint8_t v, uint8_t hi) { return v > hi ? hi : v; }

// One clamp per row of the table in design 10.3. Total: any input leaves a valid state.
void gq_save_sanitize(GqSave* s) {
  s->magic = GQ_SAVE_MAGIC;
  s->version = GQ_SAVE_VERSION;
  s->size = GQ_SAVE_SIZE;
  if (s->rng == 0) s->rng = 0x1234567u;
  if (s->minute > 1439) s->minute = 1439;
  for (int i = 0; i < GQ_TOOL_COUNT; i++) s->tool[i] = clamp8(s->tool[i], 3);
  s->tool[GQ_TOOL_PURSE] = clamp8(s->tool[GQ_TOOL_PURSE], 3);
  static const uint16_t CAP[4] GC_ROM_ATTR = {99, 250, 500, 999};
  uint16_t cap = GC_RD16(&CAP[s->tool[GQ_TOOL_PURSE]]);
  if (s->coins > cap) s->coins = cap;
  if (s->stars > 999) s->stars = 999;
  s->projects &= 0x03FF;
  if (s->fund > 99) s->fund = 99;
  if (s->weather >= GQ_W_COUNT) s->weather = GQ_W_CLEAR;
  s->weather_hours = clamp8(s->weather_hours, 8);
  s->festival_flags &= 0x0F;
  s->event_today = clamp8(s->event_today, 12);
  for (int i = 0; i < GQ_ITEM_COUNT; i++) s->inv[i] = clamp8(s->inv[i], (uint8_t)econ_item_cap(i));
  s->till_left = clamp8(s->till_left, 160);
  s->errands_today = clamp8(s->errands_today, 3);
  s->meals_today = clamp8(s->meals_today, 3);
  s->gifts_today = clamp8(s->gifts_today, 1);
  s->goal = clamp8(s->goal, 22);
  s->chapter = clamp8(s->chapter, 6);
  if (s->q_active != 0xFF && s->q_active >= quest_count()) s->q_active = 0xFF;
  if (s->q_active == 0xFF || s->q_step >= quest_steps(s->q_active)) s->q_step = 0;
  s->q_count = clamp8(s->q_count, 99);
  s->hero_hearts = s->hero_hearts < 1 ? 1 : clamp8(s->hero_hearts, 6);
  s->hero_area = GQ_AREA_VILLAGE;
  for (int i = 0; i < 12; i++) s->friend_[i] = clamp8(s->friend_[i], 100);
  s->chat_today &= 0x03FF;
  for (int i = 0; i < 38; i++) s->reserved[i] = 0;
}
