// Calendar (design 5, 6.2): minute of day, the day counter (natural midnight rule), seasons,
// the weather machine, festivals, the daily event and the palette look for this minute.
// Chunk 6. No heap, no libc, no floating point; every loop is bounded.
#include "world_priv.h"

// Daily events (event_today): 0 none, then in the order of design 5.4:
//  1 market sale, 2 bumper demand, 3 fish frenzy, 4 mushroom bloom, 5 slime trouble at the east gate,
//  6 lost chicken errand, 7 message in a bottle, 8 travelling bard, 9 Pip's hide and seek,
//  10 the cat brings a gift, 11 shooting stars (night), 12 ghost lights in the ruins (night)
enum { EVENT_COUNT = 12 };

// ---------------------------------------------------------------- tables
// Weather odds per season, in percent, each row sums to 100. Order: GQ_W_*.
//                                                  CLEAR CLOUDY RAIN STORM FOG SNOW WINDY
static const uint8_t WEATHER_ODDS[4][GQ_W_COUNT] GC_ROM_ATTR = {
  /* spring */ {32, 20, 26,  4,  8,  0, 10},
  /* summer */ {44, 14, 12, 14,  3,  0, 13},
  /* autumn */ {22, 22, 24,  6, 16,  0, 10},
  /* winter */ {26, 24,  4,  2, 10, 28,  6},
};

// Look per weather (design 5.3): wash strength 0..255, wash colour, tint multiplier (256 = 1.0).
struct WxLook { uint8_t k, wr, wg, wb; uint16_t tr, tg, tb; };
static const WxLook WX[GQ_W_COUNT] GC_ROM_ATTR = {
  {  0,   0,   0,   0, 256, 256, 256},   // clear
  { 38, 150, 154, 164, 256, 256, 256},   // cloudy: grey 15%
  { 64, 120, 135, 160, 215, 225, 240},   // rain: blue-grey 25%
  {102,  90, 100, 130, 170, 180, 210},   // storm: blue-grey 40%
  {115, 235, 238, 240, 256, 256, 256},   // fog: white 45%
  { 51, 245, 248, 255, 256, 256, 256},   // snow: white 20%
  {  0,   0,   0,   0, 256, 256, 256},   // windy
};

// Festival lighting: the tint never falls below this while a festival runs (lanterns, lights).
static const uint16_t FEST_FLOOR[4][3] GC_ROM_ATTR = {
  {190, 176, 196}, {176, 138, 118}, {184, 150, 126}, {140, 146, 196},
};

// Day/night keyframes (minute, r, g, b), linear in between.
static const uint16_t KEYS[9][4] GC_ROM_ATTR = {
  {0, 90, 105, 180}, {300, 90, 105, 180}, {390, 220, 190, 200}, {450, 256, 256, 256},
  {1050, 256, 256, 256}, {1110, 256, 200, 160}, {1170, 170, 140, 170}, {1230, 90, 105, 180},
  {1440, 90, 105, 180},
};

// ----------------------------------------------------------------- state
static uint32_t g_minute_acc;     // ms inside the game's own minute (used when the wall clock is invalid)
static uint8_t  s_armed;          // 1 once the minute was seen mid-day since the last day change
static uint16_t s_event_day;      // (day + 1) for which the daily event was drawn; volatile
static uint16_t s_fest_day;       // (day + 1) for which the festival start was announced; volatile
static int32_t  s_flash_cd;       // ms until the next lightning (storm only)
static int32_t  s_flash_ms;       // ms of the current flash left

static int festival_day(void) { return G.s.day % 7 == 6; }
static int minute_of_day(void) { return G.s.minute > 1439 ? 1439 : G.s.minute; }

int cal_minute(void) { return minute_of_day(); }
int cal_is_night(void) { return G.s.minute >= 22 * 60 || G.s.minute < 6 * 60; }
int cal_season(void) { return (G.s.day / 7) % 4; }
int cal_day_of_season(void) { return G.s.day % 7 + 1; }
int cal_year(void) { return G.s.day / 28 + 1; }
int cal_weather(void) { return G.s.weather < GQ_W_COUNT ? (int)G.s.weather : (int)GQ_W_CLEAR; }
int cal_event(void) { return G.s.event_today <= EVENT_COUNT ? G.s.event_today : 0; }

