// Shared types of the game core: ids, enums, the saved state, events, query keys.
// The values here are FINAL (docs/DESIGN.md 10.3): saved bytes and ids must not change.
#pragma once
#include "platform.h"
#include <stddef.h>   // offsetof

// ---- ids
enum { GQ_AREA_VILLAGE, GQ_AREA_FOREST, GQ_AREA_SHORE, GQ_AREA_CAVE, GQ_AREA_RUINS,
       GQ_AREA_HOME, GQ_AREA_SHOP, GQ_AREA_TAVERN, GQ_AREA_COUNT };                    // 8
enum { GQ_NPC_HERO, GQ_NPC_MIRA, GQ_NPC_BRAM, GQ_NPC_PIP, GQ_NPC_ODDA, GQ_NPC_FINN,
       GQ_NPC_TESS, GQ_NPC_LUMA, GQ_NPC_WREN, GQ_NPC_MISO, GQ_NPC_COUNT };             // 10
enum { GQ_MON_SLIME, GQ_MON_BAT, GQ_MON_CRAB, GQ_MON_GHOST, GQ_MON_BOSS, GQ_MON_COUNT };
enum { GQ_ITEM_WOOD, GQ_ITEM_STONE, GQ_ITEM_FISH, GQ_ITEM_MUSHROOM, GQ_ITEM_BERRY, GQ_ITEM_FLOWER,
       GQ_ITEM_SHELL, GQ_ITEM_ORE, GQ_ITEM_GEM, GQ_ITEM_CROP, GQ_ITEM_SEED, GQ_ITEM_HERB, GQ_ITEM_GEL,
       GQ_ITEM_MAP = 16, GQ_ITEM_KEY, GQ_ITEM_SHARD, GQ_ITEM_PARCEL, GQ_ITEM_ROPE, GQ_ITEM_CHARM,
       GQ_ITEM_OIL, GQ_ITEM_COUNT = 24 };
enum { GQ_TOOL_AXE, GQ_TOOL_ROD, GQ_TOOL_PICK, GQ_TOOL_PURSE, GQ_TOOL_BOOTS, GQ_TOOL_LANTERN,
       GQ_TOOL_SWORD, GQ_TOOL_COUNT = 8 };
enum { GQ_SPRING, GQ_SUMMER, GQ_AUTUMN, GQ_WINTER };
enum { GQ_W_CLEAR, GQ_W_CLOUDY, GQ_W_RAIN, GQ_W_STORM, GQ_W_FOG, GQ_W_SNOW, GQ_W_WINDY, GQ_W_COUNT };
enum { GQ_ACT_NONE, GQ_ACT_WANDER, GQ_ACT_CHOP, GQ_ACT_FISH, GQ_ACT_FORAGE, GQ_ACT_MINE, GQ_ACT_GARDEN,
       GQ_ACT_SELL, GQ_ACT_BUY, GQ_ACT_EAT, GQ_ACT_SIT, GQ_ACT_CHAT, GQ_ACT_GIFT, GQ_ACT_QUEST,
       GQ_ACT_FIGHT, GQ_ACT_CAT, GQ_ACT_SLEEP, GQ_ACT_COUNT };                          // 16 + NONE
enum { GQ_STAT_TREES, GQ_STAT_FISH, GQ_STAT_FORAGE, GQ_STAT_MINED, GQ_STAT_MONSTERS, GQ_STAT_QUESTS,
       GQ_STAT_ERRANDS, GQ_STAT_CHATS, GQ_STAT_MEALS, GQ_STAT_FESTIVALS, GQ_STAT_FAINTS, GQ_STAT_GIFTS,
       GQ_STAT_COUNT };                                                                 // 12
enum { GQ_SRC_SALE, GQ_SRC_QUEST, GQ_SRC_FIND, GQ_SRC_DEBUG };                          // econ_earn source

