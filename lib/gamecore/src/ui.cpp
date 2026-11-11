// UI: HUD, dialogue box, quest postcard, bottom status band, number formatting.
// (Split out of village.cpp; chunk 2 grows this into the band, banners, curtain and cards.)
#include "world_priv.h"

static uint32_t g_since_init;
static int      g_status_band;          // 1 = bottom band visible
static uint8_t  g_status_idx;
static uint32_t g_status_acc;

// dialogue box (bottom three strips)
static struct {
  uint8_t active;
  char name[8];
  char text[40];
  int len, shown;
  uint32_t acc;
  int32_t hold;
} D;

// quest postcard animation (strips 12..14 while the hero is away)
static uint32_t s_pc_acc;
static int16_t  s_pc_sig = -1;

// ----------------------------------------------------------------- format
void fmt_uint(char* out, uint32_t v) {
  char tmp[10];
  int n = 0;
  do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 10);
  while (n) *out++ = tmp[--n];
  *out = 0;
}

// At most 4 characters plus NUL, truncating (never rounding up): 999, 1.0K, 9.9K, 10K, 999K, 1.0M, 12M, 999M, 4.2B.
void fmt_compact(char* out, uint32_t v) {
  uint32_t whole, tenth;
  char suffix;
  if (v < 1000) { fmt_uint(out, v); return; }
  if (v < 1000000u)         { suffix = 'K'; whole = v / 1000; tenth = (v / 100) % 10; }
  else if (v < 1000000000u) { suffix = 'M'; whole = v / 1000000u; tenth = (v / 100000u) % 10; }
  else                      { suffix = 'B'; whole = v / 1000000000u; tenth = (v / 100000000u) % 10; }
  int n = 0;
  if (whole >= 10) {
    if (whole >= 100) out[n++] = (char)('0' + whole / 100);
    out[n++] = (char)('0' + (whole / 10) % 10);
    out[n++] = (char)('0' + whole % 10);
  } else {
    out[n++] = (char)('0' + whole);
    out[n++] = '.';
    out[n++] = (char)('0' + tenth);
  }
  out[n++] = suffix;
  out[n] = 0;
}

void itoa_into(char* p, int v) { fmt_uint(p, v < 0 ? 0u : (uint32_t)v); }

// --------------------------------------------------------------- dialogue
void ui_say(int who, const char* text, int32_t hold_ms) {
  D.active = 1;
  str_copy(D.name, gq_npc_name(who), 8);
  str_copy(D.text, text, 40);
  D.len = str_len(D.text);
  D.shown = 0;
  D.acc = 0;
  D.hold = hold_ms;
  mark_rows(192, 48);
}

int ui_dialogue_done(void) { return !D.active; }

static void dialogue_tick(uint32_t dt) {
  if (!D.active) return;
  if (D.shown < D.len) {
    D.acc += dt;
    while (D.acc >= 45 && D.shown < D.len) { D.acc -= 45; D.shown++; mark_rows(192, 48); }
    return;
  }
  D.hold -= (int32_t)dt;
  if (D.hold <= 0) { D.active = 0; mark_rows(192, 48); }
}

// ------------------------------------------------------- stubs for chunk 2
void ui_on_event(int ev, int a, int b) { (void)ev; (void)a; (void)b; }
void ui_curtain(int close) { (void)close; }
int  ui_curtain_closed(void) { return 0; }
int  ui_curtain_covers(int strip) { (void)strip; return 0; }
int  ui_card_covers(int strip) { (void)strip; return 0; }
int  ui_busy(void) { return D.active; }
void ui_status_changed(void) { village_status_changed(); }

void village_status_changed() {
  if (g_status_band) g_dirty |= 1u << (GC_ROWS - 1);
}

void ui_init(void) {
  g_since_init = 0;
  g_status_band = 1;
  g_status_idx = 0;
  g_status_acc = 0;
  D.active = 0;
  s_pc_acc = 0;
  s_pc_sig = -1;
}

// --------------------------------------------------------------- postcard
// Triangle wave 0..255..0 of the away progress: the hero walks out and back.
static int tri(int progress) { return progress < 128 ? progress * 2 : (255 - progress) * 2; }

static int pc_hero_x(int progress) { return 24 + (176 * tri(progress) / 255 & ~1); }

static int pc_frame(void) { return (int)((s_pc_acc / 150) & 3); }

static const int PC_BOX_X = 3, PC_BOX_Y = 193, PC_BOX_W = GC_W - 6, PC_BOX_H = 45;

void ui_tick(uint32_t dt) {
  g_since_init += dt;
  dialogue_tick(dt);

  int away = hero_away_progress(0);
  if (away >= 0) {
    s_pc_acc += dt;
    int sig = pc_hero_x(away) * 4 + pc_frame() + 1;
    if (sig != s_pc_sig) { s_pc_sig = (int16_t)sig; mark_rows(192, 48); }
  } else if (s_pc_sig != -1) {
    s_pc_sig = -1;
    mark_rows(192, 48);
  }

  // bottom status band: first 90 s after boot, and whenever WiFi is down
  int want = g_since_init < 90000 || (g_status[0][0] == 'W');
  if (want != g_status_band) { g_status_band = want; g_dirty |= 1u << (GC_ROWS - 1); }
  if (g_status_band) {
    g_status_acc += dt;
    if (g_status_acc >= 3000) {
      g_status_acc = 0;
      for (int k = 0; k < 4; k++) {
        g_status_idx = (uint8_t)((g_status_idx + 1) % 4);
        if (g_status[g_status_idx][0]) break;
      }
      g_dirty |= 1u << (GC_ROWS - 1);
    }
  }
}

