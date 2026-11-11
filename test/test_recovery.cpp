// Host tests for the pure parts of chunk 1: CRC32, crash counting, heap watchdog,
// maintenance restart rule, settings record, upload checks.
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_recovery.cpp lib/gamecore/src/*.cpp lib/gqsafe/src/*.cpp
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../lib/gqsafe/src/bootlogic.h"
#include "../lib/gqsafe/src/gq_crc32.h"
#include "../lib/gqsafe/src/settings.h"
#include "../lib/gqsafe/src/upcheck.h"

#define CHECK(c)                                                                  \
  do {                                                                            \
    if (!(c)) {                                                                   \
      fprintf(stderr, "CHECK failed: %s  (%s:%d)\n", #c, __FILE__, __LINE__);    \
      exit(1);                                                                    \
    }                                                                             \
  } while (0)

static void crc() {
  CHECK(gq_crc32((const uint8_t*)"123456789", 9) == 0xCBF43926u);   // the standard check value; the page JavaScript matches it
  CHECK(gq_crc32(nullptr, 0) == 0);
  uint8_t b[300];
  for (int i = 0; i < 300; i++) b[i] = (uint8_t)(i * 7);
  uint32_t st = GQ_CRC32_INIT;
  st = gq_crc32_step(st, b, 100);
  st = gq_crc32_step(st, b + 100, 200);
  CHECK(gq_crc32_done(st) == gq_crc32(b, 300));
}

// ---------------------------------------------------------------- crash counting (design 8.3)
struct Dev {                 // what RTC holds between boots
  BootCounters c;
  BootDecision d;
  Dev() { memset(&c, 0, sizeof(c)); memset(&d, 0, sizeof(d)); }
  void boot(bool cold) { boot_decide(&c, cold ? 1 : 0, &d); }
  void crash() {}          // an unplanned reset leaves `planned` at 0
  void planned(bool safe = false) { c.planned = 1; c.force_safe = safe ? 1 : 0; }
  void stable() { c.crashes = 0; }            // 60 s of uptime
  void slowStable() { c.slow = 0; }           // 6 h of uptime
};

static void boots() {
  // power-on: everything clear
  Dev v; v.c.crashes = 4; v.c.slow = 3;
  v.boot(true);
  CHECK(v.c.crashes == 0 && v.c.slow == 0 && !v.d.safe && !v.d.no_display);

  // fast crash loop: safe mode at the third unplanned reset, no display at the sixth
  Dev a; a.boot(true);
  for (int i = 1; i <= 7; i++) {
    a.boot(false);
    CHECK(a.d.safe == (i >= 3));
    CHECK(a.d.no_display == (i >= 6));
  }

  // planned restarts never count
  Dev p; p.boot(true);
  for (int i = 0; i < 50; i++) { p.planned(); p.boot(false); CHECK(!p.d.safe && p.c.crashes == 0 && p.c.slow == 0); }

  // slow crash loop: each run lives longer than 60 s, so `crashes` is cleared, but `slow` reaches 5
  Dev s; s.boot(true);
  for (int i = 1; i <= 5; i++) {
    s.stable();
    s.boot(false);
    CHECK(s.c.crashes == 1);
    CHECK(s.d.safe == (i >= 5));
  }
  // safe mode keeps living: a planned restart (the user pressing Restart) alone does not clear `slow`
  s.stable(); s.planned(); s.boot(false);
  CHECK(s.c.slow == 5 && s.d.safe);                       // safety.cpp clears slow for RR_USER from safe mode and RR_UPDATE
  // 6 hours of uptime clear it
  s.slowStable(); s.planned(); s.boot(false);
  CHECK(!s.d.safe);

  // user asked for safe mode
  Dev u; u.boot(true);
  u.planned(true); u.boot(false);
  CHECK(u.d.safe && u.c.crashes == 0);
  u.stable(); u.planned(); u.boot(false);
  CHECK(!u.d.safe);                                        // the flag is consumed

  // counters saturate
  Dev z; z.boot(true);
  for (int i = 0; i < 3000; i++) z.boot(false);
  CHECK(z.c.crashes <= BOOT_COUNTER_CAP && z.c.slow <= BOOT_COUNTER_CAP);
}

// ---------------------------------------------------------------- heap watchdog (8.6)
static void heap() {
  HeapWatch w; heapwatch_init(&w);
  uint32_t t = 100000;
  // healthy
  for (int i = 0; i < 100; i++, t += 10000) CHECK(heapwatch_step(&w, t, 30000, 20000) == 0);
  CHECK(w.min_free == 30000);
  // under 12 KB: warning only after 60 s without interruption
  uint8_t r = 0;
  int n = 0;
  while (!(r & HW_WARN)) { r = heapwatch_step(&w, t, 11000, 9000); t += 10000; n++; CHECK(n < 20); }
  CHECK(n == 7 && !(r & HW_RESTART));                       // first sample starts the clock, the 7th is 60 s later
  CHECK(w.min_free == 11000);
  // an interruption restarts the clock
  CHECK(heapwatch_step(&w, t, 20000, 20000) == 0); t += 10000;
  for (int i = 0; i < 6; i++, t += 10000) CHECK(!(heapwatch_step(&w, t, 11000, 9000) & HW_WARN));
  // restart: free under 8 KB, 60 s
  HeapWatch x; heapwatch_init(&x);
  t = 5;
  n = 0; r = 0;
  while (!(r & HW_RESTART)) { r = heapwatch_step(&x, t, 7000, 7000); t += 10000; n++; CHECK(n < 20); }
  CHECK(n == 7 && (r & HW_WARN));
  // restart: largest block under 4 KB with plenty of free heap
  HeapWatch y; heapwatch_init(&y);
  t = 0xFFFFFF00u;                                           // across the millis() wrap
  n = 0; r = 0;
  while (!(r & HW_RESTART)) { r = heapwatch_step(&y, t, 20000, 3000); t += 10000; n++; CHECK(n < 20); }
  CHECK(n == 7);
}

// ---------------------------------------------------------------- maintenance restart (8.7)
static void maint() {
  MaintIn in;
  memset(&in, 0, sizeof(in));
  in.enabled = 1; in.uptime_ms = MAINT_UPTIME_MS; in.clock_valid = 1; in.hour = 4; in.minute = 10;
  in.since_http_ms = 0xFFFFFFFFu;
  CHECK(maint_due(&in));
  MaintIn b = in; b.enabled = 0;                CHECK(!maint_due(&b));
  b = in; b.uptime_ms = MAINT_UPTIME_MS - 1;    CHECK(!maint_due(&b));
  b = in; b.hour = 5;                           CHECK(!maint_due(&b));
  b = in; b.minute = 30;                        CHECK(!maint_due(&b));
  b = in; b.ap_clients = 1;                     CHECK(!maint_due(&b));
  b = in; b.updating = 1;                       CHECK(!maint_due(&b));
  b = in; b.since_http_ms = MAINT_QUIET_MS - 1; CHECK(!maint_due(&b));
  b = in; b.since_http_ms = MAINT_QUIET_MS;     CHECK(maint_due(&b));
  b = in; b.clock_valid = 0; b.hero_asleep = 0; CHECK(!maint_due(&b));
  b.hero_asleep = 1; b.hour = 12;               CHECK(maint_due(&b));
}

// ---------------------------------------------------------------- settings record
static void settings() {
  GqSettings d;
  gqs_defaults(&d);
  CHECK(gqs_check(&d) == 0);
  CHECK(sizeof(GqSettings) == 48);
  GqSettings t = d;
  t.brightness = 101; gqs_seal(&t); CHECK(gqs_check(&t) == 4);
  t = d; t.depth = 200; gqs_seal(&t); CHECK(gqs_check(&t) == 4);
  t = d; t.flags = 4; gqs_seal(&t); CHECK(gqs_check(&t) == 4);
  t = d; t.card_period = 241; gqs_seal(&t); CHECK(gqs_check(&t) == 4);
  t = d; strcpy(t.tz, "a b"); gqs_seal(&t); CHECK(gqs_check(&t) == 5);
  t = d; t.magic = 0; CHECK(gqs_check(&t) == 1);
  t = d; t.version = 9; t.crc = 0; CHECK(gqs_check(&t) != 0);
  t = d; t.brightness ^= 1; CHECK(gqs_check(&t) == 3);                  // damaged without resealing
  // every single-bit flip of the record is caught
  for (int bit = 0; bit < 48 * 8; bit++) {
    GqSettings f = d;
    ((uint8_t*)&f)[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    CHECK(gqs_check(&f) != 0);
  }
  // time zone rule (7.3)
  CHECK(gqs_tz_valid("<-03>3"));
  CHECK(gqs_tz_valid("CET-1CEST,M3.5.0,M10.5.0/3"));
  CHECK(gqs_tz_valid("UTC"));
  CHECK(!gqs_tz_valid("UT"));                                           // 2 characters
  CHECK(!gqs_tz_valid(""));
  CHECK(!gqs_tz_valid("a b c"));
  CHECK(!gqs_tz_valid("EST5;rm"));
  CHECK(!gqs_tz_valid("\xC3\xA9\xC3\xA9"));
  CHECK(gqs_tz_valid("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefgh"));            // 34
  CHECK(!gqs_tz_valid("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghi"));          // 35
}

// ---------------------------------------------------------------- upload checks (8.2 threat 1)
static const char* MARK = "GQ-OTA-PAGE-v1";

static std::vector<uint8_t> image(size_t n, int markerAt, uint32_t seed) {
  std::vector<uint8_t> v(n);
  uint32_t x = seed;
  for (auto& c : v) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; c = (uint8_t)x; }
  v[0] = 0xE9;
  if (markerAt >= 0) memcpy(&v[(size_t)markerAt], MARK, 14);
  return v;
}

static int run(const std::vector<uint8_t>& v, size_t chunk, UpRule r, bool giveSize = true, bool giveCrc = true) {
  UpChk u;
  upchk_init(&u, MARK);
  for (size_t o = 0; o < v.size(); o += chunk) upchk_update(&u, &v[o], (uint32_t)(v.size() - o < chunk ? v.size() - o : chunk));
  r.want_size = giveSize ? (uint32_t)v.size() : 0;
  r.have_crc = giveCrc;
  r.want_crc = giveCrc ? gq_crc32(v.data(), (uint32_t)v.size()) : 0;
  return upchk_verdict(&u, &r);
}

static void upload() {
  UpRule base;
  memset(&base, 0, sizeof(base));
  base.stock_size = 488304;
  // marker at every kind of position, with every kind of chunking, including across chunk borders
  for (size_t chunk : {1u, 3u, 13u, 14u, 15u, 1436u, 4096u}) {
    for (int at : {0, 1, 777, 1435, 1436 - 7, 2000}) {
      auto v = image(3000, at, 5);
      CHECK(run(v, chunk, base) == UV_OK);
    }
    auto none = image(3000, -1, 6);
    CHECK(run(none, chunk, base) == UV_NO_MARKER);
  }
  // marker split exactly at every byte of a chunk border
  for (int cut = 1; cut < 14; cut++) {
    auto v = image(500, 100, 7);
    UpChk u; upchk_init(&u, MARK);
    upchk_update(&u, v.data(), (uint32_t)(100 + cut));
    CHECK(!u.seen);
    upchk_update(&u, v.data() + 100 + cut, (uint32_t)(v.size() - 100 - cut));
    CHECK(u.seen);
  }
  // partial and overlapping prefixes do not confuse the matcher
  {
    const char* t = "GQ-OTA-PAGGQ-OTA-PAGE-v1";
    UpChk u; upchk_init(&u, MARK);
    upchk_update(&u, (const uint8_t*)t, (uint32_t)strlen(t));
    CHECK(u.seen);
    UpChk w; upchk_init(&w, MARK);
    const char* t2 = "GQ-OTA-PAGE-v2 GQ-OTA-PAGE-v";
    upchk_update(&w, (const uint8_t*)t2, (uint32_t)strlen(t2));
    CHECK(!w.seen);
  }
  // size and CRC
  auto v = image(5000, 10, 8);
  CHECK(run(v, 1436, base) == UV_OK);
  {
    UpChk u; upchk_init(&u, MARK);
    upchk_update(&u, v.data(), 4999);                       // truncated by one byte
    UpRule r = base; r.want_size = 5000; CHECK(upchk_verdict(&u, &r) == UV_SIZE);
    r.want_size = 0; r.have_crc = 1; r.want_crc = gq_crc32(v.data(), 5000);
    CHECK(upchk_verdict(&u, &r) == UV_CRC);                 // size not given: the CRC still catches it
  }
  {
    auto bad = v; bad[2500] ^= 1;                           // a flipped bit inside a valid length
    UpChk u; upchk_init(&u, MARK);
    upchk_update(&u, bad.data(), 5000);
    UpRule r = base; r.want_size = 5000; r.have_crc = 1; r.want_crc = gq_crc32(v.data(), 5000);
    CHECK(upchk_verdict(&u, &r) == UV_CRC);
  }
  // no marker: refused unless forced or stock
  auto foreign = image(100000, -1, 9);
  CHECK(run(foreign, 1436, base) == UV_NO_MARKER);
  UpRule f = base; f.force = 1;
  CHECK(run(foreign, 1436, f) == UV_OK);
  f.want_size = 1;                                          // force does not skip the size rule
  auto stock = image(488304, -1, 10);
  CHECK(run(stock, 1436, base) == UV_OK);                   // stock size, CRC unknown: size only
  UpRule sc = base; sc.stock_crc = gq_crc32(stock.data(), (uint32_t)stock.size());
  CHECK(run(stock, 1436, sc) == UV_OK);
  sc.stock_crc ^= 1;
  CHECK(run(stock, 1436, sc) == UV_NO_MARKER);              // once the stock CRC is known, size alone is not enough
  auto almost = image(488303, -1, 10);
  CHECK(run(almost, 1436, base) == UV_NO_MARKER);
  // without a size or CRC from the page the marker rule still applies
  CHECK(run(foreign, 4096, base, false, false) == UV_NO_MARKER);
  CHECK(run(v, 4096, base, false, false) == UV_OK);
}

int main() {
  crc();
  boots();
  heap();
  maint();
  settings();
  upload();
  printf("test_recovery: all passed\n");
  return 0;
}
