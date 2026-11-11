// Number formatting vectors of design 3.7 (fmt_compact, fmt_uint).
#include "gq_test.h"
#include "../lib/gamecore/src/gq_mod.h"

struct Vec { uint32_t in; const char* out; };

int main() {
  static const Vec V[] = {
    {0, "0"}, {999, "999"}, {1000, "1.0K"}, {1234, "1.2K"}, {2831, "2.8K"}, {9999, "9.9K"},
    {10000, "10K"}, {999999, "999K"}, {1000000, "1.0M"}, {12345678, "12M"}, {999999999, "999M"},
    {4294967295u, "4.2B"},
    // more boundaries: truncation, never rounding up
    {1, "1"}, {1999, "1.9K"}, {99999, "99K"}, {100000, "100K"}, {9999999, "9.9M"},
    {10000000, "10M"}, {99999999, "99M"}, {100000000, "100M"}, {1000000000u, "1.0B"},
  };
  char buf[16];
  for (unsigned i = 0; i < sizeof(V) / sizeof(V[0]); i++) {
    fmt_compact(buf, V[i].in);
    if (std::strcmp(buf, V[i].out) != 0) { std::printf("fmt_compact(%u) = '%s', want '%s'\n", (unsigned)V[i].in, buf, V[i].out); return 1; }
  }
  // never more than 4 characters, for a spread of values
  uint32_t x = 12345;
  for (int i = 0; i < 100000; i++) {
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    uint32_t v = x >> (x & 31);
    fmt_compact(buf, v);
    CHECK(std::strlen(buf) <= 4);
  }
  fmt_uint(buf, 0);          CHECK(!std::strcmp(buf, "0"));
  fmt_uint(buf, 2831);       CHECK(!std::strcmp(buf, "2831"));
  fmt_uint(buf, 4294967295u); CHECK(!std::strcmp(buf, "4294967295"));
  std::printf("PASS\n");
  return 0;
}
