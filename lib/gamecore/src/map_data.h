// Area scripts (design 11.5). Included only by world.cpp.
// A script is a list of MapOp {op, xy, wh, tile, arg}; xy = x<<4|y, wh = w<<4|h (0 = 16).
//   MO_GROUND   fill the rectangle with ground tile `tile`
//   MO_OBJ      fill the rectangle with object tile `tile` (0xFF clears)
//   MO_SCATTER  `arg` objects of `tile` on free ground inside the rectangle; positions come from a
//               generator seeded by the area and the op index (never random per visit); an object that
//               blocks is only placed when every spot and exit stays reachable from SP_ENTRY
//   MO_HOUSE    xy = top-left of a 3x2 house, tile = roof base (+0/1/2); the door is at (x+1, y+1)
//   MO_SPOT     xy = tile, tile = spot id (SP_*)
//   MO_EXIT     xy = tile, tile = target area, arg = spot of the target area where the hero arrives
//   MO_IF_PROJECT / MO_IF_SEASON  the NEXT op is only executed when project `arg` is built / when
//               the season is in the bit mask `arg` (bit s = season s; the design says "arg = season",
//               a mask lets one op say "spring or autumn")
// Each script is at most 60 ops. The village reference (x right, y down):
//   row 0   border trees, north gap (9,0) to the cave
//   rows 2-3 home (c1-3), shop (c6-8), tavern (c11-13); doors at row 3
//   row 4   main street, east road (14,4) to the forest
//   rows 6-8 plaza (c5-9) with campfire, benches; lane south to the river at row 12
//   pond with the dock at the right; river with the bridge project at (7,12)
#pragma once

struct MapOp { uint8_t op, xy, wh, tile, arg; };
enum { MO_END, MO_GROUND, MO_OBJ, MO_SCATTER, MO_HOUSE, MO_SPOT, MO_EXIT, MO_IF_PROJECT, MO_IF_SEASON };

#define XY(x, y) ((uint8_t)(((x) << 4) | (y)))
#define WH(w, h) ((uint8_t)(((w) << 4) | (h)))
#define ALL WH(15, 15)
#define SEASON_WINTER 8
#define SEASON_SPRING_AUTUMN 5
#define SEASON_NOT_WINTER 7
#define SEASON_SPRING_SUMMER 3
#define NONE_OBJ 0xFF

