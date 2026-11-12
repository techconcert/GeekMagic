// Shared internals of the game core (not part of the public API).
#pragma once
#include "gamecore.h"
#include "gq_types.h"
#include "gq_mod.h"
#include "assets_gen.h"

// ---- tile ids in assets/src/tiles.png ----
enum {
  T_GRASS = 0, T_TUFT = 1, T_FLOWER = 2, T_PATH = 3,
  T_WATER = 4, T_WATER2 = 5, T_TREE = 6, T_ROCK = 7,
  T_ROOF_BLUE = 8, T_ROOF_RED = 11, T_ROOF_BROWN = 14,   // + 0/1/2 = left/middle/right
  T_WALL_WIN_L = 17, T_WALL_DOOR = 18, T_WALL_WIN_R = 19,
  T_FENCE = 20, T_DOCK = 21, T_BENCH = 22, T_STUMP = 23, T_LOGPILE = 24,
  T_FIRE = 25, T_FIRE2 = 26, T_SIGN_SHOP = 27, T_SIGN_TAVERN = 28,
  T_NONE = 0xFF
};

// ---- items.png cells ----
enum {
  I_AXE_UP = 0, I_AXE_DOWN = 1, I_ROD = 2, I_BOBBER = 3, I_LOG = 4,
  I_BUBBLE = 5, I_ZZZ = 6, I_FISH = 7, I_COIN = 8, I_WOOD = 9, I_HEART = 10,
  I_STAR = 32, I_FACE = 34,
  I_NONE = -1
};

enum { DIR_DOWN = 0, DIR_UP = 1, DIR_LEFT = 2, DIR_RIGHT = 3 };
enum { SH_TILES = 0, SH_CHARS = 1, SH_ITEMS = 2 };
// chars sheet: hero down 0..2, up 3..5, left 8..10 (right = left flipped); index by DIR_DOWN/UP/LEFT
static const uint8_t HERO_CELL_BASE[3] = {0, 3, 8};

// palette indices
enum { P_OUTLINE = 1, P_SAND = 7, P_WHITE = 13, P_GREY = 14, P_YELLOW = 15 };

// ---- shared state (gamecore.cpp) ----
extern uint8_t  g_ground[GC_ROWS][GC_COLS];
extern uint8_t  g_obj[GC_ROWS][GC_COLS];
extern uint16_t g_pal[16];      // tinted palette for the world
extern uint16_t g_ui[16];       // untinted palette for HUD / dialogue
extern uint32_t g_dirty;
extern char     g_status[4][32];
extern int      g_hour, g_min, g_clock_valid;

uint32_t gc_rnd();
uint32_t gc_rng_get(void);
void gc_rng_set(uint32_t state);
int gc_scene_id(void);
void mark_rows(int y, int h);
void str_copy(char* dst, const char* src, int cap);
int str_len(const char* s);
void set_tint(int r256, int g256, int b256);

// ---- drawing into one 240x16 strip whose top is at y0 ----
gc_cell sheet_cell(int sheet, int cell);
void blit_tile(uint16_t* out, int x, gc_cell tile, int transparent);
void blit_sprite(uint16_t* out, int y0, int sx, int sy, gc_cell cell, int flip, const uint8_t* remap, const uint16_t* pal);
void fill_rect(uint16_t* out, int y0, int x, int y, int w, int h, uint16_t c);
void darken_rect(uint16_t* out, int y0, int x, int y, int w, int h);
void draw_text(uint16_t* out, int y0, int x, int y, const char* s, uint16_t c, int scale, int shadow);
void draw_text_n(uint16_t* out, int y0, int x, int y, const char* s, int n, uint16_t c, int scale);
void render_ground_row(uint16_t* out, int s, uint8_t water_frame, uint8_t fire_frame);

// ---- village scene (game.cpp) ----
void village_init(uint32_t seed);
void village_tick(uint32_t dt);
void village_render(int s, uint16_t* out);
void village_status_changed();
void gq_pump(void);   // deliver queued events (game.cpp)