// Festival of this season, only on day 7 of the season between 16:00 and 22:00.
int cal_festival(void) {
  int m = minute_of_day();
  if (!festival_day() || m < 16 * 60 || m >= 22 * 60) return -1;
  return cal_season();
}

// --------------------------------------------------------------- weather
static void set_weather(int w, int hours, int emit) {
  G.s.weather_hours = (uint8_t)hours;
  if (w == G.s.weather) return;
  G.s.weather = (uint8_t)w;
  gq_touch();
  if (emit) gq_emit(GQ_EV_WEATHER, w, 0);
}

// Draws the next state from the season row (bounded loop over 7 weights).
static void draw_weather(int emit) {
  const uint8_t* row = WEATHER_ODDS[cal_season()];
  int roll = (int)(gc_rnd() % 100), w = 0;
  for (int i = 0; i < GQ_W_COUNT; i++) {
    roll -= GC_RD8(&row[i]);
    if (roll < 0) { w = i; break; }
  }
  set_weather(w, 2 + (int)(gc_rnd() % 7), emit);   // 2..8 game hours
}

// Festival days are clear until 22:00; snow only exists in winter; an empty timer draws.
static void weather_rules(int emit) {
  if (festival_day() && minute_of_day() < 22 * 60) {
    set_weather(GQ_W_CLEAR, 1, emit);
  } else if (G.s.weather == GQ_W_SNOW && cal_season() != GQ_WINTER) {
    draw_weather(emit);
  } else if (G.s.weather_hours == 0) {
    draw_weather(emit);
  }
}

static void weather_hour(void) {
  if (!(festival_day() && minute_of_day() < 22 * 60) && G.s.weather_hours > 0) G.s.weather_hours--;
  weather_rules(1);
}

// ---------------------------------------------------------------- events
static void draw_daily_event(void) {
  if (s_event_day == (uint16_t)(G.s.day + 1)) return;     // a flapping clock must not re-roll
  s_event_day = (uint16_t)(G.s.day + 1);
  int r = (int)(gc_rnd() % 100), ev = 0;
  if (!festival_day() && r >= 40) ev = 1 + (r - 40) / 5;  // 40% none; festival days have their own event
  if (ev != G.s.event_today) { G.s.event_today = (uint8_t)ev; gq_touch(); }
}

static void festival_update(int emit) {
  int cur = cal_festival();
  uint8_t flags = G.s.festival_flags & 0x0F;
  if (cur >= 0) {
    if (!(flags & (1u << cur))) {
      G.s.festival_flags = (uint8_t)(flags | (1u << cur));
      gq_touch();
      if (emit && s_fest_day != (uint16_t)(G.s.day + 1)) {
        s_fest_day = (uint16_t)(G.s.day + 1);
        gq_emit(GQ_EV_FESTIVAL, cur, 1);
      }
    }
  } else if (flags) {
    G.s.festival_flags = 0;
    gq_touch();
    if (emit)
      for (int f = 0; f < 4; f++)
        if (flags & (1u << f)) gq_emit(GQ_EV_FESTIVAL, f, 0);
  }
}

void cal_on_event(int ev, int a, int b) {
  (void)b;
  if (ev == GQ_EV_NEW_DAY) {
    weather_rules(1);
  } else if (ev == GQ_EV_HOUR) {
    weather_hour();
    if (a == 6) draw_daily_event();
  }
}

