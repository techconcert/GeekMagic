// UI: HUD, dialogue box, bottom status band. (Split out of village.cpp.)
#include "world_priv.h"

static const char* const NAMES[A_COUNT] = {"HERO", "MIRA", "BRAM", "PIP"};

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

void ui_say(int who, const char* text, int32_t hold_ms) {
  D.active = 1;
  str_copy(D.name, NAMES[who], 8);
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

void itoa_into(char* p, int v) {
  char tmp[8];
  int n = 0;
  if (v < 0) v = 0;
  do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v && n < 7);
  while (n) *p++ = tmp[--n];
  *p = 0;
}

void village_status_changed() {
  if (g_status_band) g_dirty |= 1u << (GC_ROWS - 1);
}

void ui_init(void) {
  g_since_init = 0;
  g_status_band = 1;
  g_status_idx = 0;
  g_status_acc = 0;
  D.active = 0;
}

void ui_tick(uint32_t dt) {
  g_since_init += dt;
  dialogue_tick(dt);

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

static void draw_hud(uint16_t* out, int y0) {
  darken_rect(out, y0, 0, 0, GC_W, 16);
  char buf[12];
  blit_sprite(out, y0, 0, 0, GC_ITEMS(I_WOOD), 0, 0);
  itoa_into(buf, g_wood);
  draw_text(out, y0, 17, 1, buf, g_ui[P_WHITE], 2, 0);
  blit_sprite(out, y0, 58, 0, GC_ITEMS(I_COIN), 0, 0);
  itoa_into(buf, g_gold);
  draw_text(out, y0, 74, 1, buf, g_ui[P_YELLOW], 2, 0);
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) {
    t[0] = (char)('0' + g_hour / 10); t[1] = (char)('0' + g_hour % 10);
    t[3] = (char)('0' + g_min / 10); t[4] = (char)('0' + g_min % 10);
  }
  draw_text(out, y0, GC_W - 5 * 12 - 2, 1, t, g_ui[P_WHITE], 2, 0);
}

static void draw_dialogue(uint16_t* out, int y0) {
  const int bx = 3, by = 193, bw = GC_W - 6, bh = 45;
  fill_rect(out, y0, bx, by, bw, bh, g_ui[P_WHITE]);
  fill_rect(out, y0, bx + 2, by + 2, bw - 4, bh - 4, g_ui[P_OUTLINE]);
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
  if (D.active && s >= 12) draw_dialogue(out, y0);
  else if (g_status_band && s == GC_ROWS - 1) draw_status_band(out, y0);
}