static const MapOp MAP_VILLAGE[] GC_ROM_ATTR = {
  {MO_GROUND, XY(0, 0), ALL, T_GRASS, 0},
  {MO_GROUND, XY(0, 4), WH(15, 1), T_PATH, 0},              // main street
  {MO_GROUND, XY(7, 5), WH(1, 7), T_PATH, 0},               // lane down to the river
  {MO_GROUND, XY(5, 6), WH(5, 3), T_PATH, 0},               // plaza
  {MO_GROUND, XY(7, 9), WH(3, 1), T_PATH, 0},
  {MO_GROUND, XY(9, 10), WH(1, 1), T_PATH, 0},
  {MO_GROUND, XY(11, 7), WH(2, 1), T_PATH, 0},              // in front of the stall
  {MO_GROUND, XY(9, 1), WH(1, 3), T_PATH, 0},               // trail to the north gap
  {MO_GROUND, XY(0, 12), WH(15, 1), T_WATER, 0},            // river
  {MO_GROUND, XY(11, 9), WH(3, 3), T_WATER, 0},             // pond
  {MO_OBJ, XY(0, 0), WH(15, 1), T_TREE, 0},                 // border
  {MO_OBJ, XY(9, 0), WH(1, 1), NONE_OBJ, 0},                // north gap
  {MO_GROUND, XY(9, 0), WH(1, 1), T_PATH, 0},
  {MO_OBJ, XY(0, 14), WH(15, 1), T_TREE, 0},
  {MO_HOUSE, XY(1, 2), 0, T_ROOF_RED, 0},
  {MO_HOUSE, XY(6, 2), 0, T_ROOF_BLUE, 0},
  {MO_HOUSE, XY(11, 2), 0, T_ROOF_BROWN, 0},
  {MO_OBJ, XY(5, 3), WH(1, 1), T_SIGN_SHOP, 0},
  {MO_OBJ, XY(10, 3), WH(1, 1), T_SIGN_TAVERN, 0},
  {MO_OBJ, XY(0, 3), WH(1, 1), T_LOGPILE, 0},
  {MO_OBJ, XY(0, 6), WH(4, 1), T_FENCE, 0},                 // garden fence
  {MO_OBJ, XY(7, 7), WH(1, 1), T_FIRE, 0},
  {MO_OBJ, XY(6, 8), WH(1, 1), T_BENCH, 0},
  {MO_OBJ, XY(8, 8), WH(1, 1), T_BENCH, 0},
  {MO_OBJ, XY(10, 10), WH(1, 1), T_DOCK, 0},
  {MO_OBJ, XY(3, 9), WH(1, 1), WT_BELL_TOP, 0},
  {MO_OBJ, XY(3, 10), WH(1, 1), WT_BELL_BASE, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_BOARD},    {MO_OBJ, XY(9, 5), WH(1, 1), WT_BOARD, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_GARDEN},   {MO_OBJ, XY(1, 7), WH(2, 2), WT_CROP_2, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_WELL},     {MO_OBJ, XY(10, 6), WH(1, 1), WT_WELL, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_STALL},    {MO_OBJ, XY(11, 6), WH(1, 1), WT_STALL_L, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_STALL},    {MO_OBJ, XY(12, 6), WH(1, 1), WT_STALL_R, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_LANTERNS}, {MO_OBJ, XY(1, 5), WH(1, 1), WT_LANTERN_POST, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_LANTERNS}, {MO_OBJ, XY(4, 5), WH(1, 1), WT_LANTERN_POST, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_LANTERNS}, {MO_OBJ, XY(10, 5), WH(1, 1), WT_LANTERN_POST, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_LANTERNS}, {MO_OBJ, XY(13, 5), WH(1, 1), WT_LANTERN_POST, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_FLOWERS},  {MO_OBJ, XY(2, 5), WH(2, 1), WT_FLOWERBED, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_FLOWERS},  {MO_OBJ, XY(11, 5), WH(2, 1), WT_FLOWERBED, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_STAGE},    {MO_OBJ, XY(5, 9), WH(1, 1), WT_STAGE_L, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_STAGE},    {MO_OBJ, XY(6, 9), WH(1, 1), WT_STAGE_R, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_FOUNTAIN}, {MO_OBJ, XY(5, 7), WH(1, 1), WT_FOUNTAIN, 0},
  {MO_IF_SEASON, 0, 0, 0, SEASON_WINTER}, {MO_GROUND, XY(13, 9), WH(1, 3), WT_ICE, 0},
  {MO_IF_SEASON, 0, 0, 0, SEASON_WINTER}, {MO_OBJ, XY(9, 8), WH(1, 1), WT_SNOWMAN, 0},
  // the bridge opens the way south
  {MO_IF_PROJECT, 0, 0, 0, PRJ_BRIDGE},   {MO_OBJ, XY(7, 14), WH(1, 1), NONE_OBJ, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_BRIDGE},   {MO_GROUND, XY(7, 13), WH(1, 2), T_PATH, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_BRIDGE},   {MO_OBJ, XY(7, 12), WH(1, 1), WT_BRIDGE_V, 0},
  {MO_END, 0, 0, 0, 0},
};
// (continued below: spots, exits, scatter; split in two arrays only to keep the file readable)
static const MapOp MAP_VILLAGE2[] GC_ROM_ATTR = {
  {MO_SPOT, XY(2, 4), 0, SP_ENTRY, 0},     {MO_SPOT, XY(7, 4), 0, SP_DOOR, 0},
  {MO_SPOT, XY(12, 4), 0, SP_TARGET_A, 0}, {MO_SPOT, XY(13, 4), 0, SP_TARGET_B, 0},
  {MO_SPOT, XY(9, 1), 0, SP_TARGET_C, 0},  {MO_SPOT, XY(10, 10), 0, SP_DOCK, 0},
  {MO_SPOT, XY(9, 6), 0, SP_BOARD, 0},     {MO_SPOT, XY(6, 6), 0, SP_SITE, 0},
  {MO_SPOT, XY(7, 6), 0, SP_PLAZA, 0},     {MO_SPOT, XY(3, 8), 0, SP_GARDEN, 0},
  {MO_SPOT, XY(6, 8), 0, SP_BENCH, 0},     {MO_SPOT, XY(0, 4), 0, SP_PILE, 0},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_BRIDGE},    {MO_SPOT, XY(7, 13), 0, SP_TARGET_D, 0},
  {MO_EXIT, XY(2, 3), 0, GQ_AREA_HOME, SP_ENTRY},
  {MO_EXIT, XY(7, 3), 0, GQ_AREA_SHOP, SP_ENTRY},
  {MO_EXIT, XY(12, 3), 0, GQ_AREA_TAVERN, SP_ENTRY},
  {MO_EXIT, XY(14, 4), 0, GQ_AREA_FOREST, SP_ENTRY},
  {MO_EXIT, XY(9, 0), 0, GQ_AREA_CAVE, SP_ENTRY},
  {MO_IF_PROJECT, 0, 0, 0, PRJ_BRIDGE},    {MO_EXIT, XY(7, 14), 0, GQ_AREA_SHORE, SP_ENTRY},
  {MO_SCATTER, XY(0, 9), WH(7, 3), T_TREE, 8},
  {MO_SCATTER, XY(0, 9), WH(7, 3), T_ROCK, 2},
  {MO_SCATTER, XY(0, 1), WH(15, 1), T_TREE, 5},
  {MO_SCATTER, XY(10, 7), WH(5, 2), T_TREE, 3},
  {MO_END, 0, 0, 0, 0},
};

