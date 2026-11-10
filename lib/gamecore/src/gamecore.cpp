// GeekQuest game core. See gamecore.h.
// Rules for this file: no heap, no libc, no floating point (keeps it small
// and identical on the ESP8266 and in the WebAssembly preview).
#include "gamecore.h"
#include "assets_gen.h"

// ---- tile ids in assets/src/tiles.png ----
enum {
  T_GRASS = 0, T_TUFT = 1, T_FLOWER = 2, T_PATH = 3,
  T_WATER = 4, T_WATER2 = 5, T_TREE = 6, T_ROCK = 7,
  T_NONE = 0xFF
};
// ---- hero sheet rows ----
enum { DIR_DOWN = 0, DIR_UP = 1, DIR_LEFT = 2, DIR_RIGHT = 3 };

struct Sprite {
  int16_t x, y;      // top-left pixel
  uint8_t dir;
  uint8_t step;      // walk-cycle position 0..3
  uint8_t visible;
  uint8_t moved_px;  // pixels since last step change
};

static uint8_t  g_ground[GC_ROWS][GC_COLS];
static uint8_t  g_obj[GC_ROWS][GC_COLS];
static uint16_t g_pal[16];
static int      g_swap;
static uint32_t g_dirty;
static int      g_scene;
static uint32_t g_rng = 1;

static Sprite   g_hero;
static uint32_t g_move_acc;
static int      g_wp;           // current waypoint index

static uint8_t  g_water_frame;
static uint32_t g_water_acc;

static char     g_status[4][32];
static int      g_status_cur;   // 0..4 (4 = fps line)
static uint32_t g_status_acc;
static int      g_hour, g_min, g_clock_valid;
static int      g_fps_x10;

// ------------------------------------------------------------------ utils
static uint32_t rnd() {
  g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
  return g_rng;
}

static inline uint16_t col(uint16_t c) {
  return g_swap ? (uint16_t)((c << 8) | (c >> 8)) : c;
}

static void mark_rows(int y, int h) {
  if (h <= 0) return;
  int a = y / GC_STRIP_H, b = (y + h - 1) / GC_STRIP_H;
  if (a < 0) a = 0;
  if (b > GC_ROWS - 1) b = GC_ROWS - 1;
  for (int s = a; s <= b; s++) g_dirty |= (1u << s);
}

static void str_copy(char* dst, const char* src, int cap) {
  int i = 0;
  if (src) for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
  dst[i] = 0;
}

static int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }

static void fmt2(char* p, int v) { p[0] = (char)('0' + (v / 10) % 10); p[1] = (char)('0' + v % 10); }

static void load_palette() {
  for (int i = 0; i < 16; i++) g_pal[i] = col(GC_RD16(&GC_PALETTE[i]));
  g_pal[0] = 0;  // transparent index never drawn opaque, but keep it black
}

// --------------------------------------------------------------- drawing
static void blit_tile(uint16_t* out, int x, const uint8_t* tile, int transparent) {
  for (int r = 0; r < 16; r++) {
    uint16_t* dst = out + r * GC_W + x;
    const uint8_t* src = tile + r * 8;
    for (int b = 0; b < 8; b++) {
      uint8_t v = GC_RD8(src + b);
      uint8_t p0 = v >> 4, p1 = v & 15;
      if (!transparent || p0) dst[0] = g_pal[p0];
      if (!transparent || p1) dst[1] = g_pal[p1];
      dst += 2;
    }
  }
}

static void blit_sprite(uint16_t* out, int y0, int sx, int sy, const uint8_t* cell, int flip) {
  for (int r = 0; r < 16; r++) {
    int py = sy + r - y0;
    if (py < 0 || py >= GC_STRIP_H) continue;
    const uint8_t* src = cell + r * 8;
    for (int c = 0; c < 16; c++) {
      int px = sx + (flip ? 15 - c : c);
      if (px < 0 || px >= GC_W) continue;
      uint8_t v = GC_RD8(src + (c >> 1));
      uint8_t p = (c & 1) ? (v & 15) : (v >> 4);
      if (p) out[py * GC_W + px] = g_pal[p];
    }
  }
}

static void fill_rect(uint16_t* out, int y0, int x, int y, int w, int h, uint16_t c) {
  int ya = y - y0, yb = ya + h;
  if (ya < 0) ya = 0;
  if (yb > GC_STRIP_H) yb = GC_STRIP_H;
  int xa = x < 0 ? 0 : x, xb = x + w > GC_W ? GC_W : x + w;
  for (int yy = ya; yy < yb; yy++)
    for (int xx = xa; xx < xb; xx++) out[yy * GC_W + xx] = c;
}

static void draw_char(uint16_t* out, int y0, int x, int y, char ch, uint16_t c, int scale) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  if (ch < GC_FONT_FIRST || ch > GC_FONT_LAST) ch = '?';
  const uint8_t* g = GC_FONT[ch - GC_FONT_FIRST];
  for (int gy = 0; gy < 7; gy++) {
    uint8_t bits = GC_RD8(g + gy);
    if (!bits) continue;
    for (int gx = 0; gx < 5; gx++)
      if (bits & (0x10 >> gx))
        fill_rect(out, y0, x + gx * scale, y + gy * scale, scale, scale, c);
  }
}

