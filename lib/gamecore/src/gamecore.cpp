// GeekQuest game core: public API, drawing primitives, test and info scenes.
// Rules for the core: no heap, no libc, no floating point (keeps it small and
// identical on the ESP8266 and in the WebAssembly preview).
#define GC_ASSETS_DEFINE 1   // this file owns the tile, sprite, item and font tables (assets_gen.h)
#include "gc_internal.h"

uint8_t  g_ground[GC_ROWS][GC_COLS];
uint8_t  g_obj[GC_ROWS][GC_COLS];
uint16_t g_pal[16];
uint16_t g_act[16];     // palette for actors and items (wash and tint, no season remap)
uint16_t g_ui[16];
uint32_t g_dirty;
char     g_status[4][32];
int      g_hour, g_min, g_clock_valid;

static int      g_swap;
static int      g_scene;
static uint32_t g_rng = 1;

#if GQ_TEST_SCENE
// test scene state
struct TestHero { int16_t x, y; uint8_t dir, step, moved; };
static TestHero g_th;
static uint32_t g_move_acc;
static int      g_wp;
static uint8_t  g_water_frame;
static uint32_t g_water_acc;
static int      g_status_cur;   // 0..4 (4 = fps line)
static uint32_t g_status_acc;
static int      g_fps_x10;
#endif

// ------------------------------------------------------------------ utils
uint32_t gc_rnd() {
  g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
  return g_rng;
}

uint32_t gc_rng_get(void) { return g_rng; }
void gc_rng_set(uint32_t state) { g_rng = state ? state : 0x1234567u; }
int gc_scene_id(void) { return g_scene; }

void mark_rows(int y, int h) {
  if (h <= 0) return;
  if (y < 0) { h += y; y = 0; }
  if (h <= 0 || y >= GC_H) return;
  int a = y / GC_STRIP_H, b = (y + h - 1) / GC_STRIP_H;
  if (b > GC_ROWS - 1) b = GC_ROWS - 1;
  for (int s = a; s <= b; s++) g_dirty |= (1u << s);
}

void str_copy(char* dst, const char* src, int cap) {
  int i = 0;
  if (src) for (; src[i] && i < cap - 1; i++) dst[i] = src[i];
  dst[i] = 0;
}

int str_len(const char* s) { int n = 0; while (s[n]) n++; return n; }

#if GQ_TEST_SCENE
static void fmt2(char* p, int v) { p[0] = (char)('0' + (v / 10) % 10); p[1] = (char)('0' + v % 10); }
#endif

static int g_depth;                       // 0..100
static int g_tr = 256, g_tg = 256, g_tb = 256;
static int g_season, g_wash_k;            // g_wash_k 0..255
static uint16_t g_wash_rgb;

// y = x + (x*x - x) * depth, on a 0..1023 scale. depth 0 = unchanged.
static inline int curve(int v, int maxv) {
  int x = v * 1023 / maxv;
  int y = x + ((x * x / 1023 - x) * g_depth) / 100;
  return y * maxv / 1023;
}

// Season remap (design 5.2), world palette only: SEASON_REMAP[season][palette index].
// Palette: 2 dark green, 3 green, 4 light green, 5 dark brown, 6 brown, 7 sand,
// 10 light blue (foam), 12 red, 13 white, 14 grey, 15 yellow. Index 0 and 1 never move.
static const uint8_t SEASON_REMAP[4][16] GC_ROM_ATTR = {
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},      // spring: identity
  {0, 1, 3, 4, 15, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},     // summer: warmer, dry
  {0, 1, 5, 6, 15, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},     // autumn: brown and yellow
  {0, 1, 14, 13, 10, 5, 6, 13, 8, 9, 10, 11, 12, 13, 14, 15},  // winter: grey, white, foam
};

// One channel of a colour: wash toward the wash colour, then tint, then the depth curve.
static inline int chan(int v, int maxv, int wash, int k, int tint) {
  v += (wash - v) * k / 256;
  v = v * tint >> 8;
  if (v > maxv) v = maxv;
  if (v < 0) v = 0;
  return curve(v, maxv);
}

