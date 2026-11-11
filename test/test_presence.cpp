// Hero presence, wallet cap and daily income bound (design 11.2, 4.4, 3.1).
// 3 simulated days, seeds 1..5, sampled once per simulated second.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_types.h"

int main() {
  const long SECONDS = 3L * 86400;
  for (uint32_t seed = 1; seed <= 5; seed++) {
    sim_start(seed);
    long samples = 0, bad_presence = 0, over_cap = 0;
    long chopped_samples = 0, wood_ok = 0;
    int by_presence[5] = {0, 0, 0, 0, 0};
    int32_t last_day = gc_query(GQ_Q_DAY);
    int32_t day_start_earned = gc_query(GQ_Q_EARNED_TOTAL);
    int32_t day_start_trees = gc_query(GQ_Q_STAT_BASE + GQ_STAT_TREES);
    int32_t max_earned_day = 0;
    int32_t days_seen = 0;
    int32_t max_coins = 0;
    sim_seconds_cb(SECONDS, [&]() {
      samples++;
      int p = gc_query(GQ_Q_HERO_PRESENCE);
      if (p < 0 || p > 4) p = 0;
      by_presence[p]++;
      if (p == 0) bad_presence++;
      int32_t coins = gc_query(GQ_Q_COINS);
      if (coins > max_coins) max_coins = coins;
      if (coins > gc_query(GQ_Q_COIN_CAP)) over_cap++;
      int32_t day = gc_query(GQ_Q_DAY);
      if (day != last_day) {
        int32_t earned = gc_query(GQ_Q_EARNED_TOTAL) - day_start_earned;
        if (earned > max_earned_day) max_earned_day = earned;
        days_seen++;
        last_day = day;
        day_start_earned = gc_query(GQ_Q_EARNED_TOTAL);
        day_start_trees = gc_query(GQ_Q_STAT_BASE + GQ_STAT_TREES);
      }
      if (days_seen >= 1 && gc_query(GQ_Q_STAT_BASE + GQ_STAT_TREES) > day_start_trees) {
        chopped_samples++;
        if (gc_query(GQ_Q_ITEM_BASE + GQ_ITEM_WOOD) >= 3) wood_ok++;
      }
    });
    int32_t earned_last = gc_query(GQ_Q_EARNED_TOTAL) - day_start_earned;
    if (earned_last > max_earned_day) max_earned_day = earned_last;
    std::printf("seed %u: samples %ld, presence none/sprite/door/postcard/curtain %d/%d/%d/%d/%d, max coins %d, "
                "max earned/day %d, days %d, wood>=3 while chopped-today %ld/%ld, final coins %d stars %d\n",
                (unsigned)seed, samples, by_presence[0], by_presence[1], by_presence[2], by_presence[3], by_presence[4],
                (int)max_coins, (int)max_earned_day, (int)days_seen, wood_ok, chopped_samples,
                (int)gc_query(GQ_Q_COINS), (int)gc_query(GQ_Q_STARS));
    CHECK(bad_presence == 0);                       // the hero is accounted for in 100% of samples
    CHECK(over_cap == 0);                           // coins never exceed the wallet cap
    CHECK(max_earned_day <= 60 + 3 * 14);           // till plus three errands
    CHECK(days_seen >= 2);
    CHECK(chopped_samples == 0 || wood_ok * 100 >= chopped_samples * 80);
    CHECK(gc_query(GQ_Q_COINS) <= 250);
    CHECK(max_coins <= 250);
    // both stopgaps were really exercised
    CHECK(by_presence[GQ_PRES_DOOR] > 0);
    CHECK(by_presence[GQ_PRES_POSTCARD] > 0);
  }
  std::printf("PASS\n");
  return 0;
}