// ---- events: gq_emit(ev, a, b). Dispatch order is fixed: econ, quest, calendar, world, ui.
enum {
  GQ_EV_HOUR,          // a = hour 0..23
  GQ_EV_NEW_DAY,       // a = day & 0xFF, b = season
  GQ_EV_WEATHER,       // a = new weather
  GQ_EV_CHOPPED, GQ_EV_CAUGHT /*a=collectible or 0xFF*/, GQ_EV_FORAGED /*a=item*/, GQ_EV_MINED /*a=item*/,
  GQ_EV_HARVESTED,
  GQ_EV_SOLD,          // a = item, b = units
  GQ_EV_BOUGHT,        // a = goal index
  GQ_EV_BUILT,         // a = project index
  GQ_EV_ATE, GQ_EV_SLEPT, GQ_EV_WOKE,
  GQ_EV_TALKED,        // a = npc
  GQ_EV_GIFTED,        // a = npc, b = item
  GQ_EV_ARRIVED,       // a = area, b = tile index (y*15+x)
  GQ_EV_AREA,          // a = new area (after the curtain opened)
  GQ_EV_SLAIN,         // a = monster
  GQ_EV_HURT, GQ_EV_FAINTED,
  GQ_EV_SCENE_DONE,    // a = scene id
  GQ_EV_QUEST_START, GQ_EV_QUEST_STEP /*a=quest,b=step*/, GQ_EV_QUEST_DONE /*a=quest*/,
  GQ_EV_FESTIVAL,      // a = festival 0..3, b = 1 start / 0 end
  GQ_EV_STAR, GQ_EV_ACHIEVEMENT /*a=index*/, GQ_EV_NEW_COLLECTIBLE /*a=index*/,
  GQ_EV_COUNT
};

// ---- gc_query keys
enum {
  GQ_Q_COINS, GQ_Q_COIN_CAP, GQ_Q_STARS, GQ_Q_FUND, GQ_Q_EARNED_TOTAL, GQ_Q_SPENT_TOTAL,
  GQ_Q_DAY, GQ_Q_MINUTE, GQ_Q_SEASON, GQ_Q_WEATHER, GQ_Q_FESTIVAL, GQ_Q_EVENT,
  GQ_Q_HERO_PRESENCE,   // 0 none (bug), 1 sprite visible, 2 door marker, 3 postcard, 4 curtain
  GQ_Q_HERO_AREA, GQ_Q_HERO_ACT, GQ_Q_HERO_HEARTS, GQ_Q_HERO_TILE,
  GQ_Q_GOAL, GQ_Q_GOAL_PRICE, GQ_Q_PROJECTS, GQ_Q_CHAPTER, GQ_Q_QUEST, GQ_Q_QUEST_STEP, GQ_Q_QUEST_COUNT,
  GQ_Q_TILL_LEFT, GQ_Q_ERRANDS_TODAY, GQ_Q_ACH_COUNT, GQ_Q_SEEN_COUNT,
  GQ_Q_CARD,            // 0 none, 1..4 card on screen
  GQ_Q_ACTORS_VISIBLE,
  GQ_Q_HERO_ABED,       // 1 while the hero lies in bed (sleep phases 2 and 4)
  GQ_Q_LIVE_WX,         // live weather as stored by gc_set_live_weather: 0..6, or -1 (GC_WX_NONE)
  GQ_Q_LIVE_TEMP,       // tenths of a degree Celsius as stored (clamped)
  GQ_Q_LIVE_FLAGS,      // GC_WXF_*
  GQ_Q_SLEEP_MODE,      // GC_OPT_SLEEP_MODE as stored
  GQ_Q_ITEM_BASE = 100, // + GQ_ITEM_*  -> count
  GQ_Q_TOOL_BASE = 130, // + GQ_TOOL_*  -> level
  GQ_Q_STAT_BASE = 140, // + GQ_STAT_*  -> value
  GQ_Q_FRIEND_BASE = 160 // + GQ_NPC_*  -> 0..100
};

