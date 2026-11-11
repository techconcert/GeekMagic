// UI (chunk 2): HUD, bottom band, banners, dialogue box, quest postcard, curtain, info cards,
// number formatting. Design sections 3.7, 6.3 to 6.7.
#include "world_priv.h"

static uint32_t g_since_init;
static int      g_status_band;          // 1 = network status lines may be shown in the bottom band
static uint8_t  g_status_idx;
static uint32_t g_status_acc;
static uint8_t  s_show_status;          // 1 = the band currently shows a network status line (AP name, password...)
static uint32_t s_up_s, s_up_ms;        // uptime of the game since init, seconds (the clock restarts the game on every boot)
static uint8_t  s_pg;                   // status band page: 0 hero state, 1 IP address, 2 uptime
static uint32_t s_pg_acc;

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

// HUD and band change detection
static uint32_t s_hud_sig;
static uint32_t s_band_sig;
static uint32_t s_band_acc;
static char     s_b_text[24];
static uint8_t  s_b_icon, s_b_has_icon;

// banner (achievement, collectible, star): replaces the band text for a few seconds
static char     s_ban_text[20];
static uint8_t  s_ban_icon;
static int32_t  s_ban_ms;

// curtain: s_ck = number of black strip pairs (0 open .. 8 closed), s_cdir +1 closing, -1 opening
static uint8_t  s_ck;
static int8_t   s_cdir;
static uint16_t s_cacc;

// info cards: 1 CLOCK, 2 PACK, 3 QUEST, 4 VILLAGE
static uint8_t  s_card;                 // card on screen, 0 = none
static int32_t  s_card_ms;              // remaining display time
static uint32_t s_card_acc;             // ms since the last card opened (rotation timer)
static uint8_t  s_card_pend;            // card waiting for a free moment, 0 = none
static int32_t  s_pend_hold;
static uint32_t s_pend_wait;
static uint8_t  s_card_next;            // rotation position 0..3
static uint32_t s_card_sig;
static uint32_t s_card_acc2;
static uint8_t  s_card_dirty;
static uint8_t  s_card_dbg;             // 1 = opened from the debug panel: stays up whatever the hero does
static int8_t   s_hour_seen;            // last hour announced by HOUR events

enum { UI_I_EXCL = 35, UI_I_SCROLL = 36 };   // items.png cells (design 10.5)
static const int CARD_X = 8, CARD_Y = 32, CARD_W = 224, CARD_H = 176;
static const uint32_t CARD_MASK = 0x1FFCu;   // strips 2..12

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

// Appends src to dst (capacity cap), returns the new length.
static int str_cat(char* dst, int len, const char* src, int cap) {
  for (int i = 0; src[i] && len < cap - 1; i++) dst[len++] = src[i];
  dst[len] = 0;
  return len;
}

static int imin(int a, int b) { return a < b ? a : b; }