static uint16_t make_colour(uint16_t c, int wash_on, int tinted) {
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  int k = wash_on ? g_wash_k : 0;
  int tr = tinted ? g_tr : 256, tg = tinted ? g_tg : 256, tb = tinted ? g_tb : 256;
  if (tr < 0) tr = 0;
  if (tg < 0) tg = 0;
  if (tb < 0) tb = 0;
  r = chan(r, 31, (g_wash_rgb >> 11) & 31, k, tr);
  g = chan(g, 63, (g_wash_rgb >> 5) & 63, k, tg);
  b = chan(b, 31, g_wash_rgb & 31, k, tb);
  return (uint16_t)((r << 11) | (g << 5) | b);
}

// Palette pipeline (design 6.2): base -> season remap (world only) -> wash -> tint -> depth curve.
// g_pal = world, g_act = actors and items (no season remap), g_ui = HUD and text (depth curve only).
static void build_palettes(void) {
  const uint8_t* remap = SEASON_REMAP[g_season & 3];
  for (int i = 0; i < 16; i++) {
    g_ui[i] = make_colour(GC_RD16(&GC_PALETTE[i]), 0, 0);
    g_act[i] = make_colour(GC_RD16(&GC_PALETTE[i]), 1, 1);
    g_pal[i] = make_colour(GC_RD16(&GC_PALETTE[GC_RD8(&remap[i]) & 15]), 1, 1);
  }
  g_pal[0] = g_ui[0] = g_act[0] = 0;
}

void set_tint(int r256, int g256, int b256) {
  g_tr = r256; g_tg = g256; g_tb = b256;
  build_palettes();
}

// Look = palette inputs for this moment (design 6.2). Quantised: tint to 3 bits below the
// 256 scale (steps of 8, about 5 bits), wash strength to 4 bits. Marks every strip dirty
// only when the quantised look changed.
static int32_t  g_look_key = -1;
static uint16_t g_look_wash = 0;
void gfx_set_look(const GqLook* look) {
  int r = look->tr, g = look->tg, b = look->tb;
  int season = look->season & 3;
  int wk = look->wash_k >> 4;                           // 0..15
  uint16_t wrgb = wk ? look->wash_rgb565 : 0;
  int32_t key = (int32_t)((((r >> 3) & 63) << 12) | (((g >> 3) & 63) << 6) | ((b >> 3) & 63)) |
                ((int32_t)season << 18) | ((int32_t)wk << 20);
  if (key == g_look_key && wrgb == g_look_wash) return;
  g_look_key = key;
  g_look_wash = wrgb;
  g_season = season;
  g_wash_k = wk == 15 ? 256 : wk * 17;                  // 0..256 (256 = the wash colour exactly)
  g_wash_rgb = wrgb;
  g_tr = r; g_tg = g; g_tb = b;
  build_palettes();
  gc_mark_all_dirty();
}

extern "C" void gc_set_depth(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  if (percent == g_depth) return;
  g_depth = percent;
  build_palettes();
  gc_mark_all_dirty();
}

// --------------------------------------------------------------- drawing
gc_cell sheet_cell(int sheet, int cell) {
  if (cell < 0) cell = 0;
  if (sheet == SH_CHARS) return GC_CHARS(cell < GC_CHARS_COUNT ? cell : 0);
  if (sheet == SH_ITEMS) return GC_ITEMS(cell < GC_ITEMS_COUNT ? cell : 0);
  return GC_TILES(cell < GC_TILES_COUNT ? cell : 0);
}

void blit_tile(uint16_t* out, int x, gc_cell tile, int transparent) {
  for (int r = 0; r < 16; r++) {
    uint16_t* dst = out + r * GC_W + x;
    const uint8_t* src = GC_ROWTAB[GC_RD16(&tile[r])];
    for (int b = 0; b < 8; b++) {
      uint8_t v = GC_RD8(src + b);
      uint8_t p0 = v >> 4, p1 = v & 15;
      if (!transparent || p0) dst[0] = g_pal[p0];
      if (!transparent || p1) dst[1] = g_pal[p1];
      dst += 2;
    }
  }
}

