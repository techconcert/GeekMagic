// Weather particles (design 6.4): rain, storm, snow and windy leaves without a framebuffer.
// A particle's position is a pure function of (index, phase); nothing is stored per frame.
// Chunk 6.
#include "world_priv.h"

static uint16_t s_phase;       // advances every 120 ms (rain) or 200 ms (snow, leaves)
static uint16_t s_acc;         // ms since the last phase step
static uint8_t  s_last_w = 0xFF;
static uint8_t  s_last_on;     // particles were visible at the last tick
static uint8_t  s_level;       // governor: 0 full, 1 half density and rate, 2 stopped
static uint8_t  s_fps_low;     // the last reported FPS was under 12
static int32_t  s_low_ms;      // ms the FPS has been under 12
static uint8_t  s_outdoors = 1;

// Particles live in the world strips 1..13 (HUD above, band below, design 6.6).
enum { FIRST_STRIP = 1, LAST_STRIP = 13, MAX_PARTICLES = 24 };

// Per weather: particle count and ms per phase.
static const uint8_t  COUNT[GQ_W_COUNT]  GC_ROM_ATTR = {0, 0, 16, 24, 0, 16, 8};
static const uint16_t PERIOD[GQ_W_COUNT] GC_ROM_ATTR = {0, 0, 120, 120, 0, 200, 200};
static const uint8_t  RAIN_SPEED[3]      GC_ROM_ATTR = {16, 24, 32};
// windy: {colour index for even, odd particles} per season
static const uint8_t  LEAF[4][2]         GC_ROM_ATTR = {{P_WHITE, P_SAND}, {4, 3}, {12, P_YELLOW}, {P_GREY, P_WHITE}};

static int hash16(int i, int salt) {
  uint32_t h = (uint32_t)(i + 1) * 2654435761u + (uint32_t)salt * 40503u;
  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  return (int)(h & 0xFFFF);
}

static int count_now(int w) {
  int n = GC_RD8(&COUNT[w]);
  if (s_level == 1) n >>= 1;
  return n > MAX_PARTICLES ? MAX_PARTICLES : n;
}

static int period_now(int w) {
  int p = GC_RD16(&PERIOD[w]);
  return s_level == 1 ? p * 2 : p;
}

static int active(void) {
  return s_level < 2 && s_outdoors && GC_RD8(&COUNT[cal_weather()]) != 0;
}

// Governor (design 6.1): under 8 fps the particles stop; under 12 fps for 5 s they halve in
// density and rate; above 18 fps everything is restored. fps_x10 <= 0 means unknown.
void weather_set_fps(int fps_x10) {
  if (fps_x10 <= 0) return;
  int old = s_level;
  s_fps_low = fps_x10 < 120;
  if (fps_x10 < 80) s_level = 2;
  else if (fps_x10 > 180) { s_level = 0; s_low_ms = 0; }
  else if (s_level == 2) s_level = 1;
  if (!s_fps_low) s_low_ms = 0;
  if (s_level != old) gc_mark_all_dirty();
}

void weather_tick(uint32_t dt) {
  if (dt > 250) dt = 250;
  int w = cal_weather();
  int area = world_area();
  s_outdoors = (area != GQ_AREA_CAVE && area < GQ_AREA_HOME) ? 1 : 0;
  if (s_fps_low && s_level == 0) {
    s_low_ms += (int32_t)dt;
    if (s_low_ms >= 5000) { s_level = 1; gc_mark_all_dirty(); }
  }
  int on = active();
  if (w != s_last_w || on != s_last_on) {      // state changed: wipe or start particles
    s_last_w = (uint8_t)w;
    s_last_on = (uint8_t)on;
    s_acc = 0;
    gc_mark_all_dirty();
    return;
  }
  if (!on) return;
  s_acc = (uint16_t)(s_acc + dt);
  int p = period_now(w);
  if (s_acc < p) return;
  s_acc = (uint16_t)(s_acc - p);               // one step per call: no catch-up
  if (s_acc >= p) s_acc = 0;
  s_phase++;
  // odd and even strips redraw on alternating phases (at most 7 strips per step)
  for (int s = FIRST_STRIP; s <= LAST_STRIP; s++)
    if ((s & 1) == (s_phase & 1)) g_dirty |= 1u << s;
}

static void put(uint16_t* out, int y0, int x, int y, uint16_t c) {
  int py = y - y0;
  if (x < 0 || x >= GC_W || py < 0 || py >= GC_STRIP_H) return;
  out[py * GC_W + x] = c;
}

void weather_render(int strip, uint16_t* out) {
  if (strip < FIRST_STRIP || strip > LAST_STRIP || !active()) return;
  int w = cal_weather();
  int n = count_now(w);
  int y0 = strip * GC_STRIP_H;
  // each strip shows the phase it was last scheduled for, so its picture does not depend on
  // when it happened to be redrawn
  int ph = s_phase;
  if ((ph & 1) != (strip & 1)) ph--;
  int p7 = ph & 127;                           // every motion below repeats after 128 phases
  uint16_t white = g_pal[P_WHITE];
  for (int i = 0; i < n; i++) {
    int x0 = (hash16(i, w) & 0xFF) * 240 / 256;
    int yb = hash16(i, 77) & 0xFF;
    if (w == GQ_W_RAIN || w == GQ_W_STORM) {
      int y = (yb + p7 * GC_RD8(&RAIN_SPEED[i % 3])) & 255;
      if (y + 4 <= y0 || y >= y0 + GC_STRIP_H) continue;
      int r = (white >> 11) & 31, g = (white >> 5) & 63, b = white & 31;
      uint16_t c = (uint16_t)(((r >> 1) << 11) | ((g * 3 / 4) << 5) | b);   // blue-ish, follows the day tint
      for (int k = 0; k < 4; k++) put(out, y0, x0, y + k, c);
    } else if (w == GQ_W_SNOW) {
      int y = (yb + p7 * ((i & 1) ? 4 : 2)) & 255;
      if (y + 2 <= y0 || y >= y0 + GC_STRIP_H) continue;
      int t = (p7 + i * 5) & 15;
      int x = x0 + (t < 8 ? t : 15 - t) - 4;
      for (int k = 0; k < 2; k++) { put(out, y0, x, y + k, white); put(out, y0, x + 1, y + k, white); }
    } else if (w == GQ_W_WINDY) {
      // leaves / petals: 3x2 pixels drifting sideways with a little bob
      int x = (x0 + p7 * ((i & 1) ? 6 : 4)) & 255;
      int t = (p7 * 2 + i * 3) & 15;
      int y = 24 + yb * 190 / 256 + (t < 8 ? t : 15 - t);
      if (y + 2 <= y0 || y >= y0 + GC_STRIP_H) continue;
      uint16_t c = g_pal[GC_RD8(&LEAF[cal_season() & 3][i & 1]) & 15];
      for (int k = 0; k < 3; k++) { put(out, y0, x + k, y, c); put(out, y0, x + k, y + 1, c); }
    }
  }
}