static const MapOp MAP_FOREST[] GC_ROM_ATTR = {
  {MO_GROUND, XY(0, 0), ALL, T_GRASS, 0},
  {MO_GROUND, XY(0, 4), WH(8, 1), T_PATH, 0},
  {MO_GROUND, XY(7, 4), WH(1, 4), T_PATH, 0},
  {MO_GROUND, XY(7, 7), WH(8, 1), T_PATH, 0},
  {MO_GROUND, XY(9, 4), WH(4, 3), T_PATH, 0},               // Luma's clearing
  {MO_GROUND, XY(2, 9), WH(3, 3), T_WATER, 0},              // pond
  {MO_OBJ, XY(0, 0), WH(15, 1), T_TREE, 0},
  {MO_OBJ, XY(0, 14), WH(15, 1), T_TREE, 0},
  {MO_OBJ, XY(0, 0), WH(1, 15), T_TREE, 0},
  {MO_OBJ, XY(14, 0), WH(1, 15), T_TREE, 0},
  {MO_OBJ, XY(0, 4), WH(1, 1), NONE_OBJ, 0},
  {MO_OBJ, XY(14, 7), WH(1, 1), NONE_OBJ, 0},
  {MO_IF_SEASON, 0, 0, 0, SEASON_WINTER}, {MO_GROUND, XY(2, 9), WH(3, 1), WT_ICE, 0},
  {MO_SPOT, XY(1, 4), 0, SP_ENTRY, 0},     {MO_SPOT, XY(13, 7), 0, SP_TARGET_A, 0},
  {MO_SPOT, XY(10, 5), 0, SP_TARGET_B, 0}, {MO_SPOT, XY(5, 8), 0, SP_TARGET_C, 0},
  {MO_SPOT, XY(8, 11), 0, SP_TARGET_D, 0},
  {MO_EXIT, XY(0, 4), 0, GQ_AREA_VILLAGE, SP_TARGET_B},
  {MO_EXIT, XY(14, 7), 0, GQ_AREA_RUINS, SP_ENTRY},
  {MO_SCATTER, XY(1, 1), WH(13, 13), T_TREE, 22},
  {MO_SCATTER, XY(1, 1), WH(13, 13), T_ROCK, 2},
  {MO_IF_SEASON, 0, 0, 0, SEASON_SPRING_AUTUMN}, {MO_SCATTER, XY(1, 1), WH(13, 13), WT_MUSHROOM, 4},
  {MO_IF_SEASON, 0, 0, 0, SEASON_NOT_WINTER},    {MO_SCATTER, XY(1, 1), WH(13, 13), WT_HERB, 3},
  {MO_IF_SEASON, 0, 0, 0, SEASON_SPRING_SUMMER}, {MO_SCATTER, XY(1, 1), WH(13, 13), WT_BUSH, 3},
  {MO_END, 0, 0, 0, 0},
};