static void draw_frame(uint16_t* out, int y0) {
  fill_rect(out, y0, PC_BOX_X, PC_BOX_Y, PC_BOX_W, PC_BOX_H, g_ui[P_WHITE]);
  fill_rect(out, y0, PC_BOX_X + 2, PC_BOX_Y + 2, PC_BOX_W - 4, PC_BOX_H - 4, g_ui[P_OUTLINE]);
}

static void draw_postcard(uint16_t* out, int y0, int progress) {
  const int bx = PC_BOX_X, by = PC_BOX_Y;
  draw_frame(out, y0);
  int place = 0;
  hero_away_progress(&place);
  char title[20] = "QUEST: ";
  str_copy(title + 7, hero_place_name(place), 12);
  draw_text(out, y0, bx + 6, by + 4, title, g_ui[P_YELLOW], 1, 0);
  // dotted track with the hero walking along it, the place at the right end
  for (int x = 24; x <= 208; x += 4) fill_rect(out, y0, x + 8, by + 27, 2, 2, g_ui[P_GREY]);
  blit_sprite(out, y0, 212, by + 11, GC_TILES(hero_place_icon(place)), 0, 0, g_ui);
  int heading_right = progress < 128;
  static const uint8_t cycle[4] = {0, 1, 0, 2};
  blit_sprite(out, y0, pc_hero_x(progress), by + 11, GC_CHARS(HERO_CELL_BASE[DIR_LEFT] + cycle[pc_frame()]),
              heading_right, 0, g_ui);
  const char* line = progress < 85 ? "ON THE ROAD" : (progress < 170 ? "FIGHTING SLIMES!" : "HEADING HOME");
  draw_text(out, y0, bx + 6, by + 29, line, g_ui[P_WHITE], 2, 0);
}

// ----------------------------------------------------------------- render
static void draw_hud(uint16_t* out, int y0) {
  darken_rect(out, y0, 0, 0, GC_W, 16);
  char buf[12];
  // coins: at most 3 digits (the wallet cap is 999)
  blit_sprite(out, y0, 0, 0, GC_ITEMS(I_COIN), 0, 0, g_ui);
  uint32_t coins = G.s.coins > 999 ? 999u : G.s.coins;
  fmt_uint(buf, coins);
  draw_text(out, y0, 17, 1, buf, g_ui[P_YELLOW], 2, 0);
  // focus item (wood): at most 2 digits
  blit_sprite(out, y0, 56, 0, GC_ITEMS(I_WOOD), 0, 0, g_ui);
  uint32_t wood = G.s.inv[GQ_ITEM_WOOD] > 99 ? 99u : G.s.inv[GQ_ITEM_WOOD];
  fmt_uint(buf, wood);
  draw_text(out, y0, 73, 1, buf, g_ui[P_WHITE], 2, 0);
  // stars, only once there is one: at most 3 digits
  if (G.s.stars > 0) {
    blit_sprite(out, y0, 100, 0, GC_ITEMS(I_STAR), 0, 0, g_ui);
    fmt_uint(buf, G.s.stars > 999 ? 999u : G.s.stars);
    draw_text(out, y0, 117, 1, buf, g_ui[P_WHITE], 2, 0);
  }
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) {
    t[0] = (char)('0' + g_hour / 10); t[1] = (char)('0' + g_hour % 10);
    t[3] = (char)('0' + g_min / 10); t[4] = (char)('0' + g_min % 10);
  }
  draw_text(out, y0, GC_W - 5 * 12 - 2, 1, t, g_ui[P_WHITE], 2, 0);
}

static void draw_dialogue(uint16_t* out, int y0) {
  const int bx = 3, by = 193;
  draw_frame(out, y0);
  draw_text(out, y0, bx + 6, by + 4, D.name, g_ui[P_YELLOW], 1, 0);
  // text: up to two lines split at '|'
  int split = -1;
  for (int i = 0; i < D.len; i++) if (D.text[i] == '|') { split = i; break; }
  int l1 = split < 0 ? D.len : split;
  int shown1 = D.shown < l1 ? D.shown : l1;
  draw_text_n(out, y0, bx + 6, by + 13, D.text, shown1, g_ui[P_WHITE], 2);
  if (split >= 0 && D.shown > split + 1)
    draw_text_n(out, y0, bx + 6, by + 29, D.text + split + 1, D.shown - split - 1, g_ui[P_WHITE], 2);
}

static void draw_status_band(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, 16, g_ui[P_OUTLINE]);
  const char* s = g_status[g_status_idx];
  int x = (GC_W - str_len(s) * 12) / 2;
  if (x < 2) x = 2;
  draw_text(out, y0, x, y0 + 1, s, g_ui[P_WHITE], 2, 0);
}

void ui_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  if (s == 0) draw_hud(out, y0);
  int away = hero_away_progress(0);
  if (away >= 0 && s >= 12) draw_postcard(out, y0, away);
  else if (D.active && s >= 12) draw_dialogue(out, y0);
  else if (g_status_band && s == GC_ROWS - 1) draw_status_band(out, y0);
}
