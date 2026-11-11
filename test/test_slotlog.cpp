// R5 flash record log: simulated flash (bits only clear on write, erase sets whole sector to 0xFF) with power cuts at random
// bytes during erases and writes. After every cut the newest good record must still load and must be the last record whose
// write had finished (or the one that was being written, if it happened to complete).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "../lib/gqsafe/src/slotlog.h"

#define CHECK(c) do { if (!(c)) { std::printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)

static uint8_t flash[SL_SECTORS * SL_SECTOR_BYTES];
static uint32_t rng = 12345;
static uint8_t* slot_ptr(uint32_t i) { return flash + (i / SL_SLOTS_PER_SECTOR) * SL_SECTOR_BYTES + (i % SL_SLOTS_PER_SECTOR) * SL_SLOT_BYTES; }
static uint32_t rnd() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void scan(SlScan* s) {
  for (uint32_t i = 0; i < SL_SLOTS; i++) {
    uint32_t q = 0;
    s->state[i] = (uint8_t)sl_classify(slot_ptr(i), &q);
    s->seq[i] = q;
  }
}

static void payload_for(uint32_t n, uint8_t* p) {
  uint32_t x = n * 2654435761u + 1;
  for (uint32_t i = 0; i < SL_PAYLOAD_BYTES; i++) { x = x * 1664525u + 1013904223u; p[i] = (uint8_t)(x >> 24); }
}

// One save. cut < 0: completes. Otherwise the power fails after `cut` units (erase bytes, then write bytes).
// Returns 1 if the whole save completed.
static int do_save(uint32_t n, int cut) {
  SlScan sc; scan(&sc);
  SlPlan pl; sl_plan(&sc, &pl);
  uint8_t pay[SL_PAYLOAD_BYTES], slot[SL_SLOT_BYTES];
  payload_for(n, pay);
  pl.next_seq = pl.next_seq;   // seq is taken from the log itself
  sl_build(slot, pl.next_seq, pay);
  int left = cut;
  if (pl.erase_sector >= 0) {
    uint8_t* sec = flash + (uint32_t)pl.erase_sector * SL_SECTOR_BYTES;
    for (uint32_t i = 0; i < SL_SECTOR_BYTES; i++) {
      if (cut >= 0 && left-- <= 0) return 0;
      sec[i] = 0xFF;
    }
  }
  uint8_t* dst = slot_ptr((uint32_t)pl.write_slot);
  for (uint32_t i = 0; i < SL_SLOT_BYTES; i++) {
    if (cut >= 0 && left-- <= 0) return 0;
    dst[i] &= slot[i];
  }
  return 1;
}

static int load(uint8_t* pay) {
  SlScan sc; scan(&sc);
  SlPlan pl; sl_plan(&sc, &pl);
  if (pl.load < 0) return 0;
  std::memcpy(pay, slot_ptr((uint32_t)pl.load) + 16, SL_PAYLOAD_BYTES);
  return 1;
}

static void test_basic() {
  std::memset(flash, 0xFF, sizeof(flash));
  uint8_t got[SL_PAYLOAD_BYTES], want[SL_PAYLOAD_BYTES];
  CHECK(!load(got));
  for (uint32_t n = 1; n <= 200; n++) {
    CHECK(do_save(n, -1));
    CHECK(load(got));
    payload_for(n, want);
    CHECK(std::memcmp(got, want, sizeof(got)) == 0);
  }
}

static void test_cuts(uint32_t seed, int iterations) {
  rng = seed;
  std::memset(flash, 0xFF, sizeof(flash));
  uint32_t committed = 0;        // last save number known to be complete
  int erases = 0;
  for (int it = 1; it <= iterations; it++) {
    uint32_t n = (uint32_t)it;
    int cut = -1;
    if (rnd() % 3 == 0) cut = (int)(rnd() % (SL_SLOT_BYTES + SL_SECTOR_BYTES + 8));   // sometimes beyond the end: completes
    SlScan sc; scan(&sc); SlPlan pl; sl_plan(&sc, &pl);
    if (pl.erase_sector >= 0) erases++;
    int done = do_save(n, cut);
    if (done) committed = n;
    // power back on: the log must load a record that is either the committed one or (if the cut came after the last byte) the new one
    uint8_t got[SL_PAYLOAD_BYTES], want[SL_PAYLOAD_BYTES];
    int have = load(got);
    if (committed == 0) {
      if (have) {   // only the in-flight record can have survived
        payload_for(n, want);
        CHECK(std::memcmp(got, want, sizeof(got)) == 0);
        committed = n;
      }
    } else {
      CHECK(have);
      payload_for(committed, want);
      if (std::memcmp(got, want, sizeof(got)) != 0) {
        payload_for(n, want);                       // an in-flight record that happened to be complete is also fine
        CHECK(std::memcmp(got, want, sizeof(got)) == 0);
        committed = n;
      }
    }
  }
  CHECK(erases > 0);
}

static void test_wear() {
  std::memset(flash, 0xFF, sizeof(flash));
  int er[SL_SECTORS] = {0, 0, 0};
  for (int n = 1; n <= 3600; n++) {
    SlScan sc; scan(&sc); SlPlan pl; sl_plan(&sc, &pl);
    if (pl.erase_sector >= 0) er[pl.erase_sector]++;
    CHECK(do_save((uint32_t)n, -1));
  }
  // 3600 saves: each sector erased about 3600 / 36 = 100 times, evenly
  for (uint32_t s = 0; s < SL_SECTORS; s++) CHECK(er[s] >= 99 && er[s] <= 101);
}

static void test_junk_start() {
  for (uint32_t seed = 1; seed <= 20; seed++) {   // a flash full of garbage (old firmware pieces) must not stop saving
    rng = seed * 77;
    for (uint32_t i = 0; i < sizeof(flash); i++) flash[i] = (uint8_t)rnd();
    uint8_t got[SL_PAYLOAD_BYTES], want[SL_PAYLOAD_BYTES];
    CHECK(!load(got));
    for (uint32_t n = 1; n <= 80; n++) {
      CHECK(do_save(n, -1));
      CHECK(load(got));
      payload_for(n, want);
      CHECK(std::memcmp(got, want, sizeof(got)) == 0);
    }
  }
}

static void test_classify() {
  uint8_t slot[SL_SLOT_BYTES], pay[SL_PAYLOAD_BYTES];
  payload_for(9, pay);
  sl_build(slot, 77, pay);
  uint32_t seq = 0;
  CHECK(sl_classify(slot, &seq) == SL_VALID && seq == 77);
  for (uint32_t i = 0; i < SL_SLOT_BYTES; i++) {            // every single flipped bit is caught (padding included)
    for (int b = 0; b < 8; b++) {
      slot[i] ^= (uint8_t)(1u << b);
      CHECK(sl_classify(slot, &seq) != SL_VALID);
      slot[i] ^= (uint8_t)(1u << b);
    }
  }
  std::memset(slot, 0xFF, sizeof(slot));
  CHECK(sl_classify(slot, &seq) == SL_BLANK);
  slot[319] = 0xFE;
  CHECK(sl_classify(slot, &seq) == SL_JUNK);
}

int main() {
  test_classify();
  test_basic();
  test_wear();
  test_junk_start();
  for (uint32_t seed = 1; seed <= 40; seed++) test_cuts(seed, 3000);
  std::printf("test_slotlog: all passed\n");
  return 0;
}