// ------------------------------------------------------------------ look
// Palette inputs for this minute: day/night tint times the weather tint, then the wash (design 6.2).
void cal_look(GqLook* out) {
  int m = minute_of_day(), i = 0;
  while (i < 7 && (int)GC_RD16(&KEYS[i + 1][0]) < m) i++;
  int k0 = GC_RD16(&KEYS[i][0]), k1 = GC_RD16(&KEYS[i + 1][0]);
  int span = k1 - k0;
  int f = span > 0 ? (m - k0) * 256 / span : 0;
  int t[3];
  for (int c = 0; c < 3; c++) {
    int a = GC_RD16(&KEYS[i][1 + c]), b = GC_RD16(&KEYS[i + 1][1 + c]);
    t[c] = a + (((b - a) * f) >> 8);
  }
  const WxLook* w = &WX[cal_weather()];
  t[0] = t[0] * (int)GC_RD16(&w->tr) >> 8;
  t[1] = t[1] * (int)GC_RD16(&w->tg) >> 8;
  t[2] = t[2] * (int)GC_RD16(&w->tb) >> 8;
  int fest = cal_festival();
  if (fest >= 0)
    for (int c = 0; c < 3; c++) {
      int fl = GC_RD16(&FEST_FLOOR[fest][c]);
      if (t[c] < fl) t[c] = fl;
    }
  int k = GC_RD8(&w->k), r = GC_RD8(&w->wr), g = GC_RD8(&w->wg), bl = GC_RD8(&w->wb);
  if (s_flash_ms > 0) { t[0] = t[1] = t[2] = 320; k = 90; r = g = bl = 255; }   // lightning
  for (int c = 0; c < 3; c++) { if (t[c] < 0) t[c] = 0; if (t[c] > 320) t[c] = 320; }
  out->season = (uint8_t)cal_season();
  out->wash_k = (uint8_t)k;
  out->wash_rgb565 = k ? (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (bl >> 3)) : 0;
  out->tr = (int16_t)t[0];
  out->tg = (int16_t)t[1];
  out->tb = (int16_t)t[2];
}

// ------------------------------------------------------------------ time
void cal_init(void) {
  g_minute_acc = 0;
  s_armed = 1;
  s_event_day = s_fest_day = 0;
  s_flash_ms = 0;
  s_flash_cd = 20000 + (int32_t)(gc_rnd() % 40000);
  if (g_clock_valid) G.s.minute = (uint16_t)(g_hour * 60 + g_min);
  if (G.s.minute > 1439) G.s.minute = 1439;
  // a restored or fresh state is made consistent silently (the other modules are not initialised yet)
  weather_rules(0);
  if (cal_festival() < 0) G.s.festival_flags = 0;
  G.s.festival_flags &= 0x0F;
  if (G.s.event_today > EVENT_COUNT) G.s.event_today = 0;
}

// Sets the minute of day. `day` increases only on a natural midnight crossing (design 5.1):
// the previous minute was at least 1435 and the new one is at most 5, and the minute has been
// in the middle of a day since the last increase (so a clock that flaps around midnight cannot
// count days). Any other jump (NTP arriving, time zone or DST change) changes the minute only.
static void set_minute(int m) {
  if (m < 0 || m > 1439) return;
  if (m >= 60 && m <= 1380) s_armed = 1;
  int prev = G.s.minute;
  if (m == prev) return;
  G.s.minute = (uint16_t)m;
  g_dirty |= 1u;
  if (prev >= 1435 && m <= 5 && s_armed) {
    s_armed = 0;
    if (G.s.day < 65535) G.s.day++;
    gq_touch();
    gq_emit(GQ_EV_NEW_DAY, G.s.day & 0xFF, cal_season());
  }
  if (m / 60 == (prev / 60 + 1) % 24) gq_emit(GQ_EV_HOUR, m / 60, 0);   // natural hours only
}

void cal_tick(uint32_t dt) {
  // time of day: wall clock when known, otherwise our own clock (60 s per minute).
  // gc_tick caps dt at 250 ms; a larger dt still advances at most one minute (no catch-up loop).
  if (g_clock_valid) {
    g_minute_acc = 0;
    set_minute(g_hour * 60 + g_min);
  } else {
    g_minute_acc += dt;
    if (g_minute_acc >= 60000) {
      g_minute_acc = 0;
      set_minute((G.s.minute + 1) % 1440);
    }
  }
  festival_update(1);
  // lightning: a flash of 80 ms every 20..60 s while it storms
  if (s_flash_ms > 0) s_flash_ms = s_flash_ms > (int32_t)dt ? s_flash_ms - (int32_t)dt : 0;
  if (cal_weather() == GQ_W_STORM) {
    s_flash_cd -= (int32_t)dt;
    if (s_flash_cd <= 0) {
      s_flash_cd = 20000 + (int32_t)(gc_rnd() % 40001);
      s_flash_ms = 80;
    }
  } else {
    s_flash_ms = 0;
  }
}
