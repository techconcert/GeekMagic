// Calendar: minute of day, day counter (natural midnight rule) and the day/night tint keyframes.
// (Split out of village.cpp; chunk 6 grows this into days, seasons and weather.)
#include "world_priv.h"

static uint32_t g_minute_acc;

int cal_minute(void) { return G.s.minute; }
int cal_is_night(void) { return G.s.minute >= 22 * 60 || G.s.minute < 6 * 60; }

void cal_look(GqLook* out) {
  // brightness keyframes (minute, r, g, b) - linear in between
  static const int16_t K[][4] = {
    {0, 90, 105, 180}, {300, 90, 105, 180}, {390, 220, 190, 200}, {450, 256, 256, 256},
    {1050, 256, 256, 256}, {1110, 256, 200, 160}, {1170, 170, 140, 170}, {1230, 90, 105, 180},
    {1440, 90, 105, 180},
  };
  int m = G.s.minute, i = 0;
  if (m > 1439) m = 1439;
  while (i < 7 && K[i + 1][0] < m) i++;
  int span = K[i + 1][0] - K[i][0];
  int f = span ? (m - K[i][0]) * 256 / span : 0;
  out->season = (uint8_t)cal_season();
  out->wash_k = 0;
  out->wash_rgb565 = 0;
  out->tr = (int16_t)(K[i][1] + ((K[i + 1][1] - K[i][1]) * f >> 8));
  out->tg = (int16_t)(K[i][2] + ((K[i + 1][2] - K[i][2]) * f >> 8));
  out->tb = (int16_t)(K[i][3] + ((K[i + 1][3] - K[i][3]) * f >> 8));
}

void cal_init(void) {
  g_minute_acc = 0;
  if (g_clock_valid) G.s.minute = (uint16_t)(g_hour * 60 + g_min);
}

// Sets the minute of day. `day` increases only on a natural midnight crossing (design 5.1):
// the previous minute was at least 1435 and the new one is at most 5. Any other jump (NTP
// arriving, time zone or DST change) changes the minute but never the day.
static void set_minute(int m) {
  if (m < 0 || m > 1439) return;
  int prev = G.s.minute;
  if (m == prev) return;
  G.s.minute = (uint16_t)m;
  g_dirty |= 1u;
  if (prev >= 1435 && m <= 5) {
    if (G.s.day < 65535) G.s.day++;
    gq_touch();
    gq_emit(GQ_EV_NEW_DAY, G.s.day & 0xFF, cal_season());
  }
  if (m / 60 != prev / 60) gq_emit(GQ_EV_HOUR, m / 60, 0);
}

void cal_tick(uint32_t dt) {
  // time of day: wall clock when known, otherwise our own clock (60 s per minute)
  if (g_clock_valid) {
    set_minute(g_hour * 60 + g_min);
  } else {
    g_minute_acc += dt;
    for (int guard = 0; g_minute_acc >= 60000 && guard < 8; guard++) {
      g_minute_acc -= 60000;
      set_minute((G.s.minute + 1) % 1440);
    }
    if (g_minute_acc >= 60000) g_minute_acc = 0;
  }
}

int cal_season(void) { return (G.s.day / 7) % 4; }
int cal_day_of_season(void) { return G.s.day % 7 + 1; }
int cal_year(void) { return G.s.day / 28 + 1; }
int cal_weather(void) { return G.s.weather; }
int cal_festival(void) { return -1; }
int cal_event(void) { return 0; }
void cal_on_event(int ev, int a, int b) { (void)ev; (void)a; (void)b; }
