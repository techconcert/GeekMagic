// Level graph of the "deep" missions (docs/DESIGN-R8.md section 2). Private to the game core.
// A level is a pure function of its seed: 20..30 rooms on an 8x8 grid, integer only, no allocation.
#pragma once
#include "platform.h"

#define DELVE_MAX 30

struct DelveGraph {          // 68 bytes of RAM
  uint32_t seed;             // seed the arrays were generated from (0 = none): lazy regeneration
  uint8_t  n, exit;          // room count 20..30, index of the exit room
  uint8_t  cell[DELVE_MAX];  // grid position x | y << 3
  uint8_t  link[DELVE_MAX];  // bits 0-3: doors N,E,S,W; bits 4-5: direction of the next room toward the exit
};

enum { DK_EXIT = 1, DK_CHEST = 2, DK_DEN = 4, DK_ENTRY = 8 };   // delve_kind() bits

uint32_t dv_hash(uint32_t x);                       // stateless 32-bit mixer
uint32_t dvh(uint32_t seed, uint32_t a, uint32_t salt);   // dv_hash(seed + a * golden + salt)
const DelveGraph* delve_gen(uint32_t seed);         // seed 0 counts as 1; regenerates only when the seed changed
int delve_nb(int r, int d);                         // room next to r in direction d (0 N, 1 E, 2 S, 3 W) or -1; ignores doors
int delve_next(int cur, int exit_mode, uint32_t seen);   // direction of the next door (section 3.3)
int delve_kind(int r);                              // DK_* bits of room r
