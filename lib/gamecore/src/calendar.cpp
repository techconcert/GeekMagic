// Calendar: minute of day and the day/night tint keyframes.
// (Split out of village.cpp; chunk 6 grows this into days, seasons and weather.)
#include "world_priv.h"

static int      g_minute = 9 * 60;      // minute of day used by the village
static uint32_t g_minute_acc;
static int      g_tint_key = -1;

int cal_minute(void) { return g_minute; }
int cal_is_night(void) { return g_minute >= 22 * 60 || g_minute < 6 * 60; }

static void update_tint() {
  // brightness keyframes (minute, r, g, b) - linear in between
  static const int16_t K[][4] = {
    {0, 90, 105, 180}, {300, 90, 105, 180}, {390, 220, 190, 200}, {450, 256, 256, 256},
    {1050, 256, 256, 256}, {1110, 256, 200, 160}, {1170, 170, 140, 170}, {1230, 90, 105, 180},
    {1440, 90, 105, 180},
  };
  int m = g_minute, i = 0;
  while (K[i + 1][0] < m) i++;
  int span = K[i + 1][0] - K[i][0];
  int f = span ? (m - K[i][0]) * 256 / span : 0;
  int r = K[i][1] + ((K[i + 1][1] - K[i][1]) * f >> 8);
  int g = K[i][2] + ((K[i + 1][2] - K[i][2]) * f >> 8);
  int b = K[i][3] + ((K[i + 1][3] - K[i][3]) * f >> 8);
  int key = (r >> 3) << 12 | (g >> 3) << 6 | (b >> 3);   // ignore tiny changes
  if (key == g_tint_key) return;
  g_tint_key = key;
  set_tint(r, g, b);
  gc_mark_all_dirty();
}

void cal_init(void) {
  g_minute_acc = 0;
  g_tint_key = -1;
  if (g_clock_valid) g_minute = g_hour * 60 + g_min;
  update_tint();
}

void cal_tick(uint32_t dt) {
  // time of day: wall clock when known, otherwise our own clock
  if (g_clock_valid) g_minute = g_hour * 60 + g_min;
  else {
    g_minute_acc += dt;
    while (g_minute_acc >= 60000) { g_minute_acc -= 60000; g_minute = (g_minute + 1) % 1440; g_dirty |= 1u; }
  }
  update_tint();
}