static const MapOp MAP_SHORE[] GC_ROM_ATTR = {
  {MO_GROUND, XY(0, 0), ALL, WT_SAND, 0},
  {MO_GROUND, XY(0, 10), WH(15, 5), WT_WAVE, 0},
  {MO_OBJ, XY(0, 0), WH(15, 1), WT_PINE, 0},
  {MO_OBJ, XY(0, 0), WH(1, 10), WT_PINE, 0},
  {MO_OBJ, XY(14, 0), WH(1, 10), WT_PINE, 0},
  {MO_OBJ, XY(7, 0), WH(1, 1), NONE_OBJ, 0},
  {MO_OBJ, XY(5, 10), WH(1, 2), T_DOCK, 0},
  {MO_OBJ, XY(11, 10), WH(1, 1), WT_BOAT, 0},
  {MO_SPOT, XY(7, 1), 0, SP_ENTRY, 0},     {MO_SPOT, XY(5, 11), 0, SP_DOCK, 0},
  {MO_SPOT, XY(10, 5), 0, SP_TARGET_A, 0}, {MO_SPOT, XY(3, 6), 0, SP_TARGET_B, 0},
  {MO_SPOT, XY(11, 8), 0, SP_TARGET_C, 0}, {MO_SPOT, XY(6, 7), 0, SP_TARGET_D, 0},
  {MO_EXIT, XY(7, 0), 0, GQ_AREA_VILLAGE, SP_TARGET_D},
  {MO_SCATTER, XY(1, 8), WH(13, 1), WT_REEDS, 4},
  {MO_SCATTER, XY(1, 1), WH(13, 4), WT_PINE, 4},
  {MO_SCATTER, XY(1, 2), WH(13, 7), WT_SAND_SHELL, 5},
  {MO_END, 0, 0, 0, 0},
};

static const MapOp MAP_CAVE[] GC_ROM_ATTR = {
  {MO_GROUND, XY(0, 0), ALL, WT_CAVE_FLOOR, 0},
  {MO_GROUND, XY(0, 0), WH(15, 1), WT_CAVE_WALL_TOP, 0},
  {MO_GROUND, XY(0, 1), WH(15, 1), WT_CAVE_WALL, 0},
  {MO_GROUND, XY(0, 14), WH(15, 1), WT_CAVE_WALL, 0},
  {MO_GROUND, XY(0, 2), WH(1, 12), WT_CAVE_WALL, 0},
  {MO_GROUND, XY(14, 2), WH(1, 12), WT_CAVE_WALL, 0},
  {MO_GROUND, XY(7, 14), WH(1, 1), WT_CAVE_EXIT, 0},
  {MO_OBJ, XY(5, 14), WH(1, 1), WT_TORCH, 0},
  {MO_OBJ, XY(9, 14), WH(1, 1), WT_TORCH, 0},
  {MO_OBJ, XY(3, 1), WH(1, 1), WT_TORCH, 0},
  {MO_OBJ, XY(11, 1), WH(1, 1), WT_TORCH, 0},
  {MO_SPOT, XY(7, 13), 0, SP_ENTRY, 0},    {MO_SPOT, XY(3, 4), 0, SP_TARGET_A, 0},
  {MO_SPOT, XY(11, 4), 0, SP_TARGET_B, 0}, {MO_SPOT, XY(3, 10), 0, SP_TARGET_C, 0},
  {MO_SPOT, XY(11, 10), 0, SP_TARGET_D, 0},
  {MO_EXIT, XY(7, 14), 0, GQ_AREA_VILLAGE, SP_TARGET_C},
  {MO_SCATTER, XY(1, 2), WH(13, 11), WT_ORE_ROCK, 6},
  {MO_SCATTER, XY(1, 2), WH(13, 11), WT_GEM_ROCK, 2},
  {MO_SCATTER, XY(1, 2), WH(13, 11), WT_STALAG, 5},
  {MO_END, 0, 0, 0, 0},
};

