// Private header shared by world.cpp, hero.cpp, npc.cpp, ui.cpp and game.cpp
// (the village scene). Chunk 3 owns it together with the world files.
#pragma once
#include "gc_internal.h"

#define TI(x, y) ((uint8_t)((y) * GC_COLS + (x)))
#define TX(t) ((t) % GC_COLS)
#define TY(t) ((t) / GC_COLS)

// ---- art ids of design 10.5 that gc_internal.h does not name. Prefixed W so they cannot
// collide with names other chunks add (WT_ = tile, WI_ = items sheet, WC_ = chars sheet).
enum {
  WT_VOID = 29, WT_ICE, WT_SNOWMAN,
  WT_BOARD = 32, WT_WELL, WT_LANTERN_POST, WT_LANTERN_LIT, WT_BRIDGE_H, WT_BRIDGE_V, WT_CROP_0, WT_CROP_1, WT_CROP_2,
  WT_STALL_L = 41, WT_STALL_R, WT_FLOWERBED, WT_BELL_BASE, WT_BELL_TOP, WT_BELL_TOP2, WT_FOUNTAIN, WT_FOUNTAIN2,
  WT_STAGE_L = 49, WT_STAGE_R, WT_FEST_TABLE, WT_FEST_LIGHTS, WT_BUSH, WT_MUSHROOM, WT_HERB,
  WT_PINE = 56, WT_SAND, WT_SAND_SHELL, WT_WAVE, WT_WAVE2, WT_REEDS, WT_BOAT, WT_CHEST,
  WT_CAVE_FLOOR = 64, WT_CAVE_WALL, WT_CAVE_WALL_TOP, WT_ORE_ROCK, WT_GEM_ROCK, WT_STALAG, WT_CAVE_EXIT,
  WT_RUIN_FLOOR = 71, WT_RUIN_WALL, WT_RUIN_PILLAR, WT_RUIN_DOOR, WT_ALTAR, WT_CHEST_OPEN, WT_TORCH, WT_TORCH2, WT_STAIRS,
  WT_FLOOR_WOOD = 80, WT_WALL_IN, WT_WALL_IN_WINDOW, WT_DOOR_MAT, WT_BED_TOP, WT_BED_BOT, WT_TABLE, WT_STOOL, WT_COUNTER,
  WT_SHELF, WT_BARREL, WT_HEARTH, WT_HEARTH2, WT_RUG, WT_PLANT, WT_WINDOW_MOON
};
enum {
  WI_SWORD_UP = 11, WI_SWORD_DOWN, WI_PICK_UP, WI_PICK_DOWN, WI_HAMMER,
  WI_STONE = 16, WI_MUSHROOM, WI_BERRY, WI_FLOWER, WI_SHELL, WI_ORE, WI_GEM, WI_CROP, WI_SEED, WI_HERB, WI_GEL,
  WI_HEART_EMPTY = 33, WI_EXCL = 35, WI_SCROLL = 36, WI_POOF = 38, WI_POOF2 = 39
};
// chars sheet (design 10.5)
enum {
  WC_BED = 6, WC_SIT = 7, WC_SWING_LEFT = 11, WC_SWING_DOWN = 12, WC_HURT = 13, WC_CHEER = 14, WC_SWING_UP = 15,
  WC_HEAD = 16,            // + head*3 + dir (0 down, 1 up, 2 left; right = flipped left)
  WC_MON = 40,             // slime, bat, crab, ghost: A/B each
  WC_CRIT = 48,            // chicken, frog, butterfly: A/B; firefly 54; bird 55
  WC_CAT = 56,             // down, side A, side B, asleep
  WC_BOSS = 60             // A, B, hurt
};