void blit_sprite(uint16_t* out, int y0, int sx, int sy, gc_cell cell, int flip, const uint8_t* remap, const uint16_t* pal) {
  for (int r = 0; r < 16; r++) {
    int py = sy + r - y0;
    if (py < 0 || py >= GC_STRIP_H) continue;
    const uint8_t* src = GC_ROWTAB[GC_RD16(&cell[r])];
    for (int c = 0; c < 16; c++) {
      int px = sx + (flip ? 15 - c : c);
      if (px < 0 || px >= GC_W) continue;
      uint8_t v = GC_RD8(src + (c >> 1));
      uint8_t p = (c & 1) ? (v & 15) : (v >> 4);
      if (!p) continue;
      if (remap) p = remap[p];
      out[py * GC_W + px] = pal[p];
    }
  }
}

void fill_rect(uint16_t* out, int y0, int x, int y, int w, int h, uint16_t c) {
  int ya = y - y0, yb = ya + h;
  if (ya < 0) ya = 0;
  if (yb > GC_STRIP_H) yb = GC_STRIP_H;
  int xa = x < 0 ? 0 : x, xb = x + w > GC_W ? GC_W : x + w;
  for (int yy = ya; yy < yb; yy++)
    for (int xx = xa; xx < xb; xx++) out[yy * GC_W + xx] = c;
}

void darken_rect(uint16_t* out, int y0, int x, int y, int w, int h) {
  int ya = y - y0, yb = ya + h;
  if (ya < 0) ya = 0;
  if (yb > GC_STRIP_H) yb = GC_STRIP_H;
  int xa = x < 0 ? 0 : x, xb = x + w > GC_W ? GC_W : x + w;
  for (int yy = ya; yy < yb; yy++)
    for (int xx = xa; xx < xb; xx++) {
      uint16_t c = out[yy * GC_W + xx];
      out[yy * GC_W + xx] = (uint16_t)((c >> 2) & 0x39E7);  // ~25% brightness
    }
}

static void draw_char(uint16_t* out, int y0, int x, int y, char ch, uint16_t c, int scale) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  if (ch < GC_FONT_FIRST || ch > GC_FONT_LAST) ch = '?';
  const uint8_t* g = GC_FONT[ch - GC_FONT_FIRST];
  for (int gy = 0; gy < 7; gy++) {
    int yy = y + gy * scale;
    if (yy + scale <= y0 || yy >= y0 + GC_STRIP_H) continue;
    uint8_t bits = GC_RD8(g + gy);
    if (!bits) continue;
    for (int gx = 0; gx < 5; gx++)
      if (bits & (0x10 >> gx))
        fill_rect(out, y0, x + gx * scale, yy, scale, scale, c);
  }
}

void draw_text_n(uint16_t* out, int y0, int x, int y, const char* s, int n, uint16_t c, int scale) {
  if (y + 7 * scale <= y0 || y >= y0 + GC_STRIP_H) return;
  for (int i = 0; s[i] && i < n; i++) draw_char(out, y0, x + i * 6 * scale, y, s[i], c, scale);
}

void draw_text(uint16_t* out, int y0, int x, int y, const char* s, uint16_t c, int scale, int shadow) {
  if (y + 8 * scale <= y0 || y >= y0 + GC_STRIP_H) return;
  for (int i = 0; s[i]; i++) {
    int cx = x + i * 6 * scale;
    if (shadow) draw_char(out, y0, cx + scale, y + scale, s[i], 0x0000, scale);
    draw_char(out, y0, cx, y, s[i], c, scale);
  }
}

void render_ground_row(uint16_t* out, int s, uint8_t water_frame, uint8_t fire_frame) {
  for (int tx = 0; tx < GC_COLS; tx++) {
    uint8_t g = g_ground[s][tx];
    if (g == T_WATER && water_frame) g = T_WATER2;
    blit_tile(out, tx * GC_TILE, GC_TILES(g), 0);
    uint8_t o = g_obj[s][tx];
    if (o == T_FIRE && fire_frame) o = T_FIRE2;
    if (o != T_NONE) blit_tile(out, tx * GC_TILE, GC_TILES(o), 1);
  }
}