static const MapOp MAP_RUINS[] GC_ROM_ATTR = {
  {MO_GROUND, XY(0, 0), ALL, WT_RUIN_FLOOR, 0},
  {MO_GROUND, XY(0, 0), WH(15, 1), WT_RUIN_WALL, 0},
  {MO_GROUND, XY(0, 14), WH(15, 1), WT_RUIN_WALL, 0},
  {MO_GROUND, XY(0, 1), WH(1, 13), WT_RUIN_WALL, 0},
  {MO_GROUND, XY(14, 1), WH(1, 13), WT_RUIN_WALL, 0},
  {MO_OBJ, XY(0, 7), WH(1, 1), WT_RUIN_DOOR, 0},
  {MO_OBJ, XY(4, 0), WH(1, 1), WT_TORCH, 0},
  {MO_OBJ, XY(10, 0), WH(1, 1), WT_TORCH, 0},
  {MO_OBJ, XY(3, 3), WH(1, 1), WT_RUIN_PILLAR, 0},
  {MO_OBJ, XY(11, 3), WH(1, 1), WT_RUIN_PILLAR, 0},
  {MO_OBJ, XY(3, 11), WH(1, 1), WT_RUIN_PILLAR, 0},
  {MO_OBJ, XY(11, 11), WH(1, 1), WT_RUIN_PILLAR, 0},
  {MO_OBJ, XY(7, 2), WH(1, 1), WT_ALTAR, 0},
  {MO_OBJ, XY(12, 2), WH(1, 1), WT_CHEST, 0},
  {MO_OBJ, XY(13, 7), WH(1, 1), WT_STAIRS, 0},
  {MO_SPOT, XY(1, 7), 0, SP_ENTRY, 0},     {MO_SPOT, XY(7, 4), 0, SP_TARGET_A, 0},
  {MO_SPOT, XY(12, 3), 0, SP_TARGET_B, 0}, {MO_SPOT, XY(7, 10), 0, SP_TARGET_C, 0},
  {MO_SPOT, XY(4, 7), 0, SP_TARGET_D, 0},
  {MO_EXIT, XY(0, 7), 0, GQ_AREA_FOREST, SP_TARGET_A},
  {MO_SCATTER, XY(1, 2), WH(13, 11), T_ROCK, 3},
  {MO_END, 0, 0, 0, 0},
};

// interiors: a room x 3..11, y 4..11 inside walls, door mat at (7,12), black void around
#define ROOM_BASE \
  {MO_GROUND, XY(0, 0), ALL, WT_VOID, 0}, \
  {MO_GROUND, XY(3, 4), WH(9, 8), WT_FLOOR_WOOD, 0}, \
  {MO_GROUND, XY(2, 3), WH(11, 1), WT_WALL_IN, 0}, \
  {MO_GROUND, XY(2, 12), WH(11, 1), WT_WALL_IN, 0}, \
  {MO_GROUND, XY(2, 4), WH(1, 8), WT_WALL_IN, 0}, \
  {MO_GROUND, XY(12, 4), WH(1, 8), WT_WALL_IN, 0}, \
  {MO_GROUND, XY(7, 12), WH(1, 1), WT_DOOR_MAT, 0}, \
  {MO_SPOT, XY(7, 11), 0, SP_ENTRY, 0}

