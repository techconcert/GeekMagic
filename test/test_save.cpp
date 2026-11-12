// Save format: round trip, rejection of every damaged input, total import (design 10.2, 10.3, 8.10).
#include "gq_test.h"
#include "../lib/gamecore/src/gq_mod.h"
#define gc_save_check(b, l) gq_save_validate((b), (l))   // the core exports only what the firmware needs

static uint32_t rng_state = 0x9E3779B9u;
static uint32_t rnd32() {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
  return rng_state;
}

// keys that describe the transient picture, not the saved game
static bool transient_key(int k) {
  return k == GQ_Q_HERO_PRESENCE || k == GQ_Q_HERO_AREA || k == GQ_Q_HERO_ACT || k == GQ_Q_HERO_TILE || k == GQ_Q_ACTORS_VISIBLE ||
         k == GQ_Q_CARD;
}

static int all_keys(int* keys) {
  int n = 0;
  for (int k = 0; k <= GQ_Q_ACTORS_VISIBLE; k++) keys[n++] = k;
  for (int i = 0; i < GQ_ITEM_COUNT; i++) keys[n++] = GQ_Q_ITEM_BASE + i;
  for (int i = 0; i < GQ_TOOL_COUNT; i++) keys[n++] = GQ_Q_TOOL_BASE + i;
  for (int i = 0; i < GQ_STAT_COUNT; i++) keys[n++] = GQ_Q_STAT_BASE + i;
  for (int i = 0; i < GQ_NPC_COUNT; i++) keys[n++] = GQ_Q_FRIEND_BASE + i;
  return n;
}

static bool in_range(int key, int32_t v, int32_t cap) {
  if (key >= GQ_Q_ITEM_BASE && key < GQ_Q_ITEM_BASE + GQ_ITEM_COUNT) return v >= 0 && v <= 30;
  if (key >= GQ_Q_TOOL_BASE && key < GQ_Q_TOOL_BASE + GQ_TOOL_COUNT) return v >= 0 && v <= 3;
  if (key >= GQ_Q_FRIEND_BASE && key < GQ_Q_FRIEND_BASE + GQ_NPC_COUNT) return v >= 0 && v <= 100;
  if (key >= GQ_Q_STAT_BASE && key < GQ_Q_STAT_BASE + GQ_STAT_COUNT) return v >= 0 && v <= 65535;
  switch (key) {
    case GQ_Q_COINS: return v >= 0 && v <= cap;
    case GQ_Q_COIN_CAP: return v == 99 || v == 250 || v == 500 || v == 999;
    case GQ_Q_STARS: return v >= 0 && v <= 999;
    case GQ_Q_FUND: return v >= 0 && v <= 99;
    case GQ_Q_MINUTE: return v >= 0 && v <= 1439;
    case GQ_Q_SEASON: return v >= 0 && v <= 3;
    case GQ_Q_WEATHER: return v >= 0 && v < GQ_W_COUNT;
    case GQ_Q_FESTIVAL: return v >= -1 && v <= 3;
    case GQ_Q_EVENT: return v >= 0 && v <= 12;
    case GQ_Q_HERO_PRESENCE: return v >= 1 && v <= 4;
    case GQ_Q_HERO_AREA: return v >= 0 && v <= GQ_AREA_DELVE;   // R8: 8 = a generated deep room
    case GQ_Q_HERO_ACT: return v >= 0 && v < GQ_ACT_COUNT;
    case GQ_Q_HERO_HEARTS: return v >= 1 && v <= 6;
    case GQ_Q_HERO_TILE: return v >= 0 && v < GC_COLS * GC_ROWS;
    case GQ_Q_GOAL: return v >= 0 && v <= 22;
    case GQ_Q_GOAL_PRICE: return v >= 0 && v <= 999;
    case GQ_Q_PROJECTS: return v >= 0 && v < 1024;
    case GQ_Q_CHAPTER: return v >= 0 && v <= 6;
    case GQ_Q_QUEST: return v == 255 || (v >= 0 && v < quest_count());
    case GQ_Q_QUEST_COUNT: return v >= 0 && v <= 99;
    case GQ_Q_TILL_LEFT: return v >= 0 && v <= 160;
    case GQ_Q_ERRANDS_TODAY: return v >= 0 && v <= 3;
    case GQ_Q_ACH_COUNT: return v >= 0 && v <= 32;
    case GQ_Q_SEEN_COUNT: return v >= 0 && v <= 64;
    case GQ_Q_ACTORS_VISIBLE: return v >= 0 && v <= 10;
    default: return v >= 0;
  }
}

