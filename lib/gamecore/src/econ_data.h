// Economy tables (design 3.3, 3.5, 3.8). Flash only, read through GC_RD8 / GC_RD16.
// Included by econ.cpp (and by test_econ.cpp, which checks the tables). Needs gc_internal.h first.
#pragma once

// ---------------------------------------------------------------- items
static const uint8_t ITEM_CAP[GQ_ITEM_COUNT] GC_ROM_ATTR = {
  30, 30, 20, 20, 20, 20, 20, 20, 9, 20, 9, 20, 20, 0, 0, 0,    // goods 0..15
  1, 1, 4, 1, 1, 1, 9, 0                                        // key items 16..23
};
static const uint8_t SELL_PRICE[GQ_ITEM_COUNT] GC_ROM_ATTR = {
  1, 2, 3, 2, 2, 1, 2, 5, 20, 4, 0, 3, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
// Seasons in which a good is found (bit per season). 0x0F = all year. A good sold in season
// pays +1 coin; out-of-season forage cannot be found.
static const uint8_t ITEM_SEASON[GQ_ITEM_COUNT] GC_ROM_ATTR = {
  0x0F, 0x0F, 0x0F, 0x05, 0x02, 0x03, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
  0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F
};
// Item id -> items.png cell (design 10.5). Cells without art yet show the shared placeholder.
static const uint8_t ITEM_ICON[GQ_ITEM_COUNT] GC_ROM_ATTR = {
  9, 16, 7, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 0, 0, 0,
  27, 28, 29, 30, 28, 31, 24, 0
};

// ---------------------------------------------------------------- goals
// 10 bytes per goal: kind, id, price_lo, price_hi, mat_item, mat_count, mat2_item, mat2_count,
// prereq_goal (0xFF = none), min_day (0-based game day, compared with GqSave::day).
// kind 0 = tool upgrade (id = GQ_TOOL_*; the level it sets is 1 + the number of earlier goals
// for the same tool), kind 1 = village project (id = project bit 0..9).
#define GOAL_COUNT 22
#define GK_TOOL 0
#define GK_PROJECT 1
#define NO_ITEM 0xFF
#define NO_PRE 0xFF
#define G10(kind, id, price, m1, n1, m2, n2, pre, day) \
  (kind), (id), (uint8_t)((price) & 255), (uint8_t)((price) >> 8), (m1), (n1), (m2), (n2), (pre), (day)
static const uint8_t GOALS[GOAL_COUNT * 10] GC_ROM_ATTR = {
  G10(GK_TOOL,    GQ_TOOL_PURSE,   60, NO_ITEM, 0, NO_ITEM, 0, NO_PRE, 1),                     //  0 Purse II
  G10(GK_PROJECT, 0,               40, GQ_ITEM_WOOD, 5, NO_ITEM, 0, NO_PRE, 2),                //  1 Notice board
  G10(GK_TOOL,    GQ_TOOL_AXE,    100, NO_ITEM, 0, NO_ITEM, 0, NO_PRE, 3),                     //  2 Axe II
  G10(GK_PROJECT, 1,               80, GQ_ITEM_WOOD, 10, NO_ITEM, 0, NO_PRE, 4),               //  3 Garden plot
  G10(GK_TOOL,    GQ_TOOL_ROD,    100, NO_ITEM, 0, NO_ITEM, 0, NO_PRE, 5),                     //  4 Rod II
  G10(GK_PROJECT, 2,              120, GQ_ITEM_WOOD, 20, NO_ITEM, 0, NO_PRE, 7),               //  5 Bridge
  G10(GK_TOOL,    GQ_TOOL_PICK,    80, NO_ITEM, 0, NO_ITEM, 0, NO_PRE, 9),                     //  6 Pick I
  G10(GK_TOOL,    GQ_TOOL_PURSE,  200, NO_ITEM, 0, NO_ITEM, 0, 0, 10),                         //  7 Purse III
  G10(GK_PROJECT, 3,              150, GQ_ITEM_STONE, 20, NO_ITEM, 0, 6, 12),                  //  8 Well
  G10(GK_TOOL,    GQ_TOOL_LANTERN, 120, NO_ITEM, 0, NO_ITEM, 0, NO_PRE, 14),                   //  9 Lantern
  G10(GK_TOOL,    GQ_TOOL_BOOTS,  150, NO_ITEM, 0, NO_ITEM, 0, NO_PRE, 16),                    // 10 Boots
  G10(GK_PROJECT, 4,              250, GQ_ITEM_WOOD, 25, NO_ITEM, 0, NO_PRE, 18),              // 11 Market stall
  G10(GK_PROJECT, 5,              200, GQ_ITEM_ORE, 10, NO_ITEM, 0, 14, 20),                   // 12 Street lanterns (ore needs Pick II)
  G10(GK_TOOL,    GQ_TOOL_SWORD,  250, NO_ITEM, 0, NO_ITEM, 0, NO_PRE, 21),                    // 13 Sword II
  G10(GK_TOOL,    GQ_TOOL_PICK,   300, NO_ITEM, 0, NO_ITEM, 0, 6, 23),                         // 14 Pick II
  G10(GK_PROJECT, 6,              100, GQ_ITEM_FLOWER, 20, NO_ITEM, 0, NO_PRE, 24),            // 15 Flower beds
  G10(GK_PROJECT, 7,              400, GQ_ITEM_STONE, 30, GQ_ITEM_SHARD, 4, 6, 27),            // 16 Bell tower repair
  G10(GK_TOOL,    GQ_TOOL_PURSE,  450, NO_ITEM, 0, NO_ITEM, 0, 7, 30),                         // 17 Purse IV
  G10(GK_TOOL,    GQ_TOOL_AXE,    350, NO_ITEM, 0, NO_ITEM, 0, 2, 34),                         // 18 Axe III
  G10(GK_TOOL,    GQ_TOOL_ROD,    350, NO_ITEM, 0, NO_ITEM, 0, 4, 38),                         // 19 Rod III
  G10(GK_PROJECT, 8,              300, GQ_ITEM_WOOD, 30, NO_ITEM, 0, 11, 42),                  // 20 Festival stage
  G10(GK_PROJECT, 9,              500, GQ_ITEM_STONE, 30, GQ_ITEM_GEM, 2, 14, 50),             // 21 Fountain
};

// Short names for the band and the buy scene: at most 9 characters (+ NUL pad to 10).
#define GOAL_NAME_LEN 10
static const char GOAL_NAME[GOAL_COUNT][GOAL_NAME_LEN] GC_ROM_ATTR = {
  "PURSE II", "BOARD", "AXE II", "GARDEN", "ROD II", "BRIDGE", "PICK I", "PURSE III",
  "WELL", "LANTERN", "BOOTS", "MARKET", "LAMPS", "SWORD II", "PICK II", "FLOWERS",
  "TOWER", "PURSE IV", "AXE III", "ROD III", "STAGE", "FOUNTAIN",
};

// ---------------------------------------------------------------- achievements
// 5 bytes: kind, index, threshold_lo, threshold_hi, text_id.
//   AK_STAT    stat[index] >= threshold          AK_SEEN   all of seen byte `index` (threshold = 8 bits)
//   AK_CHAPTER chapter >= threshold              AK_STARS  stars >= threshold
//   AK_BUILT   projects built >= threshold       AK_DAY    day >= threshold
//   AK_FRIENDS every villager's friendship >= threshold
enum { AK_STAT, AK_SEEN, AK_CHAPTER, AK_STARS, AK_BUILT, AK_DAY, AK_FRIENDS };
#define ACH_COUNT 24
#define A5(kind, index, thr, text) (kind), (index), (uint8_t)((thr) & 255), (uint8_t)((thr) >> 8), (text)
static const uint8_t ACH[ACH_COUNT * 5] GC_ROM_ATTR = {
  A5(AK_STAT, GQ_STAT_TREES, 100, 0),   A5(AK_STAT, GQ_STAT_FISH, 50, 1),     A5(AK_STAT, GQ_STAT_FORAGE, 50, 2),
  A5(AK_STAT, GQ_STAT_MINED, 50, 3),    A5(AK_STAT, GQ_STAT_MONSTERS, 25, 4), A5(AK_STAT, GQ_STAT_QUESTS, 20, 5),
  A5(AK_STAT, GQ_STAT_ERRANDS, 30, 6),  A5(AK_STAT, GQ_STAT_CHATS, 60, 7),    A5(AK_STAT, GQ_STAT_MEALS, 100, 8),
  A5(AK_STAT, GQ_STAT_FESTIVALS, 4, 9), A5(AK_STAT, GQ_STAT_FAINTS, 3, 10),   A5(AK_STAT, GQ_STAT_GIFTS, 20, 11),
  A5(AK_SEEN, 0, 8, 12), A5(AK_SEEN, 1, 8, 13), A5(AK_SEEN, 2, 8, 14), A5(AK_SEEN, 3, 8, 15),
  A5(AK_CHAPTER, 0, 2, 16), A5(AK_CHAPTER, 0, 4, 17), A5(AK_CHAPTER, 0, 5, 18), A5(AK_CHAPTER, 0, 6, 19),
  A5(AK_STARS, 0, 1, 20), A5(AK_BUILT, 0, 10, 21), A5(AK_DAY, 0, 28, 22), A5(AK_FRIENDS, 0, 50, 23),
};
#define ACH_NAME_LEN 14
static const char ACH_NAME[ACH_COUNT][ACH_NAME_LEN] GC_ROM_ATTR = {
  "WOODCUTTER", "ANGLER", "FORAGER", "MINER", "MONSTER BANE", "QUESTER", "ERRAND BOY", "CHATTERBOX",
  "GOOD EATER", "PARTY GOER", "TOUGH NUT", "GIFT GIVER", "SPRING FISH", "SUMMER FISH", "AUTUMN FISH", "WINTER FISH",
  "NEW STORY", "HALFWAY", "NEARLY DONE", "THE END", "FIRST STAR", "BUILDER", "ONE YEAR", "BEST FRIEND",
};
