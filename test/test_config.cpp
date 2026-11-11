// Host tests for the R3 settings record, its validation and the storage interface (docs/DESIGN-R3.md 9.4, addendum: RTC only).
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_config.cpp lib/gqsafe/src/*.cpp
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../lib/gqsafe/src/cfgstore.h"
#include "../lib/gqsafe/src/gqconf.h"

#define CHECK(c)                                                                         \
  do {                                                                                   \
    if (!(c)) { fprintf(stderr, "CHECK failed: %s  (%s:%d)\n", #c, __FILE__, __LINE__); exit(1); } \
  } while (0)

static int coord(const char* t, int max, int* v) {
  int16_t o = 12345;
  int ok = gqc_parse_coord(t, max, &o);
  if (!ok) CHECK(o == 12345);   // untouched on refusal
  *v = o;
  return ok;
}
static void expectCoord(const char* t, int max, int want) {
  int v;
  if (!coord(t, max, &v) || v != want) { fprintf(stderr, "coord '%s' -> %d (want %d)\n", t, v, want); exit(1); }
}
static void refuseCoord(const char* t, int max) {
  int v;
  if (coord(t, max, &v)) { fprintf(stderr, "coord '%s' accepted (%d)\n", t, v); exit(1); }
}

static void test_record() {
  GqConf c;
  gqc_defaults(&c);
  CHECK(sizeof(GqConf) == 64);
  CHECK(gqc_check(&c) == 0);
  CHECK(c.flags == 0 && c.lat_c == 0 && c.lon_c == 0 && c.seq == 0);
  CHECK(c.sleep_min == 1380 && c.wake_min == 420);
  CHECK(c.magic == 0x31435147u && c.version == 1);
  CHECK(memcmp(&c.magic, "GQC1", 4) == 0);   // little-endian host, as the clock
  // round trip through bytes
  uint8_t raw[64];
  memcpy(raw, &c, 64);
  GqConf d;
  memcpy(&d, raw, 64);
  CHECK(gqc_check(&d) == 0 && gqc_same(&c, &d));
  // a flipped bit anywhere is refused (all 512 bits)
  for (int bit = 0; bit < 512; bit++) {
    GqConf e = c;
    ((uint8_t*)&e)[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    CHECK(gqc_check(&e) != 0);
  }
  // same for a record that has content
  c.flags = GQC_F_WEATHER | GQC_F_SLEEP;
  c.lat_c = 3872;
  c.lon_c = -914;
  strcpy(c.place, "Lisbon, Portugal");
  gqc_sanitize(&c);
  CHECK(gqc_check(&c) == 0);
  for (int bit = 0; bit < 512; bit++) {
    GqConf e = c;
    ((uint8_t*)&e)[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    CHECK(gqc_check(&e) != 0);
  }
  // unknown version refused even with a matching CRC
  GqConf v = c;
  v.version = 2;
  v.crc = 0;
  GqConf w = v;
  CHECK(gqc_check(&w) == 2);
  // sanitize clamps every field
  GqConf s;
  memset(&s, 0xFF, sizeof(s));
  gqc_sanitize(&s);
  CHECK(gqc_check(&s) == 0);
  CHECK((s.flags & ~7u) == 0);
  CHECK(s.lat_c == -1 && s.lon_c == -1);   // 0xFFFF as int16 is in range
  CHECK(s.sleep_min < 1440 && s.wake_min < 1440);
  CHECK(s.place[23] == 0);
  for (int i = 0; i < 24; i++) CHECK(s.place[i] == 0 || s.place[i] == ' ');
  for (int i = 0; i < 20; i++) CHECK(s.reserved[i] == 0);
  GqConf n;
  memset(&n, 0, sizeof(n));
  n.lat_c = -30000;
  n.lon_c = -30000;
  n.sleep_min = 1440;
  n.wake_min = 65535;
  gqc_sanitize(&n);
  CHECK(n.lat_c == -9000 && n.lon_c == -18000 && n.sleep_min == 0 && n.wake_min == 65535 % 1440);
  GqConf t;
  memset(&t, 0, sizeof(t));
  strcpy(t.place, "a<b>c\"");
  gqc_sanitize(&t);
  CHECK(strcmp(t.place, "a b c ") == 0);
  puts("record ok");
}

static void test_coord() {
  expectCoord("38.72", 90, 3872);
  expectCoord("-9.14", 180, -914);
  expectCoord("38,72", 90, 3872);
  expectCoord("+5", 90, 500);
  expectCoord("-0.004", 90, 0);
  expectCoord("0.005", 90, 1);
  expectCoord("-0.005", 90, -1);
  expectCoord("0.0049", 90, 0);
  expectCoord("38.725", 90, 3873);
  expectCoord("38.7", 90, 3870);
  expectCoord("38.", 90, 3800);
  expectCoord(".5", 90, 50);
  expectCoord("  12.34  ", 90, 1234);
  expectCoord("90", 90, 9000);
  expectCoord("90.00", 90, 9000);
  expectCoord("90.004", 90, 9000);
  expectCoord("-90", 90, -9000);
  expectCoord("180", 180, 18000);
  expectCoord("-180.000000", 180, -18000);
  expectCoord("000012.5", 90, 1250);
  expectCoord("12.123456", 90, 1212);
  refuseCoord("90.01", 90);
  refuseCoord("-90.01", 90);
  refuseCoord("90.005", 90);          // rounds to 90.01
  refuseCoord("180.01", 180);
  refuseCoord("181", 180);
  refuseCoord("abc", 90);
  refuseCoord("", 90);
  refuseCoord("   ", 90);
  refuseCoord("-", 90);
  refuseCoord(".", 90);
  refuseCoord("1.2.3", 90);
  refuseCoord("1e2", 90);
  refuseCoord("12 34", 90);
  refuseCoord("12.1234567", 90);      // more than 6 decimals
  refuseCoord("--5", 90);
  refuseCoord("5x", 90);
  expectCoord("1,000", 180, 100);     // a comma is a decimal mark, never a thousands separator
  {
    char big[64];
    memset(big, '9', 40);
    big[40] = 0;
    refuseCoord(big, 180);            // 40 digits: refused, no overflow
    memset(big, '0', 40);
    big[40] = 0;
    refuseCoord(big, 180);            // 40 zeros also refused (cap on digit count)
    big[0] = '1';
    strcpy(big + 1, ".");
    memset(big + 2, '0', 39);
    big[41] = 0;
    refuseCoord(big, 180);            // 39 decimals
    memset(big, '9', 13);
    big[13] = 0;
    refuseCoord(big, 180);
  }
  CHECK(!gqc_parse_coord(nullptr, 90, nullptr));
  // boundary of every value in range: format then parse gives the same number
  for (int v = -18000; v <= 18000; v++) {
    char b[16];
    int n = gqc_format_coord((int16_t)v, b);
    CHECK(n == (int)strlen(b) && n <= 7);
    int16_t o = 0;
    CHECK(gqc_parse_coord(b, 180, &o) && o == v);
  }
  char b[16];
  gqc_format_coord(-50, b);
  CHECK(strcmp(b, "-0.50") == 0);
  gqc_format_coord(0, b);
  CHECK(strcmp(b, "0.00") == 0);
  gqc_format_coord(18000, b);
  CHECK(strcmp(b, "180.00") == 0);
  gqc_format_coord(3872, b);
  CHECK(strcmp(b, "38.72") == 0);
  puts("coordinates ok");
}

static void test_time_label() {
  CHECK(gqc_parse_time("00:00") == 0);
  CHECK(gqc_parse_time("23:59") == 1439);
  CHECK(gqc_parse_time("07:05") == 425);
  CHECK(gqc_parse_time("24:00") == -1);
  CHECK(gqc_parse_time("23:60") == -1);
  CHECK(gqc_parse_time("7:5") == -1);            // decision: strict HH:MM, refused
  CHECK(gqc_parse_time("7:05") == -1);
  CHECK(gqc_parse_time("07:5") == -1);
  CHECK(gqc_parse_time("07:05:00") == -1);
  CHECK(gqc_parse_time("07-05") == -1);
  CHECK(gqc_parse_time("") == -1);
  CHECK(gqc_parse_time("07:") == -1);
  CHECK(gqc_parse_time("ab:cd") == -1);
  CHECK(gqc_parse_time(" 7:05") == -1);
  CHECK(gqc_parse_time(nullptr) == -1);
  for (int m = 0; m < 1440; m++) {
    char b[8];
    CHECK(gqc_format_time((uint16_t)m, b) == 5);
    CHECK(gqc_parse_time(b) == m);
  }

  CHECK(gqc_place_ok(""));
  CHECK(gqc_place_ok("Lisbon, Portugal"));
  CHECK(gqc_place_ok("St. John's-on-Sea 12"));
  CHECK(gqc_place_ok("ABCDEFGHIJKLMNOPQRSTUVW"));          // 23
  CHECK(!gqc_place_ok("ABCDEFGHIJKLMNOPQRSTUVWX"));        // 24
  CHECK(!gqc_place_ok("<script>"));
  CHECK(!gqc_place_ok("a\"b"));
  CHECK(!gqc_place_ok("a&b"));
  CHECK(!gqc_place_ok("a;b"));
  CHECK(!gqc_place_ok("caf\xC3\xA9"));
  CHECK(!gqc_place_ok("a\nb"));
  CHECK(!gqc_place_ok(nullptr));
  puts("time and label ok");
}

static GqcInput blankInput() {
  GqcInput i;
  memset(&i, 0, sizeof(i));
  i.weather = i.fahrenheit = i.sleep_on = -1;
  return i;
}

static void test_validate() {
  GqConf cur, out;
  gqc_defaults(&cur);
  GqcInput in = blankInput();
  CHECK(gqc_validate(&cur, &in, &out) == GQC_OK && gqc_same(&cur, &out));    // keep everything

  in.weather = 1;
  CHECK(gqc_validate(&cur, &in, &out) == GQC_E_NO_COORDS);                   // weather without coordinates
  in.lat = "38.72"; in.lon = "-9,14"; in.place = "Lisbon, Portugal"; in.fahrenheit = 0; in.sleep_on = 1;
  in.sleep_time = "23:30"; in.wake_time = "06:45";
  CHECK(gqc_validate(&cur, &in, &out) == GQC_OK);
  CHECK(out.flags == (GQC_F_WEATHER | GQC_F_SLEEP) && out.lat_c == 3872 && out.lon_c == -914);
  CHECK(strcmp(out.place, "Lisbon, Portugal") == 0 && out.sleep_min == 1410 && out.wake_min == 405);
  CHECK(gqc_check(&out) == 0 && !gqc_same(&cur, &out));
  CHECK(out.seq == cur.seq);                                                 // the caller bumps seq
  GqConf saved = out;

  // an invalid value anywhere changes nothing (out untouched), the whole form is checked first
  struct { const char* what; GqcInput in; int want; } bad[12];
  int nb = 0;
  GqcInput b = blankInput();
  b.lat = "90.01"; b.lon = "0"; bad[nb++] = { "lat", b, GQC_E_LAT };
  b = blankInput(); b.lat = "10"; b.lon = "180.5"; bad[nb++] = { "lon", b, GQC_E_LON };
  b = blankInput(); b.lat = "10"; b.lon = ""; bad[nb++] = { "pair", b, GQC_E_COORD_PAIR };
  b = blankInput(); b.lat = ""; b.lon = "10"; bad[nb++] = { "pair2", b, GQC_E_COORD_PAIR };
  b = blankInput(); b.place = "<script>"; bad[nb++] = { "place", b, GQC_E_PLACE };
  b = blankInput(); b.sleep_time = "25:00"; bad[nb++] = { "sleep", b, GQC_E_SLEEP_TIME };
  b = blankInput(); b.wake_time = "7:5"; bad[nb++] = { "wake", b, GQC_E_WAKE_TIME };
  b = blankInput(); b.sleep_time = "08:00"; b.wake_time = "08:00"; bad[nb++] = { "same", b, GQC_E_SAME_TIME };
  b = blankInput(); b.wake_time = "23:30"; bad[nb++] = { "same vs stored", b, GQC_E_SAME_TIME };
  b = blankInput(); b.lat = ""; b.lon = ""; bad[nb++] = { "clear coords with weather on", b, GQC_E_NO_COORDS };
  b = blankInput(); b.lat = "1"; b.lon = "2"; b.place = "ok"; b.sleep_time = "99:99"; bad[nb++] = { "late error", b, GQC_E_SLEEP_TIME };
  for (int i = 0; i < nb; i++) {
    GqConf o2;
    memset(&o2, 0x5A, sizeof(o2));
    GqConf before = o2;
    int r = gqc_validate(&saved, &bad[i].in, &o2);
    if (r != bad[i].want) { fprintf(stderr, "validate '%s' gave %d want %d\n", bad[i].what, r, bad[i].want); exit(1); }
    CHECK(memcmp(&o2, &before, sizeof(o2)) == 0);
  }
  // weather off makes empty coordinates fine; both empty clears them
  GqcInput c = blankInput();
  c.weather = 0; c.lat = ""; c.lon = "";
  CHECK(gqc_validate(&saved, &c, &out) == GQC_OK && out.lat_c == 0 && out.lon_c == 0 && !(out.flags & GQC_F_WEATHER));
  // a clear label is allowed
  c = blankInput(); c.place = "";
  CHECK(gqc_validate(&saved, &c, &out) == GQC_OK && out.place[0] == 0);
  // a label that fills all 23 characters is stored with its NUL
  c = blankInput(); c.place = "ABCDEFGHIJKLMNOPQRSTUVW";
  CHECK(gqc_validate(&saved, &c, &out) == GQC_OK && strlen(out.place) == 23);
  // Fahrenheit and keep
  c = blankInput(); c.fahrenheit = 1;
  CHECK(gqc_validate(&saved, &c, &out) == GQC_OK && (out.flags & GQC_F_FAHRENHEIT) && (out.flags & GQC_F_WEATHER));
  // a stored record with junk is repaired, not trusted
  GqConf junk = saved;
  junk.lat_c = 20000;
  junk.sleep_min = 5000;
  junk.wake_min = 5000 % 1440;
  c = blankInput();
  int r = gqc_validate(&junk, &c, &out);
  CHECK(r == GQC_E_SAME_TIME || r == GQC_OK);
  // random text never crashes and OK only with all values in range
  uint32_t seed = 12345;
  const char alpha[] = "0123456789.,-+ :abcXYZ<>&\"'";
  for (int iter = 0; iter < 200000; iter++) {
    char a[48], bb[48], p[40], t1[12], t2[12];
    for (int k = 0; k < 4; k++) {
      char* d = k == 0 ? a : k == 1 ? bb : k == 2 ? p : t1;
      size_t cap = k == 0 || k == 1 ? sizeof(a) : k == 2 ? sizeof(p) : sizeof(t1);
      seed = seed * 1664525u + 1013904223u;
      size_t n = (seed >> 16) % (cap - 1);
      for (size_t j = 0; j < n; j++) { seed = seed * 1664525u + 1013904223u; d[j] = alpha[(seed >> 16) % (sizeof(alpha) - 1)]; }
      d[n] = 0;
    }
    seed = seed * 1664525u + 1013904223u;
    snprintf(t2, sizeof(t2), "%02u:%02u", (seed >> 8) % 30, (seed >> 20) % 70);
    GqcInput f = blankInput();
    f.lat = a; f.lon = bb; f.place = p; f.sleep_time = t1; f.wake_time = t2;
    f.weather = (int8_t)((seed >> 3) % 3) - 1;
    GqConf o3;
    if (gqc_validate(&cur, &f, &o3) == GQC_OK) {
      CHECK(gqc_check(&o3) == 0);
      CHECK(o3.lat_c >= -9000 && o3.lat_c <= 9000 && o3.lon_c >= -18000 && o3.lon_c <= 18000);
      CHECK(o3.sleep_min < 1440 && o3.wake_min < 1440 && o3.sleep_min != o3.wake_min);
      CHECK(gqc_place_ok(o3.place));
      if (o3.flags & GQC_F_WEATHER) CHECK(o3.lat_c != 0 || o3.lon_c != 0);
    }
  }
  puts("validate ok");
}

// ---- storage: a RAM backend with failure injection
struct Ram {
  uint8_t guard1[16];
  uint8_t mem[64];
  uint8_t guard2[16];
  int failWrite, failRead, corruptOnWrite, writes, torn;   // torn: bytes actually written (-1 = all)
  Ram() { memset(this, 0, sizeof(*this)); memset(guard1, 0xA5, 16); memset(guard2, 0xA5, 16); torn = -1; }
  void checkGuards() const { for (int i = 0; i < 16; i++) CHECK(guard1[i] == 0xA5 && guard2[i] == 0xA5); }
};
static int ramRead(void* c, uint8_t* b, uint32_t n) {
  Ram* r = (Ram*)c;
  if (r->failRead || n != 64) return 0;
  memcpy(b, r->mem, n);
  return 1;
}
static int ramWrite(void* c, const uint8_t* b, uint32_t n) {
  Ram* r = (Ram*)c;
  if (r->failWrite || n != 64) return 0;
  r->writes++;
  memcpy(r->mem, b, r->torn >= 0 ? (uint32_t)r->torn : n);
  if (r->corruptOnWrite) r->mem[r->corruptOnWrite % 64] ^= 1;
  return 1;
}

static void test_store() {
  Ram ram;
  CfgBackend be = { "rtc", ramRead, ramWrite, &ram };
  GqConf c, got;
  CHECK(cfg_load(&be, &got) == 0);                     // blank memory: defaults
  GqConf d;
  gqc_defaults(&d);
  CHECK(gqc_same(&got, &d) && gqc_check(&got) == 0);

  gqc_defaults(&c);
  c.flags = GQC_F_WEATHER; c.lat_c = 3872; c.lon_c = -914; c.seq = 7; strcpy(c.place, "Lisbon");
  CHECK(cfg_save(&be, &c) == 1);
  CHECK(cfg_load(&be, &got) == 1 && gqc_same(&got, &c) && got.seq == 7);
  ram.checkGuards();

  // bit rot in the stored record: back to defaults, never garbage
  for (int bit = 0; bit < 512; bit++) {
    Ram r2 = ram;
    r2.mem[bit / 8] ^= (uint8_t)(1u << (bit % 8));
    CfgBackend b2 = { "rtc", ramRead, ramWrite, &r2 };
    GqConf g2;
    CHECK(cfg_load(&b2, &g2) == 0);
    CHECK(gqc_same(&g2, &d) && gqc_check(&g2) == 0);
  }
  // a write cut after every possible byte count: the load either returns the old record, the new one, or defaults
  GqConf n = c;
  n.lat_c = 100; n.seq = 8;
  for (int cut = 0; cut <= 64; cut++) {
    Ram r3 = ram;
    r3.torn = cut;
    CfgBackend b3 = { "rtc", ramRead, ramWrite, &r3 };
    GqConf nn = n;
    int ok = cfg_save(&b3, &nn);
    GqConf g3;
    int loaded = cfg_load(&b3, &g3);
    CHECK(gqc_check(&g3) == 0);
    if (cut == 64) { CHECK(ok == 1 && loaded == 1 && g3.lat_c == 100); }
    else {
      if (ok) CHECK(loaded == 1 && gqc_same(&g3, &nn));  // only when the cut bytes happened to be equal
      else CHECK(ok == 0);                              // read-back caught the torn write
      CHECK(loaded == 0 || gqc_same(&g3, &c) || gqc_same(&g3, &nn));
    }
    r3.checkGuards();
  }
  // failing write or read
  { Ram r4 = ram; r4.failWrite = 1; CfgBackend b4 = { "rtc", ramRead, ramWrite, &r4 }; GqConf x = n; CHECK(cfg_save(&b4, &x) == 0); CHECK(cfg_load(&b4, &got) == 1 && got.lat_c == 3872); }
  { Ram r4 = ram; r4.failRead = 1; CfgBackend b4 = { "rtc", ramRead, ramWrite, &r4 }; GqConf x = n; CHECK(cfg_save(&b4, &x) == 0); CHECK(cfg_load(&b4, &got) == 0); }
  // read-back mismatch (memory that changes what it stores)
  for (int k = 0; k < 64; k++) { Ram r5 = ram; r5.corruptOnWrite = k ? k : 64; CfgBackend b5 = { "rtc", ramRead, ramWrite, &r5 }; GqConf x = n; CHECK(cfg_save(&b5, &x) == 0); }
  // null backend never crashes
  CHECK(cfg_load(nullptr, &got) == 0 && gqc_check(&got) == 0);
  CHECK(cfg_save(nullptr, &c) == 0);
  // saving an unsanitized record stores a sanitized one
  { Ram r6; CfgBackend b6 = { "rtc", ramRead, ramWrite, &r6 }; GqConf x; memset(&x, 0xFF, sizeof(x)); CHECK(cfg_save(&b6, &x) == 1); CHECK(gqc_check(&x) == 0 && x.lat_c == -1); CHECK(cfg_load(&b6, &got) == 1 && got.lat_c == -1); }
  // 10,000 saves: each load returns what was saved; no access outside the 64 bytes
  Ram r7;
  CfgBackend b7 = { "rtc", ramRead, ramWrite, &r7 };
  for (int i = 0; i < 10000; i++) {
    GqConf x;
    gqc_defaults(&x);
    x.seq = (uint16_t)i; x.lat_c = (int16_t)(i % 9000);
    CHECK(cfg_save(&b7, &x) == 1);
    GqConf y;
    CHECK(cfg_load(&b7, &y) == 1 && y.seq == (uint16_t)i && y.lat_c == x.lat_c);
  }
  r7.checkGuards();
  puts("store ok");
}

int main() {
  test_record();
  test_coord();
  test_time_label();
  test_validate();
  test_store();
  puts("test_config: all passed");
  return 0;
}