// ---- spots: named tiles of an area (16 at most), looked up with world_spot()
enum { SP_ENTRY, SP_DOOR, SP_COUNTER, SP_BED, SP_TABLE, SP_DOCK, SP_BOARD, SP_SITE,
       SP_TARGET_A, SP_TARGET_B, SP_TARGET_C, SP_TARGET_D, SP_PLAZA, SP_GARDEN, SP_BENCH, SP_PILE, SP_COUNT };
// village projects (bit index in GqSave.projects; the order of the goal table in design 3.5)
enum { PRJ_BOARD, PRJ_GARDEN, PRJ_BRIDGE, PRJ_WELL, PRJ_STALL, PRJ_LANTERNS, PRJ_FLOWERS, PRJ_BELL, PRJ_STAGE, PRJ_FOUNTAIN };

// Row/column reference of the village (15x15 tiles of 16 px): see map_data.h.
static const uint8_t PILE_FRONT_X = 0;

// ---- actors: slot 0 is the hero, 1..7 are villagers, the cat, critters and monsters
#define A_COUNT 8
#define A_HERO 0
enum { AK_NONE = 0, AK_HERO, AK_VILLAGER, AK_CRITTER, AK_MONSTER, AK_CAT };
enum { POSE_NORMAL = 0, POSE_BED, POSE_SIT, POSE_SWING, POSE_HURT, POSE_CHEER };
enum { CR_CHICKEN, CR_FROG, CR_BUTTERFLY, CR_FIREFLY, CR_BIRD };

struct Actor {
  int16_t x, y;            // top-left pixel
  uint8_t kind, id;        // AK_*, npc id / critter type / monster type
  uint8_t dir, step, moved, visible, pose, anim;
  uint8_t ms_per_px;
  uint16_t move_acc;
  uint8_t path[48];
  uint8_t plen, pidx;
  int8_t item;             // I_* drawn in the hands, or I_NONE
  int8_t emote;            // I_* drawn above the head, or I_NONE
  int16_t ex, ey;          // emote position override (e.g. at a door), ex<0 = above head
  uint8_t bob;             // bobber offset while fishing
  uint8_t act, phase, talking, counter, home;
  int32_t timer;           // ms
  uint16_t anim_acc;
  uint32_t sig;
  int16_t box_y, box_h;
};

extern Actor A[A_COUNT];

// ---- world.cpp (the rest of the world API is in gq_mod.h)
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
int  world_spot(int spot, int* tx, int* ty);              // 1 = this area has the spot
int  world_find_exit(int target_area, int* tx, int* ty, int* arg);
int  world_change_area(int area, int spot, int show_hero);  // through the curtain; 0 = busy or bad
int  world_transition_busy(void);
void world_instant_area(int area, int spot);              // no curtain (init, debug, last-resort)
void world_build_area(int area);                          // builds the map only (tests)
void world_obj_change(int tx, int ty, int now, int regrow_s);
int  world_area_open(int area);                           // may the hero go there today?
int  world_next_hop(int from, int to);
void world_chime(void);
void actor_clear(Actor* a);
Actor* actor_alloc(void);

// ---- hero.cpp, npc.cpp
void hero_reset(void);
void hero_tick(Actor* h, uint32_t dt);
void hero_update_presence(void);
void hero_force_activity(int gq_act);
void hero_force_delve(int biome, uint32_t seed);   // GQ_DBG_DELVE
const char* hero_place_name(int place);
int  hero_place_icon(int place);   // tile cell used as the place icon on the (retired) postcard
int  hero_max_hearts(void);
void npc_populate(void);                    // fills the actor slots for the loaded area
void npc_sync(void);                        // hourly: villagers arrive and leave
void npc_tick(int i, uint32_t dt);
int  npc_where(int npc, int* area, int* tx, int* ty, int* working);   // 1 = present somewhere
int  npc_actor(int npc);                    // slot of the villager or -1
void npc_set_talking(int slot, int on);
int  npc_monster_slot(int skip);            // a live monster in this area or -1

// ---- ui.cpp
void itoa_into(char* p, int v);
