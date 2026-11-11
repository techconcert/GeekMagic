// Shared helpers for the host tests (header only).
//   sim_start(seed)      start a village game at 08:00 wall clock
//   sim_minutes(n)       advance n simulated minutes (250 ms ticks)
//   sim_days(n)          advance n simulated days
//   sim_seconds_cb(n,f)  advance n seconds, calling f() after every simulated second
//   frame_hash()         FNV-1a over all 15 strips as currently rendered
//   CHECK(cond)          abort with file and line when false
// The wall clock is fed through gc_set_clock every simulated minute. Every
// 97th frame all strips are rendered with an overrun guard behind each strip.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../lib/gamecore/src/gamecore.h"

#define CHECK(cond) do { if (!(cond)) { std::printf("CHECK FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); std::exit(1); } } while (0)

namespace gqt {
static uint16_t strip[GC_STRIP_PIXELS + 64];
static uint32_t frame = 0;
static uint32_t ms_in_minute = 0;   // simulated ms inside the current wall minute
static int      wall_min = 8 * 60;  // wall clock minute of day
static uint32_t rendered_frames = 0;

static inline void render_strip_guarded(int s) {
  for (int i = GC_STRIP_PIXELS; i < GC_STRIP_PIXELS + 64; i++) strip[i] = 0xBEEF;
  gc_render_strip(s, strip);
  for (int i = GC_STRIP_PIXELS; i < GC_STRIP_PIXELS + 64; i++)
    if (strip[i] != 0xBEEF) { std::printf("FAIL: strip overrun, strip %d\n", s); std::exit(1); }
}

static inline void feed_clock() { gc_set_clock(wall_min / 60, wall_min % 60, 1); }
}  // namespace gqt

// One 250 ms step of the whole device: tick, clock, occasional render.
static inline void sim_step() {
  using namespace gqt;
  gc_tick(250);
  ms_in_minute += 250;
  if (ms_in_minute >= 60000) {
    ms_in_minute -= 60000;
    wall_min = (wall_min + 1) % 1440;
    feed_clock();
  }
  frame++;
  if (frame % 97 == 0) {
    for (int s = 0; s < GC_ROWS; s++) render_strip_guarded(s);
    gc_clear_dirty();
    rendered_frames++;
  }
}

static inline void sim_start(uint32_t seed, int start_minute = 8 * 60) {
  using namespace gqt;
  gc_init(GC_SCENE_VILLAGE, seed, 0);
  frame = 0; ms_in_minute = 0; wall_min = start_minute; rendered_frames = 0;
  feed_clock();
}

static inline void sim_minutes(int n) { for (long i = 0; i < (long)n * 240; i++) sim_step(); }
static inline void sim_days(int n) { sim_minutes(n * 1440); }
template <class F> static inline void sim_seconds_cb(long n, F cb) {
  for (long i = 0; i < n; i++) { for (int k = 0; k < 4; k++) sim_step(); cb(); }
}

static inline uint32_t frame_hash() {
  uint32_t h = 2166136261u;
  for (int s = 0; s < GC_ROWS; s++) {
    gqt::render_strip_guarded(s);
    for (int i = 0; i < GC_STRIP_PIXELS; i++) {
      h = (h ^ (gqt::strip[i] & 0xFF)) * 16777619u;
      h = (h ^ (gqt::strip[i] >> 8)) * 16777619u;
    }
  }
  return h;
}
