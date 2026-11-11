// Scene glue: init, tick and render order of the village scene (design 6.3, 10.4).
#include "world_priv.h"

int g_wood, g_gold, g_fish;

void village_init(uint32_t seed) {
  (void)seed;
  world_init();
  g_wood = g_fish = 0;
  g_gold = 3;
  ui_init();
  cal_init();
}

void village_tick(uint32_t dt) {
  cal_tick(dt);
  world_tick(dt);
  ui_tick(dt);
}

void village_render(int s, uint16_t* out) {
  world_render(s, out);
  ui_render(s, out);
}