// ------------------------------------------------------------ test scene
#if GQ_TEST_SCENE
static void build_test_map() {
  for (int y = 0; y < GC_ROWS; y++)
    for (int x = 0; x < GC_COLS; x++) {
      uint32_t r = gc_rnd() % 10;
      g_ground[y][x] = r < 6 ? T_GRASS : (r < 9 ? T_TUFT : T_FLOWER);
      g_obj[y][x] = T_NONE;
    }
  for (int i = 2; i <= 12; i++) {
    g_ground[2][i] = g_ground[12][i] = T_PATH;
    g_ground[i][2] = g_ground[i][12] = T_PATH;
  }
  for (int y = 6; y <= 8; y++)
    for (int x = 6; x <= 8; x++) g_ground[y][x] = T_WATER;
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

static const int16_t WAYPOINTS[][2] = {
  {2 * 16, 2 * 16}, {12 * 16, 2 * 16}, {12 * 16, 12 * 16}, {2 * 16, 12 * 16}
};
#define WP_COUNT 4

static void test_init() {
  build_test_map();
  g_th.x = WAYPOINTS[0][0];
  g_th.y = WAYPOINTS[0][1];
  g_th.dir = DIR_RIGHT;
  g_th.step = g_th.moved = 0;
  g_wp = 1;
  g_move_acc = g_water_acc = 0;
  g_water_frame = 0;
}

static void test_tick(uint32_t dt_ms) {
  g_move_acc += dt_ms;
  while (g_move_acc >= 25) {
    g_move_acc -= 25;
    int tx = WAYPOINTS[g_wp][0], ty = WAYPOINTS[g_wp][1];
    if (g_th.x == tx && g_th.y == ty) { g_wp = (g_wp + 1) % WP_COUNT; continue; }
    int dx = tx > g_th.x ? 1 : (tx < g_th.x ? -1 : 0);
    int dy = ty > g_th.y ? 1 : (ty < g_th.y ? -1 : 0);
    mark_rows(g_th.y, 16);
    if (dx) { g_th.x += dx; g_th.dir = dx > 0 ? DIR_RIGHT : DIR_LEFT; }
    else if (dy) { g_th.y += dy; g_th.dir = dy > 0 ? DIR_DOWN : DIR_UP; }
    if (++g_th.moved >= 6) { g_th.moved = 0; g_th.step = (g_th.step + 1) & 3; }
    mark_rows(g_th.y, 16);
  }
  g_water_acc += dt_ms;
  if (g_water_acc >= 600) {
    g_water_acc -= 600;
    g_water_frame ^= 1;
    for (int y = 0; y < GC_ROWS; y++)
      for (int x = 0; x < GC_COLS; x++)
        if (g_ground[y][x] == T_WATER) { g_dirty |= 1u << y; break; }
  }
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

static void draw_test_top(uint16_t* out, int y0) {
  // Colour-order check: these three bars must read RED, GREEN, BLUE on the clock.
  fill_rect(out, y0, 0, 0, 30, 16, 0xF800);
  fill_rect(out, y0, 30, 0, 30, 16, 0x07E0);
  fill_rect(out, y0, 60, 0, 30, 16, 0x001F);
  draw_text(out, y0, 12, 4, "R", 0xFFFF, 1, 0);
  draw_text(out, y0, 42, 4, "G", 0x0000, 1, 0);
  draw_text(out, y0, 72, 4, "B", 0xFFFF, 1, 0);
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) { fmt2(t, g_hour); fmt2(t + 3, g_min); }
  draw_text(out, y0, GC_W - 5 * 12 - 4, 1, t, 0xFFFF, 2, 1);
}

static void draw_test_bottom(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, 16, g_ui[P_OUTLINE]);
  char buf[32];
  if (g_status_cur == 4) {
    int f = g_fps_x10, n = 4, whole = f / 10;
    buf[0] = 'F'; buf[1] = 'P'; buf[2] = 'S'; buf[3] = ' ';
    if (whole >= 100) buf[n++] = (char)('0' + (whole / 100) % 10);
    if (whole >= 10) buf[n++] = (char)('0' + (whole / 10) % 10);
    buf[n++] = (char)('0' + whole % 10);
    buf[n++] = '.';
    buf[n++] = (char)('0' + f % 10);
    buf[n] = 0;
  } else {
    str_copy(buf, g_status[g_status_cur], 32);
  }
  int x = (GC_W - str_len(buf) * 12) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + 1, buf, g_ui[P_WHITE], 2, 0);
}

