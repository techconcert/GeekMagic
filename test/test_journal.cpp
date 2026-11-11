// Host test for the flash journal (design 9.4, 11.3): a simulated NOR sector with injected power cuts.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_journal.cpp lib/gamecore/src/*.cpp lib/gqsafe/src/*.cpp
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>

#include "../lib/gqsafe/src/journal.h"
#include "../lib/gqsafe/src/gq_crc32.h"

#define CHECK(c)                                                                          \
  do {                                                                                    \
    if (!(c)) {                                                                           \
      fprintf(stderr, "CHECK failed: %s  (%s:%d, seq run %u)\n", #c, __FILE__, __LINE__, g_run); \
      exit(1);                                                                            \
    }                                                                                     \
  } while (0)

static unsigned g_run = 0;
static uint32_t g_rs = 0x1234567u;
static uint32_t rnd() {
  g_rs ^= g_rs << 13; g_rs ^= g_rs >> 17; g_rs ^= g_rs << 5;
  return g_rs;
}

// ---------------------------------------------------------------- simulated NOR sector
struct Sim {
  uint8_t mem[4096];
  int64_t arm;            // units left before a power cut (a written byte or an erase is one unit); < 0 = never
  bool dead;              // power is gone: every call fails until reboot()
  bool eraseHit;          // an erase was started since the last reboot()
  bool cutHappened;       // a cut hit since the last reboot()
  uint32_t erases;        // every erase call, complete or not
  uint32_t oob;           // accesses outside 0..4095 or unaligned
  void blank() { memset(mem, 0xFF, sizeof(mem)); }
  void reboot() { dead = false; arm = -1; eraseHit = false; cutHappened = false; }
};

static int sRead(void* c, uint32_t off, uint8_t* buf, uint32_t n) {
  Sim* s = (Sim*)c;
  if ((off & 3) || (n & 3) || off + n > 4096 || n == 0) { s->oob++; return -1; }
  if (s->dead) return -1;
  memcpy(buf, s->mem + off, n);
  return 0;
}
static int sWrite(void* c, uint32_t off, const uint8_t* buf, uint32_t n) {
  Sim* s = (Sim*)c;
  if ((off & 3) || (n & 3) || off + n > 4096 || n == 0) { s->oob++; return -1; }
  if (s->dead) return -1;
  for (uint32_t i = 0; i < n; i++) {
    if (s->arm == 0) { s->dead = true; s->cutHappened = true; return -1; }   // power lost before this byte
    if (s->arm > 0) s->arm--;
    s->mem[off + i] &= buf[i];                                              // NOR: bits only go 1 -> 0
  }
  return 0;
}
static int sErase(void* c) {
  Sim* s = (Sim*)c;
  if (s->dead) return -1;
  s->erases++;
  s->eraseHit = true;
  if (s->arm == 0) {                                                        // cut during the erase: random content
    for (int i = 0; i < 4096; i++) s->mem[i] = (uint8_t)rnd();
    s->dead = true;
    s->cutHappened = true;
    return -1;
  }
  if (s->arm > 0) s->arm--;
  memset(s->mem, 0xFF, 4096);
  return 0;
}
static JournalHal hal(Sim* s) { JournalHal h = {sRead, sWrite, sErase, s}; return h; }

static void fillPayload(uint8_t* p) { for (int i = 0; i < 240; i++) p[i] = (uint8_t)rnd(); }

// ---------------------------------------------------------------- basic behaviour
static void basics() {
  Sim s; s.blank(); s.reboot(); s.erases = 0; s.oob = 0;
  JournalHal h = hal(&s);
  uint32_t used = 99, ec = 99;
  CHECK(journal_probe(&h, &used, &ec) == JR_BLANK);
  uint8_t p[240], q[240];
  fillPayload(p);
  CHECK(journal_append(&h, p, 1) == JR_BLANK);        // refuses until formatted
  CHECK(journal_load(&h, q, nullptr) == JR_BLANK);
  CHECK(journal_format(&h) == JR_OK);
  CHECK(journal_probe(&h, &used, &ec) == JR_OK && used == 0 && ec == 0);
  CHECK(journal_load(&h, q, nullptr) == JR_NO_VALID);
  CHECK(journal_format(&h) == JR_OK);                 // already formatted: no change
  uint32_t seq = 0;
  for (uint32_t i = 1; i <= 40; i++) {
    fillPayload(p);
    CHECK(journal_append(&h, p, i) == JR_OK);
    CHECK(journal_load(&h, q, &seq) == JR_OK && seq == i && memcmp(p, q, 240) == 0);
  }
  CHECK(s.erases == 2);                               // 40 appends = 15 + 15 + 10: two rollovers
  CHECK(journal_probe(&h, &used, &ec) == JR_OK && ec == 2 && used == 10);
  CHECK(journal_erase(&h) == JR_OK);
  CHECK(journal_probe(&h, nullptr, nullptr) == JR_BLANK);
  CHECK(s.oob == 0);
}

static void foreign() {
  for (int k = 0; k < 200; k++) {
    Sim s; s.reboot(); s.erases = 0; s.oob = 0;
    for (int i = 0; i < 4096; i++) s.mem[i] = (uint8_t)rnd();
    if (k % 3 == 0) { memset(s.mem, 0xFF, 4096); s.mem[rnd() % 4096] = 0x7F; }   // almost blank: still foreign
    uint8_t before[4096];
    memcpy(before, s.mem, 4096);
    JournalHal h = hal(&s);
    uint8_t p[240];
    fillPayload(p);
    CHECK(journal_probe(&h, nullptr, nullptr) == JR_FOREIGN);
    CHECK(journal_append(&h, p, 1) == JR_FOREIGN);
    CHECK(journal_format(&h) == JR_FOREIGN);
    CHECK(journal_load(&h, p, nullptr) == JR_FOREIGN);
    CHECK(memcmp(before, s.mem, 4096) == 0);          // nothing was written
    CHECK(s.erases == 0 && s.oob == 0);
  }
}

// a damaged commit word, CRC or length must hide a slot
static void corruption() {
  Sim s; s.blank(); s.reboot();
  JournalHal h = hal(&s);
  uint8_t p[240], q[240];
  CHECK(journal_format(&h) == JR_OK);
  fillPayload(p);
  CHECK(journal_append(&h, p, 7) == JR_OK);
  uint8_t save[256];
  memcpy(save, s.mem + 256, 256);
  for (int off : {0, 1, 2, 4, 8, 100, 247, 248, 252}) {
    memcpy(s.mem + 256, save, 256);
    if (!s.mem[256 + off]) continue;
    s.mem[256 + off] &= (uint8_t)(s.mem[256 + off] - 1);   // clear the lowest set bit
    uint32_t seq;
    int r = journal_load(&h, q, &seq);
    CHECK(r == JR_NO_VALID);
  }
  memcpy(s.mem + 256, save, 256);
  CHECK(journal_load(&h, q, nullptr) == JR_OK && memcmp(p, q, 240) == 0);
}

// ---------------------------------------------------------------- random power cuts
static void randomRuns() {
  for (g_run = 0; g_run < 10000; g_run++) {
    Sim s; s.blank(); s.reboot(); s.erases = 0; s.oob = 0;
    JournalHal h = hal(&s);
    bool formatted = false;
    if (rnd() % 4 == 0) { for (int i = 0; i < 4096; i++) s.mem[i] = (uint8_t)rnd(); }   // a torn sector from the start
    uint8_t good[240], pend[240];
    bool haveGood = false;
    uint32_t goodSeq = 0, pendSeq = 0, seq = 0;
    uint32_t appends = 0, cuts = 0, explicitErases = 0;
    int ops = 20 + (int)(rnd() % 60);
    for (int op = 0; op < ops; op++) {
      uint32_t r = rnd() % 10;
      uint32_t probe = journal_probe(&h, nullptr, nullptr);
      if (probe != JR_OK) {
        // repair path of the firmware: only the user's button erases and formats
        CHECK(journal_append(&h, pend, 1) == (int)probe);          // refused, nothing written
        if (probe == JR_FOREIGN) { CHECK(journal_erase(&h) == JR_OK); explicitErases++; }
        CHECK(journal_format(&h) == JR_OK);
        formatted = true;
        haveGood = false;
        continue;
      }
      formatted = true;
      if (r < 7) {                                                 // append, maybe with a power cut
        fillPayload(pend);
        pendSeq = ++seq;
        bool cut = rnd() % 4 == 0;
        s.arm = cut ? (int64_t)(rnd() % 600) : -1;
        s.eraseHit = false;
        int res = journal_append(&h, pend, pendSeq);
        bool cutNow = s.cutHappened;
        bool eraseHit = s.eraseHit;
        s.reboot();
        if (res == JR_OK) {
          CHECK(!cutNow);
          appends++;
          memcpy(good, pend, 240);
          goodSeq = pendSeq;
          haveGood = true;
        } else {
          cuts++;
          CHECK(cutNow);                                           // a failure only ever comes from the injected cut
          uint8_t q[240];
          uint32_t qs = 0;
          int lr = journal_load(&h, q, &qs);
          if (lr == JR_OK) {
            bool isGood = haveGood && qs == goodSeq && memcmp(q, good, 240) == 0;
            bool isPend = qs == pendSeq && memcmp(q, pend, 240) == 0;
            CHECK(isGood || isPend);                               // never a payload that was not appended
            if (isGood) CHECK(!eraseHit);                          // an erase destroys the old slots
            memcpy(good, q, 240);
            goodSeq = qs;
            haveGood = true;
          } else {
            CHECK(eraseHit || !haveGood);                          // nothing survives only when an erase was cut
            haveGood = false;
          }
        }
      } else if (r < 9) {                                          // reboot and read
        s.reboot();
        uint8_t q[240];
        uint32_t qs = 0;
        int lr = journal_load(&h, q, &qs);
        if (haveGood) { CHECK(lr == JR_OK && qs == goodSeq && memcmp(q, good, 240) == 0); }
        else CHECK(lr == JR_OK ? false : true);
      } else {                                                     // the user disables and re-enables saving
        CHECK(journal_erase(&h) == JR_OK);
        explicitErases++;
        CHECK(journal_format(&h) == JR_OK);
        haveGood = false;
      }
    }
    (void)formatted;
    CHECK(s.oob == 0);
    CHECK(s.erases <= appends / 15 + cuts + explicitErases + 1);
  }
  g_run = 0;
}

int main() {
  basics();
  foreign();
  corruption();
  randomRuns();
  printf("test_journal: all passed\n");
  return 0;
}
