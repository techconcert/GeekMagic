// Step 1 / step 3 reference: frame hash of the village for seeds 1..3 after
// 10 simulated minutes. Kept only until step 4 changes behaviour on purpose.
#include "gq_test.h"

int main(int argc, char** argv) {
  static const uint32_t EXPECT[3] = {0xf94990f5u, 0x03ea710cu, 0x75dff7a3u};
  (void)argv;
  int print = argc > 1;
  for (uint32_t seed = 1; seed <= 3; seed++) {
    sim_start(seed);
    sim_minutes(10);
    uint32_t h = frame_hash();
    if (print) std::printf("seed %u hash 0x%08x\n", (unsigned)seed, (unsigned)h);
    else CHECK(h == EXPECT[seed - 1]);
  }
  if (!print) std::printf("PASS\n");
  return 0;
}