// exports differ only in seq (bytes 12..15) and crc (8..11) when the game is the same
static bool same_game(const uint8_t* a, const uint8_t* b) {
  for (int i = 0; i < GC_SAVE_BYTES; i++) {
    if ((i >= 8 && i < 16)) continue;
    if (a[i] != b[i]) { std::printf("export differs at byte %d: %02x vs %02x\n", i, a[i], b[i]); return false; }
  }
  return true;
}

static void test_round_trip() {
  int keys[256];
  int nk = all_keys(keys);
  for (uint32_t seed = 1; seed <= 3; seed++) {
    sim_start(seed);
    sim_minutes(60 * 20);             // most of a day: sells, buys, errands happen
    gc_debug(GQ_DBG_ADD_COINS, 500);  // reach the stars too
    uint8_t buf[GC_SAVE_BYTES], buf2[GC_SAVE_BYTES];
    CHECK(gc_save_export(buf, GC_SAVE_BYTES) == GC_SAVE_BYTES);
    CHECK(gc_save_check(buf, GC_SAVE_BYTES) == GC_SAVE_OK);
    int32_t before[256];
    for (int i = 0; i < nk; i++) before[i] = gc_query(keys[i]);
    CHECK(before[GQ_Q_COINS] > 0);

    sim_start(seed + 100, before[GQ_Q_MINUTE]);   // a different game on the same wall clock, then restore
    CHECK(gc_save_import(buf, GC_SAVE_BYTES) == 1);
    for (int i = 0; i < nk; i++) {
      if (transient_key(keys[i])) continue;
      if (gc_query(keys[i]) != before[i]) {
        std::printf("key %d: %d != %d after import\n", keys[i], (int)gc_query(keys[i]), (int)before[i]);
        std::exit(1);
      }
    }
    CHECK(gc_query(GQ_Q_HERO_PRESENCE) == GQ_PRES_SPRITE);
    CHECK(gc_save_export(buf2, GC_SAVE_BYTES) == GC_SAVE_BYTES);
    CHECK(same_game(buf, buf2));
    CHECK(gc_save_serial() == gc_save_serial());
    // the restored game keeps running
    sim_minutes(30);
  }
  // export needs room
  uint8_t small[10];
  CHECK(gc_save_export(small, 10) == 0);
  std::printf("round trip ok\n");
}

static void test_rejection() {
  sim_start(7);
  sim_minutes(120);
  uint8_t good[GC_SAVE_BYTES];
  CHECK(gc_save_export(good, GC_SAVE_BYTES) == GC_SAVE_BYTES);
  uint8_t ref[GC_SAVE_BYTES], now[GC_SAVE_BYTES];
  CHECK(gc_save_export(ref, GC_SAVE_BYTES) == GC_SAVE_BYTES);

  // every single-bit flip is rejected (1,536 of them), by check and by import
  int flips = 0;
  for (int bit = 0; bit < GC_SAVE_BYTES * 8; bit++) {
    uint8_t b[GC_SAVE_BYTES];
    std::memcpy(b, good, sizeof b);
    b[bit >> 3] ^= (uint8_t)(1u << (bit & 7));
    CHECK(gc_save_check(b, GC_SAVE_BYTES) != GC_SAVE_OK);
    CHECK(gc_save_import(b, GC_SAVE_BYTES) == 0);
    flips++;
  }
  CHECK(flips == 1536);

  // right codes for wrong length, magic, version
  CHECK(gc_save_check(good, GC_SAVE_BYTES - 1) == GC_SAVE_ERR_LEN);
  CHECK(gc_save_check(good, GC_SAVE_BYTES + 1) == GC_SAVE_ERR_LEN);
  CHECK(gc_save_check(good, 0) == GC_SAVE_ERR_LEN);
  CHECK(gc_save_check(0, GC_SAVE_BYTES) == GC_SAVE_ERR_LEN);
  uint8_t b[GC_SAVE_BYTES];
  std::memcpy(b, good, sizeof b); b[0] ^= 0xFF;
  CHECK(gc_save_check(b, GC_SAVE_BYTES) == GC_SAVE_ERR_MAGIC);
  std::memcpy(b, good, sizeof b); b[4] = 2;
  CHECK(gc_save_check(b, GC_SAVE_BYTES) == GC_SAVE_ERR_VERSION);
  std::memcpy(b, good, sizeof b); b[40] ^= 1;
  CHECK(gc_save_check(b, GC_SAVE_BYTES) == GC_SAVE_ERR_CRC);
  CHECK(gc_save_import(good, GC_SAVE_BYTES - 1) == 0);
  CHECK(gc_save_import(b, GC_SAVE_BYTES) == 0);

  // 10,000 random buffers (half of them with a correct header) are rejected
  for (int i = 0; i < 10000; i++) {
    uint8_t r[GC_SAVE_BYTES];
    for (int k = 0; k < GC_SAVE_BYTES; k++) r[k] = (uint8_t)rnd32();
    if (i & 1) { r[0] = 0x47; r[1] = 0x47; r[2] = 0x51; r[3] = 0x53; r[4] = 1; r[5] = 0; r[6] = 192; r[7] = 0; }
    CHECK(gc_save_check(r, GC_SAVE_BYTES) != GC_SAVE_OK);
    CHECK(gc_save_import(r, GC_SAVE_BYTES) == 0);
  }

  // none of that changed the game
  CHECK(gc_save_export(now, GC_SAVE_BYTES) == GC_SAVE_BYTES);
  CHECK(same_game(ref, now));
  std::printf("rejection ok\n");
}