// ---- gc_debug commands (preview and tests; they go through the same clamps as normal play)
enum { GQ_DBG_ADD_COINS, GQ_DBG_SET_DAY, GQ_DBG_SET_WEATHER, GQ_DBG_ADD_ITEM /*arg=item<<8|n*/,
       GQ_DBG_START_QUEST, GQ_DBG_SHOW_CARD, GQ_DBG_FORCE_ACT, GQ_DBG_BUY_GOAL, GQ_DBG_NEXT_DAY,
       GQ_DBG_FORCE_CHAT,   // arg = villager 1..8: start a chat with him now (0 = anyone)
       GQ_DBG_TALK_TURNS }; // arg = 2..4: chats play a built-in numbered script of that many turns (0 = off); tests and preview

// ---- hero presence (value of GQ_Q_HERO_PRESENCE)
enum { GQ_PRES_NONE, GQ_PRES_SPRITE, GQ_PRES_DOOR, GQ_PRES_POSTCARD, GQ_PRES_CURTAIN };

// ---- the game state, 192 bytes, little-endian, no padding. R5 saves it to flash (firmware/store.cpp); the core only exports and imports bytes.
// (The design calls the friendship array `friend`, which is a C++ keyword: it is `friend_` here.)
#define GQ_SAVE_SIZE    192
#define GQ_SAVE_MAGIC   0x53514747u   // "GGQS"
#define GQ_SAVE_VERSION 1
typedef struct {
  uint32_t magic;            //   0
  uint16_t version;          //   4
  uint16_t size;             //   6
  uint32_t crc32;            //   8  over bytes 12..191
  uint32_t seq;              //  12
  uint32_t epoch;            //  16
  uint32_t play_min;         //  20
  uint32_t rng;              //  24
  uint32_t earned_total;     //  28
  uint32_t spent_total;      //  32
  uint16_t day;              //  36
  uint16_t minute;           //  38
  uint16_t coins;            //  40
  uint16_t stars;            //  42
  uint16_t projects;         //  44
  uint16_t fund;             //  46
  uint16_t stat[12];         //  48
  uint8_t  weather;          //  72
  uint8_t  weather_hours;    //  73
  uint8_t  festival_flags;   //  74
  uint8_t  event_today;      //  75
  uint8_t  inv[24];          //  76
  uint8_t  till_left;        // 100
  uint8_t  errands_today;    // 101
  uint8_t  meals_today;      // 102
  uint8_t  gifts_today;      // 103
  uint8_t  tool[8];          // 104
  uint8_t  goal;             // 112
  uint8_t  chapter;          // 113
  uint8_t  q_active;         // 114
  uint8_t  q_step;           // 115
  uint8_t  q_count;          // 116
  uint8_t  hero_hearts;      // 117
  uint8_t  hero_area;        // 118
  uint8_t  year_legends;     // 119
  uint8_t  q_done[8];        // 120
  uint8_t  friend_[12];      // 128
  uint8_t  seen[8];          // 140
  uint8_t  ach[4];           // 148
  uint16_t chat_today;       // 152
  uint8_t  reserved[38];     // 154
} GqSave;

