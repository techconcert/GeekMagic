// Host smoke test for the game core: runs every scene for hours of simulated
// time under AddressSanitizer/UBSan, rendering every strip each frame.
// Catches out-of-bounds writes and crashes before they can reach the clock.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../lib/gamecore/src/gamecore.h"

static uint16_t strip[GC_STRIP_PIXELS + 64];

int main() {
  const int scenes[] = {GC_SCENE_TEST, GC_SCENE_VILLAGE, GC_SCENE_INFO};
  for (int sc : scenes) {
    for (uint32_t seed = 1; seed <= 5; seed++) {
      gc_init(sc, seed, seed & 1);
      gc_set_depth((int)(seed * 25) % 101);
      gc_set_status(0, "IP 192.168.100.200");
      gc_set_status(1, "AP GEEKQUEST-ABCD");
      gc_set_status(2, "A VERY LONG STATUS LINE THAT IS TOO WIDE");
      gc_set_status(3, "");
      // 1 simulated hour at ~30 fps, with irregular frame times.
      uint32_t t = 0;
      for (int f = 0; f < 3600 * 30; f++) {
        uint32_t dt = 20 + (f * 7919u) % 30;
        if (f % 5000 == 0) dt = 400;  // stall
        t += dt;
        gc_tick(dt);
        int mins = (int)((t / 1000ULL * 24 / 60) % 1440);  // a full day per simulated hour
        gc_set_clock(mins / 60, mins % 60, f > 100);
        gc_set_fps((f * 13) % 400);
        uint32_t mask = gc_dirty_mask();
        gc_clear_dirty();
        for (int s = 0; s < GC_ROWS; s++) {
          if (!(mask & (1u << s)) && f % 97) continue;
          for (int i = GC_STRIP_PIXELS; i < GC_STRIP_PIXELS + 64; i++) strip[i] = 0xBEEF;
          gc_render_strip(s, strip);
          for (int i = GC_STRIP_PIXELS; i < GC_STRIP_PIXELS + 64; i++)
            if (strip[i] != 0xBEEF) { printf("FAIL: strip overrun scene %d\n", sc); return 1; }
        }
      }
    }
    printf("scene %d ok\n", sc);
  }
  printf("PASS\n");
  return 0;
}