static void fill_valid(uint8_t* r) {
  for (int k = 0; k < GC_SAVE_BYTES; k++) r[k] = (uint8_t)rnd32();
  // a third of the buffers use small numbers so that fields are not always saturated
  if (rnd32() % 3 == 0) for (int k = 12; k < GC_SAVE_BYTES; k++) r[k] &= 0x0F;
  r[0] = 0x47; r[1] = 0x47; r[2] = 0x51; r[3] = 0x53;
  r[4] = 1; r[5] = 0; r[6] = 192; r[7] = 0;
  uint32_t c = gq_crc32(r + 12, GC_SAVE_BYTES - 12);
  r[8] = (uint8_t)c; r[9] = (uint8_t)(c >> 8); r[10] = (uint8_t)(c >> 16); r[11] = (uint8_t)(c >> 24);
}

static void test_random_valid() {
  int keys[256];
  int nk = all_keys(keys);
  for (int i = 0; i < 2000; i++) {
    uint8_t r[GC_SAVE_BYTES];
    fill_valid(r);
    sim_start(1 + (uint32_t)(i % 5), (int)(rnd32() % 1440));
    CHECK(gc_save_check(r, GC_SAVE_BYTES) == GC_SAVE_OK);
    CHECK(gc_save_import(r, GC_SAVE_BYTES) == 1);
    sim_minutes(10);                  // renders every 97th frame under ASan
    (void)frame_hash();
    int32_t cap = gc_query(GQ_Q_COIN_CAP);
    for (int k = 0; k < nk; k++) {
      int32_t v = gc_query(keys[k]);
      if (!in_range(keys[k], v, cap)) {
        std::printf("buffer %d: key %d value %d out of range\n", i, keys[k], (int)v);
        std::exit(1);
      }
    }
    // the sanitized game exports and re-imports cleanly
    uint8_t out[GC_SAVE_BYTES];
    CHECK(gc_save_export(out, GC_SAVE_BYTES) == GC_SAVE_BYTES);
    CHECK(gc_save_check(out, GC_SAVE_BYTES) == GC_SAVE_OK);
  }
  std::printf("random valid buffers ok\n");
}

static void test_reset() {
  sim_start(3);
  sim_minutes(300);
  gc_debug(GQ_DBG_ADD_COINS, 40);
  uint32_t serial = gc_save_serial();
  gc_reset_game();
  CHECK(gc_query(GQ_Q_COINS) == 3);
  CHECK(gc_query(GQ_Q_DAY) == 0);
  CHECK(gc_save_serial() != serial);
  sim_minutes(5);
  std::printf("reset ok\n");
}

int main() {
  test_round_trip();
  test_rejection();
  test_random_valid();
  test_reset();
  std::printf("PASS\n");
  return 0;
}