static const MapOp MAP_HOME[] GC_ROM_ATTR = {
  ROOM_BASE,
  {MO_GROUND, XY(4, 3), WH(1, 1), WT_WALL_IN_WINDOW, 0},
  {MO_GROUND, XY(7, 3), WH(1, 1), WT_WALL_IN_WINDOW, 0},
  {MO_GROUND, XY(6, 9), WH(3, 2), WT_RUG, 0},
  {MO_OBJ, XY(4, 4), WH(1, 1), WT_BED_TOP, 0},
  {MO_OBJ, XY(4, 5), WH(1, 1), WT_BED_BOT, 0},
  {MO_OBJ, XY(10, 4), WH(1, 1), WT_HEARTH, 0},
  {MO_OBJ, XY(6, 4), WH(1, 1), WT_SHELF, 0},
  {MO_OBJ, XY(11, 4), WH(1, 1), WT_PLANT, 0},
  {MO_OBJ, XY(8, 7), WH(1, 1), WT_TABLE, 0},
  {MO_OBJ, XY(7, 7), WH(1, 1), WT_STOOL, 0},
  {MO_OBJ, XY(9, 7), WH(1, 1), WT_STOOL, 0},
  {MO_SPOT, XY(4, 5), 0, SP_BED, 0},       {MO_SPOT, XY(7, 7), 0, SP_TABLE, 0},
  {MO_SPOT, XY(10, 5), 0, SP_BENCH, 0},    {MO_SPOT, XY(8, 9), 0, SP_TARGET_A, 0},
  {MO_EXIT, XY(7, 12), 0, GQ_AREA_VILLAGE, SP_ENTRY},
  {MO_END, 0, 0, 0, 0},
};

static const MapOp MAP_SHOP[] GC_ROM_ATTR = {
  ROOM_BASE,
  {MO_GROUND, XY(5, 3), WH(1, 1), WT_WALL_IN_WINDOW, 0},
  {MO_GROUND, XY(9, 3), WH(1, 1), WT_WALL_IN_WINDOW, 0},
  {MO_OBJ, XY(4, 4), WH(7, 1), WT_SHELF, 0},
  {MO_OBJ, XY(5, 6), WH(5, 1), WT_COUNTER, 0},
  {MO_OBJ, XY(3, 10), WH(1, 1), WT_BARREL, 0},
  {MO_OBJ, XY(11, 10), WH(1, 1), WT_BARREL, 0},
  {MO_OBJ, XY(3, 4), WH(1, 1), WT_PLANT, 0},
  {MO_SPOT, XY(7, 7), 0, SP_COUNTER, 0},   {MO_SPOT, XY(7, 5), 0, SP_TARGET_A, 0},
  {MO_EXIT, XY(7, 12), 0, GQ_AREA_VILLAGE, SP_DOOR},
  {MO_END, 0, 0, 0, 0},
};

static const MapOp MAP_TAVERN[] GC_ROM_ATTR = {
  ROOM_BASE,
  {MO_GROUND, XY(4, 3), WH(1, 1), WT_WALL_IN_WINDOW, 0},
  {MO_GROUND, XY(10, 3), WH(1, 1), WT_WALL_IN_WINDOW, 0},
  {MO_GROUND, XY(6, 9), WH(3, 2), WT_RUG, 0},
  {MO_OBJ, XY(7, 4), WH(4, 1), WT_SHELF, 0},
  {MO_OBJ, XY(11, 4), WH(1, 1), WT_HEARTH, 0},
  {MO_OBJ, XY(4, 5), WH(3, 1), WT_COUNTER, 0},
  {MO_OBJ, XY(8, 8), WH(1, 1), WT_TABLE, 0},
  {MO_OBJ, XY(10, 8), WH(1, 1), WT_TABLE, 0},
  {MO_OBJ, XY(8, 9), WH(1, 1), WT_STOOL, 0},
  {MO_OBJ, XY(10, 9), WH(1, 1), WT_STOOL, 0},
  {MO_OBJ, XY(3, 10), WH(1, 1), WT_BARREL, 0},
  {MO_SPOT, XY(5, 6), 0, SP_COUNTER, 0},   {MO_SPOT, XY(8, 9), 0, SP_TABLE, 0},
  {MO_SPOT, XY(11, 5), 0, SP_BENCH, 0},    {MO_SPOT, XY(5, 4), 0, SP_TARGET_A, 0},
  {MO_EXIT, XY(7, 12), 0, GQ_AREA_VILLAGE, SP_TARGET_A},
  {MO_END, 0, 0, 0, 0},
};
