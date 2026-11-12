// Level graph generator of the deep missions (docs/DESIGN-R8.md 2.1). The global RNG is never used.
#include "delve.h"

static DelveGraph g;

uint32_t dv_hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7FEB352Du;
  x ^= x >> 15; x *= 0x846CA68Bu;
  return x ^ (x >> 16);
}
uint32_t dvh(uint32_t seed, uint32_t a, uint32_t salt) { return dv_hash(seed + a * 0x9E3779B1u + salt); }

static uint32_t xs(uint32_t* s) { *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s; }

// grid cell next to cell c in direction d, or -1 outside the 8x8 grid; the room in a cell, or -1
static int step(int c, int d) {
  int x = (c & 7) + (d == 1) - (d == 3), y = (c >> 3) + (d == 2) - (d == 0);
  return (x | y) & ~7 ? -1 : x | y << 3;
}
static int room_at(int c) {
  for (int i = 0; i < g.n && c >= 0; i++) if (g.cell[i] == c) return i;
  return -1;
}
int delve_nb(int r, int d) { return room_at(step(g.cell[r], d)); }

static void join(int a, int b, int d) {      // door from a to b in direction d, and back
  g.link[a] = (uint8_t)(g.link[a] | 1 << d);
  g.link[b] = (uint8_t)(g.link[b] | 1 << (d ^ 2));
}

// BFS over the doors, never through the exit room. label: from the exit, store in every room the direction of
// the next room toward it. Otherwise: first step toward the nearest room not in `seen` (-1 if none).
static int bfs(int from, uint32_t seen, int label) {
  uint8_t q[DELVE_MAX], first[DELVE_MAX];
  uint32_t vis = 1u << from;
  int qh = 0, qt = 0;
  q[qt++] = (uint8_t)from;
  while (qh < qt) {
    int u = q[qh++];
    for (int d = 0; d < 4; d++) {
      int v = ((g.link[u] >> d) & 1) ? delve_nb(u, d) : -1;
      if (v < 0 || v == g.exit || ((vis >> v) & 1)) continue;
      vis |= 1u << v;
      if (label) g.link[v] = (uint8_t)((g.link[v] & 15) | (d ^ 2) << 4);
      else {
        first[v] = u == from ? (uint8_t)d : first[u];
        if (!((seen >> v) & 1)) return first[v];
      }
      q[qt++] = (uint8_t)v;
    }
  }
  return -1;
}

const DelveGraph* delve_gen(uint32_t seed) {
  if (!seed) seed = 1;
  if (g.seed == seed) return &g;
  g.seed = 0;
  uint32_t s = dv_hash(seed) | 1;
  uint8_t depth[DELVE_MAX];
  int n = 20 + (int)(xs(&s) % 11), t = (int)(xs(&s) & 3);
  g.cell[0] = (uint8_t)(27 + (t & 1) + (t >> 1) * 8);   // one of the 4 centre cells
  g.link[0] = depth[0] = 0;
  g.n = 1;
  for (int k = 1; k < n; k++) {                // spanning tree: every new room gets a door to an existing one
    int p = (xs(&s) & 3) ? k - 1 : (int)(xs(&s) % (uint32_t)k), ds = (int)(xs(&s) & 3), done = 0;
    g.link[k] = 0;
    for (int i = 0; i < k && !done; i++) {
      int q = (p + k - i) % k;
      for (int j = 0; j < 4 && !done; j++) {
        int d = (ds + j) & 3, c = step(g.cell[q], d);
        if (c < 0 || room_at(c) >= 0) continue;
        g.cell[k] = (uint8_t)c;
        g.n = (uint8_t)(k + 1);
        join(q, k, d);
        depth[k] = (uint8_t)(depth[q] + 1);
        done = 1;
      }
    }
  }
  int ex = 1;                                  // deepest room (a leaf); lowest index on a tie
  for (int k = 2; k < n; k++) if (depth[k] > depth[ex]) ex = k;
  g.exit = (uint8_t)ex;
  for (int e = n / 6; e > 0; e--)              // loops: the exit keeps its single door
    for (int i = 0; i < 3; i++) {
      int r = (int)(xs(&s) % (uint32_t)n), d = (int)(xs(&s) & 3), b = delve_nb(r, d);
      if (r == ex || b < 0 || b == ex || ((g.link[r] >> d) & 1)) continue;
      join(r, b, d);
      break;
    }
  bfs(ex, 0, 1);
  g.seed = seed;
  return &g;
}

int delve_next(int cur, int exit_mode, uint32_t seen) {
  int d = exit_mode ? -1 : bfs(cur, seen, 0);
  return d < 0 ? (g.link[cur] >> 4) & 3 : d;   // everything else seen: toward the exit
}

int delve_kind(int r) {
  int l = g.link[r] & 15, k = 0;
  if (r == g.exit) k = DK_EXIT | DK_CHEST;
  else if (r == 0) k = DK_ENTRY;
  else {
    if (!(l & (l - 1)) || dvh(g.seed, (uint32_t)r, 1) % 5 == 0) k = DK_CHEST;
  }
  return k;
}
