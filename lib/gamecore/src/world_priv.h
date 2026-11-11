// Private header shared by world.cpp, hero.cpp, npc.cpp, ui.cpp and game.cpp
// (the village scene). Chunk 3 owns it together with the three world files.
#pragma once
#include "gc_internal.h"

#define TI(x, y) ((uint8_t)((y) * GC_COLS + (x)))
#define TX(t) ((t) % GC_COLS)
#define TY(t) ((t) / GC_COLS)

// Row/column reference (15x15 tiles of 16 px):
//   row 2-3  : home (red roof, c1-3), shop (blue, c6-8), tavern (brown, c11-13)
//   row 4    : main street, exit east at c14
//   rows 6-8 : plaza with campfire and benches
//   bottom-left forest, bottom-right pond with a dock
static const uint8_t HOME_DOOR_X = 2, SHOP_DOOR_X = 7, TAVERN_DOOR_X = 12, DOOR_Y = 3;
static const uint8_t STREET_Y = 4;
static const uint8_t PILE_FRONT_X = 0;          // stand at (0,4) facing up to the woodpile at (0,3)
static const uint8_t DOCK_X = 9, DOCK_Y = 11;   // fishing spot, facing right
static const uint8_t BENCH_A_X = 6, BENCH_B_X = 8, BENCH_Y = 8;
static const uint8_t EXIT_X = 14;

// ---- actors (indices match GQ_NPC_HERO, GQ_NPC_MIRA, GQ_NPC_BRAM, GQ_NPC_PIP)
enum { A_HERO = 0, A_MIRA, A_BRAM, A_PIP, A_COUNT };

struct Actor {
  int16_t x, y;            // top-left pixel
  uint8_t dir, step, moved, visible;
  uint8_t ms_per_px;
  uint16_t move_acc;
  uint8_t path[64];
  uint8_t plen, pidx;
  int8_t item;             // I_* drawn in the hands, or I_NONE
  int8_t emote;            // I_* drawn above the head, or I_NONE
  int16_t ex, ey;          // emote position override (e.g. at a door), ex<0 = above head
  uint8_t bob;             // bobber offset while fishing
  int8_t manual_dx;        // walking off/onto the screen edge
  // ai
  uint8_t act, phase, talking, home;
  int32_t timer;           // ms
  uint8_t counter;
  // dirty tracking
  uint32_t sig;
  int16_t box_y, box_h;
};

extern Actor A[A_COUNT];

// ---- world.cpp
int  rnd_range(int lo, int hi);
void recompute_walk(void);
int  walkable(int x, int y);
int  aligned(const Actor* a);
int  tile_x(const Actor* a);
int  tile_y(const Actor* a);
int  find_path(Actor* a, int gx, int gy);
int  arrived(const Actor* a);
void face(Actor* a, int tx, int ty);
void place(Actor* a, int tx, int ty);
void world_init(void);
void world_tick(uint32_t dt);
void world_render(int strip, uint16_t* out);

// ---- hero.cpp, npc.cpp
void hero_tick(Actor* h, uint32_t dt);
void npc_tick(int i, uint32_t dt);

// ---- ui.cpp
void ui_init(void);
void ui_tick(uint32_t dt);
void ui_render(int strip, uint16_t* out);
void ui_say(int who, const char* text, int32_t hold_ms);   // dialogue box; text is copied (40 bytes)
int  ui_dialogue_done(void);
void itoa_into(char* p, int v);

// ---- calendar.cpp
void cal_init(void);
void cal_tick(uint32_t dt);
int  cal_minute(void);
int  cal_is_night(void);

// ---- shared game state (game.cpp)
extern int g_wood, g_gold, g_fish;