static_assert(sizeof(GqSave) == GQ_SAVE_SIZE, "GqSave must be 192 bytes");
static_assert(offsetof(GqSave, magic) == 0, "magic");
static_assert(offsetof(GqSave, version) == 4, "version");
static_assert(offsetof(GqSave, size) == 6, "size");
static_assert(offsetof(GqSave, crc32) == 8, "crc32");
static_assert(offsetof(GqSave, seq) == 12, "seq");
static_assert(offsetof(GqSave, epoch) == 16, "epoch");
static_assert(offsetof(GqSave, play_min) == 20, "play_min");
static_assert(offsetof(GqSave, rng) == 24, "rng");
static_assert(offsetof(GqSave, earned_total) == 28, "earned_total");
static_assert(offsetof(GqSave, spent_total) == 32, "spent_total");
static_assert(offsetof(GqSave, day) == 36, "day");
static_assert(offsetof(GqSave, minute) == 38, "minute");
static_assert(offsetof(GqSave, coins) == 40, "coins");
static_assert(offsetof(GqSave, stars) == 42, "stars");
static_assert(offsetof(GqSave, projects) == 44, "projects");
static_assert(offsetof(GqSave, fund) == 46, "fund");
static_assert(offsetof(GqSave, stat) == 48, "stat");
static_assert(offsetof(GqSave, weather) == 72, "weather");
static_assert(offsetof(GqSave, weather_hours) == 73, "weather_hours");
static_assert(offsetof(GqSave, festival_flags) == 74, "festival_flags");
static_assert(offsetof(GqSave, event_today) == 75, "event_today");
static_assert(offsetof(GqSave, inv) == 76, "inv");
static_assert(offsetof(GqSave, till_left) == 100, "till_left");
static_assert(offsetof(GqSave, errands_today) == 101, "errands_today");
static_assert(offsetof(GqSave, meals_today) == 102, "meals_today");
static_assert(offsetof(GqSave, gifts_today) == 103, "gifts_today");
static_assert(offsetof(GqSave, tool) == 104, "tool");
static_assert(offsetof(GqSave, goal) == 112, "goal");
static_assert(offsetof(GqSave, chapter) == 113, "chapter");
static_assert(offsetof(GqSave, q_active) == 114, "q_active");
static_assert(offsetof(GqSave, q_step) == 115, "q_step");
static_assert(offsetof(GqSave, q_count) == 116, "q_count");
static_assert(offsetof(GqSave, hero_hearts) == 117, "hero_hearts");
static_assert(offsetof(GqSave, hero_area) == 118, "hero_area");
static_assert(offsetof(GqSave, year_legends) == 119, "year_legends");
static_assert(offsetof(GqSave, q_done) == 120, "q_done");
static_assert(offsetof(GqSave, friend_) == 128, "friend_");
static_assert(offsetof(GqSave, seen) == 140, "seen");
static_assert(offsetof(GqSave, ach) == 148, "ach");
static_assert(offsetof(GqSave, chat_today) == 152, "chat_today");
static_assert(offsetof(GqSave, reserved) == 154, "reserved");

typedef struct {
  GqSave   s;             // the saved part
  uint32_t serial;        // bumped by gq_touch() on state changes (tests use it)
  uint8_t  presence;      // GQ_Q_HERO_PRESENCE
  uint8_t  net, clock_approx, card_period, night_quiet;
  uint8_t  sleep_mode;     // GC_OPT_SLEEP_MODE
  uint8_t  live_wx1;       // live weather: GQ_W_* + 1, 0 = none (so that zero-initialised means none)
  uint8_t  live_flags;     // GC_WXF_*
  int16_t  live_temp10;    // tenths of a degree Celsius
  uint8_t  dbg_turns;      // GQ_DBG_TALK_TURNS
  int16_t  year; uint8_t month, mday, wday;
} GqState;
extern GqState G;

typedef struct { uint8_t kind, area, tx, ty, npc, item, count, activity; } GqNeed;
enum { GQ_NEED_NONE, GQ_NEED_TALK, GQ_NEED_ITEM, GQ_NEED_GOTO, GQ_NEED_SLAY, GQ_NEED_DO, GQ_NEED_WAIT };
typedef struct { uint8_t index, is_project, mat_item, mat_need, mat2_item, mat2_need, affordable, name_txt;
                 uint16_t price; } GqGoal;

// Palette inputs for one moment (design 6.2). Tint 256 = 1.0, range 0..320.
typedef struct {
  uint8_t  season;        // 0..3
  uint8_t  wash_k;        // 0..255
  uint16_t wash_rgb565;
  int16_t  tr, tg, tb;
} GqLook;