static void draw_text(uint16_t* out, int y0, int x, int y, const char* s, uint16_t c, int scale, int shadow) {
  for (int i = 0; s[i]; i++) {
    int cx = x + i * 6 * scale;
    if (shadow) draw_char(out, y0, cx + scale, y + scale, s[i], col(0x0000), scale);
    draw_char(out, y0, cx, y, s[i], c, scale);
  }
}

// ------------------------------------------------------------- the world
static void build_test_map() {
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      uint32_t r = rnd() % 10;
      g_ground[y][x] = r < 6 ? T_GRASS : (r < 9 ? T_TUFT : T_FLOWER);
      g_obj[y][x] = T_NONE;
    }
  // path ring rows/cols 2..12
  for (int i = 2; i <= 12; i++) {
    g_ground[2][i] = g_ground[12][i] = T_PATH;
    g_ground[i][2] = g_ground[i][12] = T_PATH;
  }
  // pond in the middle
  for (int y = 6; y <= 8; y++)
    for (int x = 6; x <= 8; x++) g_ground[y][x] = T_WATER;
  // trees and rocks around the edges
  static const uint8_t trees[][2] = {
    {1, 4}, {1, 7}, {1, 10}, {13, 1}, {13, 4}, {13, 9}, {13, 13},
    {4, 0}, {8, 0}, {11, 14}, {5, 14}, {4, 4}, {10, 10}, {4, 10}
  };
  for (unsigned i = 0; i < sizeof(trees) / sizeof(trees[0]); i++)
    g_obj[trees[i][0]][trees[i][1]] = T_TREE;
  g_obj[10][4] = T_ROCK;
  g_obj[5][9] = T_ROCK;
  g_obj[9][13] = T_ROCK;
}

// Test-scene loop the hero walks (pixel coords of tile corners).
static const int16_t WAYPOINTS[][2] = {
  {2 * 16, 2 * 16}, {12 * 16, 2 * 16}, {12 * 16, 12 * 16}, {2 * 16, 12 * 16}
};
#define WP_COUNT 4

static void hero_step_towards(int tx, int ty) {
  int dx = tx > g_hero.x ? 1 : (tx < g_hero.x ? -1 : 0);
  int dy = ty > g_hero.y ? 1 : (ty < g_hero.y ? -1 : 0);
  mark_rows(g_hero.y, 16);
  if (dx) { g_hero.x += dx; g_hero.dir = dx > 0 ? DIR_RIGHT : DIR_LEFT; }
  else if (dy) { g_hero.y += dy; g_hero.dir = dy > 0 ? DIR_DOWN : DIR_UP; }
  if (++g_hero.moved_px >= 6) { g_hero.moved_px = 0; g_hero.step = (g_hero.step + 1) & 3; }
  mark_rows(g_hero.y, 16);
}

// ------------------------------------------------------------------- API
extern "C" void gc_init(int scene, uint32_t seed, int swap_bytes) {
  g_scene = scene;
  g_swap = swap_bytes;
  g_rng = seed ? seed : 0x1234567u;
  load_palette();
  build_test_map();
  g_hero.x = WAYPOINTS[0][0];
  g_hero.y = WAYPOINTS[0][1];
  g_hero.dir = DIR_RIGHT;
  g_hero.step = 0;
  g_hero.visible = 1;
  g_hero.moved_px = 0;
  g_wp = 1;
  g_move_acc = g_water_acc = g_status_acc = 0;
  g_water_frame = 0;
  g_status_cur = 0;
  gc_mark_all_dirty();
}

extern "C" void gc_tick(uint32_t dt_ms) {
  if (dt_ms > 250) dt_ms = 250;  // after a stall don't teleport
  if (g_scene == GC_SCENE_INFO) return;

  // hero: 40 px/s
  g_move_acc += dt_ms;
  while (g_move_acc >= 25) {
    g_move_acc -= 25;
    int tx = WAYPOINTS[g_wp][0], ty = WAYPOINTS[g_wp][1];
    if (g_hero.x == tx && g_hero.y == ty) g_wp = (g_wp + 1) % WP_COUNT;
    else hero_step_towards(tx, ty);
  }

  // water animation: 2 frames, 600 ms each
  g_water_acc += dt_ms;
  if (g_water_acc >= 600) {
    g_water_acc -= 600;
    g_water_frame ^= 1;
    for (int y = 0; y < GC_ROWS; y++)
      for (int x = 0; x < GC_COLS; x++)
        if (g_ground[y][x] == T_WATER) { g_dirty |= 1u << y; break; }
  }

  // bottom status bar rotation every 3 s
  g_status_acc += dt_ms;
  if (g_status_acc >= 3000) {
    g_status_acc -= 3000;
    for (int i = 0; i < 5; i++) {
      g_status_cur = (g_status_cur + 1) % 5;
      if (g_status_cur == 4 || g_status[g_status_cur][0]) break;
    }
    g_dirty |= 1u << (GC_ROWS - 1);
  }
}