// ------------------------------------------------------------------ tables
static const uint8_t UI_ITEM_ICON[GQ_ITEM_COUNT] GC_ROM_ATTR = {
  9, 16, 7, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,   // wood stone fish mushroom berry flower shell ore gem crop seed herb gel
  5, 5, 5,                                            // reserved goods
  27, 28, 29, 30, 28, 31, 24, 5                       // map key shard parcel rope charm oil, reserved
};
static const char* const SEASON_NAME[4] = {"SPRING", "SUMMER", "AUTUMN", "WINTER"};
static const char* const SEASON_ABBR[4] = {"SPR", "SUM", "AUT", "WIN"};
static const char* const WEATHER_NAME[GQ_W_COUNT] = {"CLEAR", "CLOUDY", "RAIN", "STORM", "FOG", "SNOW", "WINDY"};
static const char* const WDAY_NAME[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
static const char* const MONTH_NAME[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
static const uint8_t PACK_ITEMS[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12};

static int item_icon(int item) {
  if (item < 0 || item >= GQ_ITEM_COUNT) item = 0;
  return GC_RD8(&UI_ITEM_ICON[item]);
}

// --------------------------------------------------------------- dialogue
static void card_close(void);

void ui_say(int who, const char* text, int32_t hold_ms) {
  card_close();
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

// ----------------------------------------------------------------- curtain
// Step k closes strips k-1 and 14-(k-1) (design 6.5: 8 steps of 40 ms, 15 strips).
static void curtain_mark(int d) {
  g_dirty |= (1u << d) | (1u << (GC_ROWS - 1 - d));
}

void ui_curtain(int close) {
  if (close) {
    card_close();
    if (s_ck >= 8) { s_cdir = 0; return; }
    s_cdir = 1;
  } else {
    if (s_ck == 0) { s_cdir = 0; return; }
    s_cdir = -1;
  }
}

int ui_curtain_closed(void) { return s_ck >= 8; }

int ui_curtain_covers(int strip) {
  if (strip < 0 || strip >= GC_ROWS) return 0;
  int d = imin(strip, GC_ROWS - 1 - strip);
  return d < s_ck;
}

static void curtain_tick(uint32_t dt) {
  if (!s_cdir) return;
  uint32_t acc = s_cacc + dt;
  for (int guard = 0; acc >= 40 && s_cdir && guard < 16; guard++) {
    acc -= 40;
    if (s_cdir > 0) {
      curtain_mark(s_ck);
      s_ck++;
      if (s_ck >= 8) { s_ck = 8; s_cdir = 0; }
    } else {
      s_ck--;
      curtain_mark(s_ck);
      if (s_ck == 0) s_cdir = 0;
    }
  }
  s_cacc = (uint16_t)(s_cdir ? (acc > 39 ? 39 : acc) : 0);
}

// -------------------------------------------------------------------- band
// Goal savings text, for example "AXE II 73/100". Returns 0 when every goal is bought.
static int goal_text(char* buf, int cap, int* num, int* den) {
  GqGoal g;
  if (!econ_goal(&g)) return 0;
  char name[16];
  gq_text(g.name_txt, name, 16);
  if (!name[0]) str_copy(name, "GOAL", 16);
  int n = G.s.coins < g.price ? G.s.coins : g.price;
  char a[6], b[6];
  fmt_uint(a, (uint32_t)n);
  fmt_uint(b, g.price);
  int numlen = str_len(a) + 1 + str_len(b);
  int room = 18 - numlen - 1;
  int len = 0;
  for (int i = 0; name[i] && i < room && len < cap - 1; i++) buf[len++] = name[i];
  buf[len] = 0;
  len = str_cat(buf, len, " ", cap);
  len = str_cat(buf, len, a, cap);
  len = str_cat(buf, len, "/", cap);
  str_cat(buf, len, b, cap);
  *num = n; *den = g.price;
  return 1;
}

static uint32_t hash_str(uint32_t h, const char* s) {
  for (int i = 0; s[i]; i++) h = (h ^ (uint8_t)s[i]) * 16777619u;
  return h;
}

// Status band (R2): the bottom strip shows, in turn every 4 s, what the hero is doing, the clock's IP address and
// the uptime, all at scale 2 (at most 18 characters). A banner (achievement, collectible) replaces it for a few
// seconds. Only strip 14 is ever marked dirty, and only when the text on it changed.
#define BAND_PAGE_MS 4000

// Uptime as "UP 12 MIN", "UP 3H 25M" or "UP 2D 03H".
static void uptime_text(char* buf, int cap) {
  uint32_t s = s_up_s, d = s / 86400, h = s / 3600 % 24, m = s / 60 % 60;
  char a[12], b[12];
  int n = str_cat(buf, 0, "UP ", cap);
  if (d || h) {
    uint32_t big = d ? d : h, small = d ? h : m;
    fmt_uint(a, big); fmt_uint(b, small);
    n = str_cat(buf, n, a, cap);
    n = str_cat(buf, n, d ? "D " : "H ", cap);
    if (small < 10) n = str_cat(buf, n, "0", cap);
    n = str_cat(buf, n, b, cap);
    str_cat(buf, n, d ? "H" : "M", cap);
  } else {
    fmt_uint(a, m);
    n = str_cat(buf, n, a, cap);
    str_cat(buf, n, " MIN", cap);
  }
}

static void band_update(void) {
  char t[24];
  int icon = I_NONE;
  t[0] = 0;
  if (s_ban_ms > 0) {
    str_copy(t, s_ban_text, 24);
    icon = s_ban_icon;
  } else if (s_pg == 1 && g_status[0][0] == 'I') {
    str_copy(t, g_status[0], 24);                 // "IP 192.168.1.20"
  } else if (s_pg == 2) {
    uptime_text(t, 24);
  } else {
    hero_status_text(t, 24);
  }
  uint32_t h = hash_str(2166136261u, t);
  h = (h ^ (uint32_t)(icon + 1)) * 16777619u;
  if (h != s_band_sig) {
    s_band_sig = h;
    str_copy(s_b_text, t, 24);
    s_b_icon = (uint8_t)(icon < 0 ? 0 : icon);
    s_b_has_icon = icon >= 0;
    if (!s_show_status) g_dirty |= 1u << (GC_ROWS - 1);
  }
}

const char* ui_band_text(void) { return s_b_text; }   // for the tests

static void banner(int icon, const char* text, int32_t ms) {
  str_copy(s_ban_text, text, 20);
  s_ban_icon = (uint8_t)icon;
  s_ban_ms = ms;
  band_update();
  g_dirty |= 1u << (GC_ROWS - 1);
}

// ------------------------------------------------------------------- cards
static void card_close(void) {
  if (!s_card) return;
  s_card = 0;
  s_card_ms = 0;
  mark_rows(16, 208);          // strips 1..13
}

static void card_open(int id, int32_t hold_ms) {
  if (id < 1 || id > 4) return;
  s_card = (uint8_t)id;
  s_card_dbg = 0;
  s_card_ms = hold_ms;
  s_card_acc = 0;
  s_card_pend = 0;
  s_card_sig = 0;
  s_card_acc2 = 0;
  s_card_dirty = 1;
  mark_rows(16, 208);
}

void ui_debug_show_card(int id) {
  if (id >= 1 && id <= 4) { card_open(id, 30000); s_card_dbg = 1; }
  else card_close();
}

int ui_card_id(void) { return s_card; }

int ui_card_covers(int strip) { return s_card && strip >= 2 && strip <= 12; }

int ui_busy(void) { return D.active || s_card || s_ck || s_cdir; }

static void card_request(int id, int32_t hold_ms) {
  if (!G.card_period || s_card) return;
  s_card_pend = (uint8_t)id;
  s_pend_hold = hold_ms;
  s_pend_wait = 0;
}

static int card_can_open(void) {
  if (D.active || s_ck || s_cdir) return 0;
  if (hero_activity() == GQ_ACT_FIGHT) return 0;
  if (cal_festival() >= 0) return 0;
  if (hero_away_progress(0) >= 0) return 0;
  if (G.night_quiet && cal_is_night()) return 0;
  return 1;
}

static uint32_t card_signature(void) {
  uint32_t h = 2166136261u;
  const GqSave& s = G.s;
  uint32_t v[10] = {s.minute, s.coins, s.stars, s.projects, s.weather, s.day, s.goal, s.chapter,
                    (uint32_t)(s.q_active | (s.q_step << 8) | (s.q_count << 16)), s.fund};
  for (int i = 0; i < 10; i++) h = (h ^ v[i]) * 16777619u;
  for (int i = 0; i < GQ_ITEM_COUNT; i++) h = (h ^ s.inv[i]) * 16777619u;
  for (int i = 0; i < 12; i++) h = (h ^ s.friend_[i]) * 16777619u;
  h = (h ^ s.earned_total) * 16777619u;
  h = (h ^ G.clock_approx) * 16777619u;
  return h;
}

static void card_tick(uint32_t dt) {
  if (s_card) {
    s_card_ms -= (int32_t)dt;
    if (s_card_ms <= 0) { card_close(); return; }
    if (!s_card_dbg && (hero_away_progress(0) >= 0 || hero_activity() == GQ_ACT_FIGHT)) { card_close(); return; }
    s_card_acc2 += dt;
    if (s_card_acc2 >= 500) {
      s_card_acc2 = 0;
      uint32_t sig = card_signature();
      if (sig != s_card_sig) { s_card_sig = sig; s_card_dirty = 1; }
    }
    return;
  }
  if (G.card_period) {
    s_card_acc += dt;
    if (s_card_acc >= (uint32_t)G.card_period * 60000u) {
      s_card_acc = 0;
      int id = s_card_next + 1;
      s_card_next = (uint8_t)((s_card_next + 1) & 3);
      card_request(id, 10000);
    }
  } else {
    s_card_pend = 0;
  }
  if (s_card_pend) {
    s_pend_wait += dt;
    if (card_can_open()) card_open(s_card_pend, s_pend_hold);
    else if (s_pend_wait >= 60000u) s_card_pend = 0;
  }
}

// ------------------------------------------------------------ banners, events
void ui_on_event(int ev, int a, int b) {
  (void)a; (void)b;
  switch (ev) {
    case GQ_EV_ACHIEVEMENT:     banner(UI_I_SCROLL, "NEW ACHIEVEMENT!", 6000); break;
    case GQ_EV_NEW_COLLECTIBLE: banner(UI_I_EXCL, "NEW COLLECTIBLE!", 4000); break;
    case GQ_EV_STAR:            banner(I_STAR, "NEW STAR!", 4000); break;
    case GQ_EV_HOUR:                                             // clock card at every natural full hour
      if (a == (s_hour_seen + 1) % 24) card_request(1, 15000);
      s_hour_seen = (int8_t)(a & 31);
      break;
    default: break;
  }
}

void ui_status_changed(void) { village_status_changed(); }

void village_status_changed() {
  if (g_status_band && s_show_status) g_dirty |= 1u << (GC_ROWS - 1);
}

void ui_init(void) {
  g_since_init = 0;
  g_status_band = 1;
  g_status_idx = 0;
  g_status_acc = 0;
  s_show_status = 0;
  D.active = 0;
  s_pc_acc = 0;
  s_pc_sig = -1;
  s_hud_sig = 0;
  s_band_sig = 0;
  s_band_acc = 0;
  s_b_text[0] = 0;
  s_b_has_icon = 0;
  s_up_s = s_up_ms = 0; s_pg = 0; s_pg_acc = 0;
  s_ban_ms = 0;
  s_ck = 0; s_cdir = 0; s_cacc = 0;
  s_card = 0; s_card_dbg = 0; s_card_ms = 0; s_card_acc = 0; s_card_pend = 0; s_card_next = 0;
  s_card_dirty = 0; s_card_acc2 = 0;
  s_hour_seen = (int8_t)(cal_minute() / 60);
  band_update();
}

// --------------------------------------------------------------- postcard
// Triangle wave 0..255..0 of the away progress: the hero walks out and back.
static int tri(int progress) { return progress < 128 ? progress * 2 : (255 - progress) * 2; }

static int pc_hero_x(int progress) { return 24 + (176 * tri(progress) / 255 & ~1); }

static int pc_frame(void) { return (int)((s_pc_acc / 150) & 3); }

static const int PC_BOX_X = 3, PC_BOX_Y = 193, PC_BOX_W = GC_W - 6, PC_BOX_H = 45;

static uint32_t hud_signature(void) {
  uint32_t h = G.s.coins;
  h = h * 131u + (uint32_t)econ_focus_item();
  h = h * 131u + G.s.inv[econ_focus_item() & 31];
  h = h * 131u + (uint32_t)cal_weather();
  h = h * 131u + (uint32_t)cal_season();
  h = h * 131u + (uint32_t)cal_day_of_season();
  h = h * 131u + G.net;
  h = h * 131u + G.clock_approx;
  h = h * 131u + (uint32_t)g_clock_valid;
  return h;
}

void ui_tick(uint32_t dt) {
  if (g_since_init < 0xFFFF0000u) g_since_init += dt;
  s_up_ms += dt;
  while (s_up_ms >= 1000) { s_up_ms -= 1000; if (s_up_s < 0xFFFFFFFFu) s_up_s++; }
  s_pg_acc += dt;
  if (s_pg_acc >= BAND_PAGE_MS) {
    s_pg_acc = 0;
    s_pg = (uint8_t)((s_pg + 1) % 3);
    if (s_pg == 1 && g_status[0][0] != 'I') s_pg = 2;   // no address to show
  }
  dialogue_tick(dt);
  curtain_tick(dt);
  card_tick(dt);

  if (s_ban_ms > 0) {
    s_ban_ms -= (int32_t)dt;
    if (s_ban_ms <= 0) { s_ban_ms = 0; band_update(); g_dirty |= 1u << (GC_ROWS - 1); }
  }
  s_band_acc += dt;
  if (s_band_acc >= 250) { s_band_acc = 0; band_update(); }

  uint32_t hs = hud_signature();
  if (hs != s_hud_sig) { s_hud_sig = hs; g_dirty |= 1u; }

  int away = hero_away_progress(0);
  if (away >= 0) {
    s_pc_acc += dt;
    int sig = pc_hero_x(away) * 4 + pc_frame() + 1;
    if (sig != s_pc_sig) { s_pc_sig = (int16_t)sig; mark_rows(192, 48); }
  } else if (s_pc_sig != -1) {
    s_pc_sig = -1;
    mark_rows(192, 48);
  }

  // network status lines: first 90 s after boot, and whenever WiFi is down; they alternate
  // with the goal / quest band every 5 s (design 6.6)
  int want = g_since_init < 90000 || (g_status[0][0] == 'W');
  if (want != g_status_band) { g_status_band = want; g_dirty |= 1u << (GC_ROWS - 1); }
  if (g_status_band) {
    g_status_acc += dt;
    if (g_status_acc >= 5000) {
      g_status_acc = 0;
      if (s_show_status) {
        s_show_status = 0;
      } else {
        for (int k = 0; k < 4; k++) {
          g_status_idx = (uint8_t)((g_status_idx + 1) % 4);
          if (g_status[g_status_idx][0]) { s_show_status = 1; break; }
        }
      }
      g_dirty |= 1u << (GC_ROWS - 1);
    }
  } else if (s_show_status) {
    s_show_status = 0;
    g_dirty |= 1u << (GC_ROWS - 1);
  }

  // While a card covers strips 2..12, world changes under it need no redraw.
  if (s_card) {
    int need = s_card_dirty;
    s_card_dirty = 0;
    g_dirty &= ~CARD_MASK;
    if (need) g_dirty |= CARD_MASK;
  }
}

// ------------------------------------------------------------------ drawing
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

static void draw_hud(uint16_t* out, int y0) {
  darken_rect(out, y0, 0, 0, GC_W, 16);
  char buf[12];
  // coins: at most 3 digits (the wallet cap is 999)
  blit_sprite(out, y0, 0, 0, GC_ITEMS(I_COIN), 0, 0, g_ui);
  uint32_t coins = G.s.coins > 999 ? 999u : G.s.coins;
  fmt_uint(buf, coins);
  draw_text(out, y0, 17, 1, buf, g_ui[P_YELLOW], 2, 0);
  // focus item (what the goal or quest needs): at most 2 digits
  int fi = econ_focus_item();
  if (fi < 0 || fi >= GQ_ITEM_COUNT) fi = 0;
  blit_sprite(out, y0, 56, 0, GC_ITEMS(item_icon(fi)), 0, 0, g_ui);
  uint32_t cnt = G.s.inv[fi] > 99 ? 99u : G.s.inv[fi];
  fmt_uint(buf, cnt);
  draw_text(out, y0, 73, 1, buf, g_ui[P_WHITE], 2, 0);
  // weather icon, season and day of season
  int w = cal_weather();
  if (w < 0 || w >= GQ_W_COUNT) w = 0;
  blit_sprite(out, y0, 100, 0, GC_ITEMS(40 + w), 0, 0, g_ui);
  int se = cal_season() & 3;
  draw_text(out, y0, 120, 0, SEASON_ABBR[se], g_ui[P_WHITE], 1, 0);
  buf[0] = 'D'; buf[1] = (char)('0' + cal_day_of_season() % 10); buf[2] = 0;
  draw_text(out, y0, 120, 8, buf, g_ui[P_GREY], 1, 0);
  // red "!" only while the station link is down
  if (G.net == GC_NET_OFFLINE) draw_text(out, y0, 150, 1, "!", g_ui[12], 2, 0);
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) {
    t[0] = (char)('0' + g_hour / 10); t[1] = (char)('0' + g_hour % 10);
    t[3] = (char)('0' + g_min / 10); t[4] = (char)('0' + g_min % 10);
  }
  draw_text(out, y0, GC_W - 5 * 12 - 2, 1, t, G.clock_approx ? g_ui[P_GREY] : g_ui[P_WHITE], 2, 0);
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

static void draw_band(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, 16, g_ui[P_OUTLINE]);
  if (s_show_status && g_status[g_status_idx][0]) {
    const char* s = g_status[g_status_idx];
    int x = (GC_W - str_len(s) * 12) / 2;
    if (x < 2) x = 2;
    draw_text(out, y0, x, y0, s, g_ui[P_WHITE], 2, 0);
    return;
  }
  if (!s_b_text[0]) return;
  int len = str_len(s_b_text);
  if (len > 18) len = 18;
  if (s_b_has_icon) {
    blit_sprite(out, y0, 0, y0, GC_ITEMS(s_b_icon), 0, 0, g_ui);
    draw_text_n(out, y0, 20, y0, s_b_text, len, g_ui[P_WHITE], 2);
    return;
  }
  int x = (GC_W - len * 12) / 2;
  if (x < 2) x = 2;
  draw_text_n(out, y0, x, y0, s_b_text, len, g_ui[P_WHITE], 2);
}

// ---- cards
// Text clipped to the card interior (x 12..227).
static void ct(uint16_t* out, int y0, int x, int y, const char* s, uint16_t c, int scale) {
  int n = (228 - x + scale) / (6 * scale);
  if (n <= 0) return;
  int len = str_len(s);
  draw_text_n(out, y0, x, y, s, len < n ? len : n, c, scale);
}

static void ctc(uint16_t* out, int y0, int y, const char* s, uint16_t c, int scale) {
  int w = str_len(s) * 6 * scale - scale;
  int x = CARD_X + (CARD_W - w) / 2;
  if (x < 12) x = 12;
  ct(out, y0, x, y, s, c, scale);
}

// Two lines of at most 18 characters at scale 2, broken at a space where possible.
static void cwrap(uint16_t* out, int y0, int y, const char* s, uint16_t c) {
  int len = str_len(s), cut = len;
  if (len > 18) {
    cut = 18;
    for (int i = 18; i > 4; i--) if (s[i] == ' ') { cut = i; break; }
  }
  draw_text_n(out, y0, 12, y, s, imin(cut, 18), c, 2);
  if (cut < len) {
    int off = cut + (s[cut] == ' ' ? 1 : 0);
    draw_text_n(out, y0, 12, y + 18, s + off, imin(len - off, 18), c, 2);
  }
}

static void chud_heart(uint16_t* out, int y0, int x, int y, uint16_t c) {
  static const uint8_t H[6] = {0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
  for (int r = 0; r < 6; r++) {
    if (y + r < y0 || y + r >= y0 + GC_STRIP_H) continue;
    for (int b = 0; b < 7; b++)
      if (H[r] & (0x40 >> b)) fill_rect(out, y0, x + b, y + r, 1, 1, c);
  }
}

static void card_clock(uint16_t* out, int y0) {
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) {
    t[0] = (char)('0' + g_hour / 10); t[1] = (char)('0' + g_hour % 10);
    t[3] = (char)('0' + g_min / 10); t[4] = (char)('0' + g_min % 10);
  }
  uint16_t white = g_ui[P_WHITE], grey = g_ui[P_GREY];
  draw_text(out, y0, 30, 44, t, G.clock_approx ? grey : white, 6, 0);
  char b[24];
  if (G.year > 0) {
    int n = str_cat(b, 0, WDAY_NAME[G.wday % 7], 24);
    n = str_cat(b, n, " ", 24);
    char d[4];
    fmt_uint(d, G.mday > 31 ? 31u : G.mday);
    n = str_cat(b, n, d, 24);
    n = str_cat(b, n, " ", 24);
    str_cat(b, n, MONTH_NAME[(G.month + 11) % 12], 24);
    ctc(out, y0, 98, b, white, 2);
  } else {
    ctc(out, y0, 98, "DATE UNKNOWN", grey, 2);
  }
  int n = str_cat(b, 0, SEASON_NAME[cal_season() & 3], 24);
  n = str_cat(b, n, " DAY ", 24);
  char d[8];
  fmt_uint(d, (uint32_t)cal_day_of_season());
  str_cat(b, n, d, 24);
  ctc(out, y0, 118, b, g_ui[P_YELLOW], 2);
  n = str_cat(b, 0, "YEAR ", 24);
  fmt_uint(d, (uint32_t)cal_year());
  str_cat(b, n, d, 24);
  ctc(out, y0, 136, b, white, 2);
  int w = cal_weather();
  if (w < 0 || w >= GQ_W_COUNT) w = 0;
  int tw = 22 + str_len(WEATHER_NAME[w]) * 12;
  int x = CARD_X + (CARD_W - tw) / 2;
  blit_sprite(out, y0, x, 156, GC_ITEMS(40 + w), 0, 0, g_ui);
  ct(out, y0, x + 22, 157, WEATHER_NAME[w], white, 2);
  if (G.clock_approx) ctc(out, y0, 186, "CLOCK NOT SYNCED YET", grey, 1);
}

static void card_pack(uint16_t* out, int y0) {
  uint16_t white = g_ui[P_WHITE], yellow = g_ui[P_YELLOW];
  ct(out, y0, 14, 37, "PACK", yellow, 2);
  char b[12];
  for (int i = 0; i < 12; i++) {
    int item = GC_RD8(&PACK_ITEMS[i]);
    int x = 14 + (i & 3) * 54, y = 58 + (i >> 2) * 28;
    blit_sprite(out, y0, x, y, GC_ITEMS(item_icon(item)), 0, 0, g_ui);
    uint32_t c = G.s.inv[item] > 99 ? 99u : G.s.inv[item];
    fmt_uint(b, c);
    ct(out, y0, x + 18, y + 1, b, c ? white : g_ui[P_GREY], 2);
  }
  char t[24];
  int num = 0, den = 0;
  if (goal_text(t, 24, &num, &den)) {
    ct(out, y0, 14, 142, t, yellow, 2);
    fill_rect(out, y0, 14, 160, 200, 8, g_ui[P_OUTLINE]);
    if (den > 0) fill_rect(out, y0, 14, 160, 200 * num / den, 8, yellow);
  } else {
    ct(out, y0, 14, 142, "ALL GOALS DONE", yellow, 2);
  }
  int n = str_cat(t, 0, "COINS ", 24);
  fmt_uint(b, G.s.coins > 999 ? 999u : G.s.coins);
  n = str_cat(t, n, b, 24);
  n = str_cat(t, n, "/", 24);
  int cap = econ_coin_cap();
  fmt_uint(b, (uint32_t)(cap < 0 ? 0 : (cap > 999 ? 999 : cap)));
  str_cat(t, n, b, 24);
  ct(out, y0, 14, 178, t, white, 2);
}

static void card_quest(uint16_t* out, int y0) {
  uint16_t white = g_ui[P_WHITE], yellow = g_ui[P_YELLOW], grey = g_ui[P_GREY];
  ct(out, y0, 14, 37, "QUEST", yellow, 2);
  for (int i = 0; i < 6; i++) {
    int x = 14 + i * 20;
    fill_rect(out, y0, x, 58, 14, 8, grey);
    fill_rect(out, y0, x + 1, 59, 12, 6, i < G.s.chapter ? yellow : g_ui[P_OUTLINE]);
  }
  char t[40];
  t[0] = 0;
  if (quest_title(t, 40) && t[0]) {
    cwrap(out, y0, 74, t, yellow);
    int num = 0, den = 0;
    t[0] = 0;
    if (quest_band(t, 40, &num, &den) && t[0]) cwrap(out, y0, 118, t, white);
    GqNeed need;
    quest_need(&need);
    if (need.kind == GQ_NEED_TALK && need.npc > 0 && need.npc < GQ_NPC_COUNT) {
      char g[24];
      int n = str_cat(g, 0, "TALK TO ", 24);
      str_cat(g, n, gq_npc_name(need.npc), 24);
      ct(out, y0, 14, 178, g, grey, 2);
    }
  } else {
    ct(out, y0, 12, 74, "NO QUEST ACTIVE", white, 2);
    ct(out, y0, 12, 92, "THE HERO RESTS", grey, 2);
  }
}

static void card_village(uint16_t* out, int y0) {
  uint16_t white = g_ui[P_WHITE], yellow = g_ui[P_YELLOW];
  ct(out, y0, 14, 37, "VILLAGE", yellow, 2);
  char b[24], d[12];
  int n, pc = 0, ac = 0;
  for (int i = 0; i < 10; i++) pc += (G.s.projects >> i) & 1;
  for (int i = 0; i < 24; i++) ac += (G.s.ach[i >> 3] >> (i & 7)) & 1;
  n = str_cat(b, 0, "PROJECTS ", 24);
  fmt_uint(d, (uint32_t)pc); n = str_cat(b, n, d, 24);
  str_cat(b, n, "/10", 24);
  ct(out, y0, 14, 54, b, white, 2);
  n = str_cat(b, 0, "STARS ", 24);
  fmt_uint(d, G.s.stars > 999 ? 999u : G.s.stars); str_cat(b, n, d, 24);
  ct(out, y0, 14, 70, b, white, 2);
  n = str_cat(b, 0, "ACH ", 24);
  fmt_uint(d, (uint32_t)ac); n = str_cat(b, n, d, 24);
  str_cat(b, n, "/24", 24);
  ct(out, y0, 14, 86, b, white, 2);
  n = str_cat(b, 0, "DAY ", 24);
  fmt_uint(d, G.s.day); n = str_cat(b, n, d, 24);
  n = str_cat(b, n, " Y", 24);
  fmt_uint(d, (uint32_t)cal_year()); str_cat(b, n, d, 24);
  ct(out, y0, 14, 102, b, white, 2);
  n = str_cat(b, 0, "EARNED ", 24);
  fmt_compact(d, G.s.earned_total); str_cat(b, n, d, 24);
  ct(out, y0, 14, 118, b, yellow, 2);
  for (int i = 1; i < GQ_NPC_COUNT; i++) {
    int cx = 14 + ((i - 1) % 3) * 72, cy = 140 + ((i - 1) / 3) * 22;
    if (cy + 16 <= y0 || cy >= y0 + GC_STRIP_H) continue;
    ct(out, y0, cx, cy, gq_npc_name(i), g_ui[P_WHITE], 1);
    int hearts = G.s.friend_[i] / 25;
    for (int h = 0; h < 4; h++)
      chud_heart(out, y0, cx + h * 9, cy + 9, h < hearts ? g_ui[12] : g_ui[P_GREY]);
  }
}

static void draw_card(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, GC_STRIP_H, g_ui[P_OUTLINE]);
  fill_rect(out, y0, CARD_X, CARD_Y, CARD_W, CARD_H, g_ui[P_YELLOW]);
  fill_rect(out, y0, CARD_X + 2, CARD_Y + 2, CARD_W - 4, CARD_H - 4, g_ui[8]);
  switch (s_card) {
    case 1: card_clock(out, y0); break;
    case 2: card_pack(out, y0); break;
    case 3: card_quest(out, y0); break;
    default: card_village(out, y0); break;
  }
}

// ----------------------------------------------------------------- render
void ui_render(int s, uint16_t* out) {
  int y0 = s * GC_STRIP_H;
  if (ui_curtain_covers(s)) return;
  if (ui_card_covers(s)) { draw_card(out, y0); return; }
  if (s_card && (s == 1 || s == 13)) darken_rect(out, y0, 0, y0, GC_W, GC_STRIP_H);
  if (s == 0) draw_hud(out, y0);
  int away = hero_away_progress(0);
  if (away >= 0 && s >= 12) draw_postcard(out, y0, away);
  else if (D.active && s >= 12) draw_dialogue(out, y0);
  else if (s == GC_ROWS - 1) draw_band(out, y0);
}
