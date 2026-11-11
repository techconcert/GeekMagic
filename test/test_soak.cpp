// Soak skeleton (design 13.1): runs the whole game for a few simulated days and prints the
// daily ledger. The integration pass extends it to 30 days and the full invariant list.
// Checked here every simulated second: presence, wallet cap. Every simulated hour: status text.
#include "gq_test.h"
#include "../lib/gamecore/src/gq_mod.h"

static int days_to_run(int argc, char** argv) { return argc > 1 ? std::atoi(argv[1]) : 3; }

int main(int argc, char** argv) {
  const int DAYS = days_to_run(argc, argv);
  for (uint32_t seed = 1; seed <= 3; seed++) {
    sim_start(seed);
    std::printf("seed %u\n day   earned  spent  coins  stars  goal  errands\n", (unsigned)seed);
    int32_t day = gc_query(GQ_Q_DAY);
    int32_t earned0 = 0, spent0 = 0;
    long sec = 0;
    int hour_marks = 0;
    sim_seconds_cb((long)DAYS * 86400, [&]() {
      sec++;
      CHECK(gc_query(GQ_Q_HERO_PRESENCE) != 0);
      CHECK(gc_query(GQ_Q_COINS) <= gc_query(GQ_Q_COIN_CAP));
      if (sec % 3600 == 1800) {
        // status band text: 1..19 characters of upper case words, digits and ": "
        char st[24];
        hero_status_text(st, 24);
        size_t n = std::strlen(st);
        CHECK(n >= 1 && n <= 19);
        for (size_t i = 0; i < n; i++) CHECK((st[i] >= 'A' && st[i] <= 'Z') || st[i] == ' ' || st[i] == ':' || st[i] == '\'');
        hour_marks++;
      }
      int32_t d = gc_query(GQ_Q_DAY);
      if (d != day) {
        int32_t e = gc_query(GQ_Q_EARNED_TOTAL), s = gc_query(GQ_Q_SPENT_TOTAL);
        std::printf(" %3d  %7d %6d %6d %6d  %4d  %7d\n", (int)day, (int)(e - earned0), (int)(s - spent0),
                    (int)gc_query(GQ_Q_COINS), (int)gc_query(GQ_Q_STARS), (int)gc_query(GQ_Q_GOAL),
                    (int)gc_query(GQ_Q_ERRANDS_TODAY));
        earned0 = e; spent0 = s; day = d;
      }
    });
    CHECK(hour_marks == DAYS * 24);
  }
  std::printf("PASS\n");
  return 0;
}