static void test_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  render_ground_row(out, s, g_water_frame, 0);
  if (g_th.y < y0 + GC_STRIP_H && g_th.y + 16 > y0) {
    static const uint8_t cycle[4] = {0, 1, 0, 2};
    int row = g_th.dir == DIR_RIGHT ? (int)DIR_LEFT : (int)g_th.dir;
    blit_sprite(out, y0, g_th.x, g_th.y, GC_CHARS(HERO_CELL_BASE[row] + cycle[g_th.step]), g_th.dir == DIR_RIGHT, 0, g_act);
  }
  if (s == 0) draw_test_top(out, y0);
  if (s == GC_ROWS - 1) draw_test_bottom(out, y0);
}

#endif  // GQ_TEST_SCENE

// ------------------------------------------------------------ info scene
static void info_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  fill_rect(out, y0, 0, y0, GC_W, GC_STRIP_H, g_ui[P_OUTLINE]);
  const char* t = 0;
  uint16_t c = g_ui[P_WHITE];
  if (s == 1) { t = "SAFE MODE"; c = 0xFB20; }
  else if (s >= 4 && s <= 10 && !(s & 1)) t = g_status[(s - 4) / 2];
  else if (s == 13) { t = "GAME PAUSED"; c = g_ui[P_GREY]; }
  if (!t) return;
  int x = (GC_W - str_len(t) * 12) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + 1, t, c, 2, 0);
}

// ------------------------------------------------------------------- API
extern "C" void gc_init(int scene, uint32_t seed, int swap_bytes) {
  g_scene = scene;
  g_swap = swap_bytes;
  g_rng = seed ? seed : 0x1234567u;
  g_season = 0; g_wash_k = 0; g_wash_rgb = 0;
  set_tint(256, 256, 256);
  g_look_key = -1;
  g_look_wash = 0;
  if (scene == GC_SCENE_VILLAGE) G.card_period = 10;   // default: one info card every 10 minutes
#if GQ_TEST_SCENE
  g_status_cur = 0;
  g_status_acc = 0;
#endif
  if (scene == GC_SCENE_VILLAGE) village_init(seed);
#if GQ_TEST_SCENE
  else if (scene == GC_SCENE_TEST) test_init();
#endif
  gc_mark_all_dirty();
}

extern "C" void gc_tick(uint32_t dt_ms) {
  if (dt_ms > 250) dt_ms = 250;  // after a stall don't teleport
  if (g_scene == GC_SCENE_INFO) return;
  if (g_scene == GC_SCENE_VILLAGE) village_tick(dt_ms);
#if GQ_TEST_SCENE
  else if (g_scene == GC_SCENE_TEST) test_tick(dt_ms);
#endif
}

extern "C" void gc_set_clock(int hour, int minute, int valid) {
  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) valid = 0;
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
  else if (g_scene == GC_SCENE_VILLAGE) village_status_changed();
#if GQ_TEST_SCENE
  else if (g_status_cur == line) g_dirty |= 1u << (GC_ROWS - 1);
#endif
}

extern "C" void gc_set_fps(int fps_x10) {
#if GQ_TEST_SCENE
  if (fps_x10 != g_fps_x10) {
    g_fps_x10 = fps_x10;
    if (g_scene == GC_SCENE_TEST && g_status_cur == 4) g_dirty |= 1u << (GC_ROWS - 1);
  }
#endif
  weather_set_fps(fps_x10);   // governor input (design 6.1)
}

extern "C" uint32_t gc_dirty_mask(void) { return g_dirty; }
extern "C" void gc_clear_dirty(void) { g_dirty = 0; }
extern "C" void gc_mark_all_dirty(void) { g_dirty = (1u << GC_ROWS) - 1; }

extern "C" void gc_render_strip(int s, uint16_t* out) {
  if (s < 0 || s >= GC_ROWS) return;
  if (g_scene == GC_SCENE_INFO) info_render(s, out);
  else if (g_scene == GC_SCENE_VILLAGE) village_render(s, out);
#if GQ_TEST_SCENE
  else if (g_scene == GC_SCENE_TEST) test_render(s, out);
#endif
  else info_render(s, out);
  if (g_swap)
    for (int i = 0; i < GC_STRIP_PIXELS; i++) out[i] = (uint16_t)((out[i] << 8) | (out[i] >> 8));
}