extern "C" void gc_set_clock(int hour, int minute, int valid) {
  if (hour != g_hour || minute != g_min || valid != g_clock_valid) {
    g_hour = hour; g_min = minute; g_clock_valid = valid;
    g_dirty |= 1u;
  }
}

extern "C" void gc_set_status(int line, const char* text) {
  if (line < 0 || line > 3) return;
  int same = 1;
  for (int i = 0; i < 32; i++) {
    char a = g_status[line][i], b = text ? text[i] : 0;
    if (a != b) { same = 0; break; }
    if (!a) break;
  }
  if (same) return;
  str_copy(g_status[line], text, 32);
  if (g_scene == GC_SCENE_INFO) g_dirty |= 1u << (4 + line * 2);
  else if (g_status_cur == line) g_dirty |= 1u << (GC_ROWS - 1);
}

extern "C" void gc_set_fps(int fps_x10) {
  if (fps_x10 != g_fps_x10) {
    g_fps_x10 = fps_x10;
    if (g_status_cur == 4) g_dirty |= 1u << (GC_ROWS - 1);
  }
}

extern "C" uint32_t gc_dirty_mask(void) { return g_dirty; }
extern "C" void gc_clear_dirty(void) { g_dirty = 0; }
extern "C" void gc_mark_all_dirty(void) { g_dirty = (1u << GC_ROWS) - 1; }

static void draw_top_hud(uint16_t* out, int y0) {
  // Colour-order check: these three bars must read RED, GREEN, BLUE on the clock.
  fill_rect(out, y0, 0, 0, 30, 16, col(0xF800));
  fill_rect(out, y0, 30, 0, 30, 16, col(0x07E0));
  fill_rect(out, y0, 60, 0, 30, 16, col(0x001F));
  draw_text(out, y0, 12, 4, "R", col(0xFFFF), 1, 0);
  draw_text(out, y0, 42, 4, "G", col(0x0000), 1, 0);
  draw_text(out, y0, 72, 4, "B", col(0xFFFF), 1, 0);
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) { fmt2(t, g_hour); fmt2(t + 3, g_min); }
  draw_text(out, y0, GC_W - 5 * 12 - 4, 1, t, col(0xFFFF), 2, 1);
}

static void draw_bottom_hud(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, 16, g_pal[1]);
  char buf[32];
  if (g_status_cur == 4) {
    // "FPS 12.3"
    int f = g_fps_x10;
    buf[0] = 'F'; buf[1] = 'P'; buf[2] = 'S'; buf[3] = ' ';
    int n = 4;
    int whole = f / 10;
    if (whole >= 100) buf[n++] = (char)('0' + (whole / 100) % 10);
    if (whole >= 10) buf[n++] = (char)('0' + (whole / 10) % 10);
    buf[n++] = (char)('0' + whole % 10);
    buf[n++] = '.';
    buf[n++] = (char)('0' + f % 10);
    buf[n] = 0;
  } else {
    str_copy(buf, g_status[g_status_cur], 32);
  }
  int w = str_len(buf) * 12;
  int x = (GC_W - w) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + 1, buf, g_pal[13], 2, 0);
}

static void render_info_strip(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  fill_rect(out, y0, 0, y0, GC_W, GC_STRIP_H, g_pal[1]);
  const char* t = 0;
  uint16_t c = g_pal[13];
  if (s == 1) { t = "SAFE MODE"; c = col(0xFB20); }
  else if (s >= 4 && s <= 10 && !(s & 1)) t = g_status[(s - 4) / 2];
  else if (s == 13) { t = "GAME PAUSED"; c = g_pal[14]; }
  if (!t) return;
  int x = (GC_W - str_len(t) * 12) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + 1, t, c, 2, 0);
}

extern "C" void gc_render_strip(int s, uint16_t* out) {
  if (s < 0 || s >= GC_ROWS) return;
  if (g_scene == GC_SCENE_INFO) { render_info_strip(s, out); return; }
  int y0 = s * GC_STRIP_H;
  for (int tx = 0; tx < GC_COLS; tx++) {
    uint8_t g = g_ground[s][tx];
    if (g == T_WATER && g_water_frame) g = T_WATER2;
    blit_tile(out, tx * GC_TILE, GC_TILES[g], 0);
    uint8_t o = g_obj[s][tx];
    if (o != T_NONE) blit_tile(out, tx * GC_TILE, GC_TILES[o], 1);
  }
  if (g_hero.visible && g_hero.y < y0 + GC_STRIP_H && g_hero.y + 16 > y0) {
    static const uint8_t cycle[4] = {0, 1, 0, 2};
    int row = g_hero.dir == DIR_RIGHT ? (int)DIR_LEFT : (int)g_hero.dir;
    int cell = row * GC_HERO_COLS + cycle[g_hero.step];
    blit_sprite(out, y0, g_hero.x, g_hero.y, GC_HERO[cell], g_hero.dir == DIR_RIGHT);
  }
  if (s == 0) draw_top_hud(out, y0);
  if (s == GC_ROWS - 1) draw_bottom_hud(out, y0);
  (void)g_scene;
}
