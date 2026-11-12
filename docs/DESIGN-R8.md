# DESIGN-R8: generated forests, caves and dungeons ("deep" missions)

Status: design only, nothing implemented. Base: main at R7, quest image 440,288 bytes.
Size gates: check_bin warns from 440 KiB (450,560), CI fails at 470 KiB (481,280), hard limit 500 KiB.
Target for this feature: **+3.5 KB (ceiling +5 KB)**, so the image stays under the 450,560 warning line.

## 0. Summary of the approach

- One new area id, `GQ_AREA_DELVE` (= 8, defined next to the enum as `#define GQ_AREA_DELVE GQ_AREA_COUNT`;
  `GQ_AREA_COUNT` stays 8 = "number of fixed areas", so no existing table, loop or test changes size).
- A level is a seed. From the seed a graph of 20..30 rooms on an 8x8 grid is generated into ~68 bytes of RAM.
  A room's tiles are rebuilt from (seed, room) each time the hero enters, through the existing
  `fill()` / `scatter()` / `reach_ok()` code in world.cpp. Nothing is stored in flash except ~150 bytes of rule tables.
- The hero runs a private activity `ACT_DELVE` (like `ACT_HOME`, reported as `GQ_ACT_QUEST`). It is a thin
  supervisor: inside a room it starts the existing fight / mine / forage phase machines and takes over again
  when they `finish()`. All existing safety nets (phase and activity watchdogs, bed time, faint, `go_home`) stay in force.
- No new art. Monster variety comes from the 4 existing monster sprites plus 3 palette rows (48 bytes).
- Mission state that must survive a power cut is 14 bytes taken from `GqSave.reserved` (record stays 192 bytes,
  version stays 1). `firmware/store.cpp` and every recovery file are untouched.

Facts from the code that shape the design:
- Game time is wall-clock time (calendar.cpp): "1.5-2 minutes per screen" is 1.5-2 real minutes, a 20-30 room
  level is a 35-55 minute expedition.
- A natural pass through a room (walk 13 tiles at 24 ms/px, one fight) takes ~15-25 s, so the hero needs things
  to do in a room to fill ~90 s. Section 3 gives him a per-room "dwell" list.
- The hero has 3..6 hearts and wins a fight with 70..95 %. Unbounded fighting over 25 rooms would always end
  in a faint, so fights per room are capped and chests heal (section 4).
- Coins from finds are already capped twice (10 a day in hero.cpp and in econ.cpp); goods are capped by
  `ITEM_CAP` and the shop till. Missions therefore cannot break the economy if they pay in goods plus the
  existing errand payment.

## 1. Data model and RAM budget

### 1.1 Saved (inside GqSave, no growth)
`reserved[38]` at offset 154 becomes:

| off | type | field | meaning |
|-----|------|-------|---------|
| 154 | u8  | dv_state | 0 = no mission, 1 forest, 2 cave, 3 dungeon |
| 155 | u8  | dv_cur   | current room index (0..n-1) |
| 156 | u32 | dv_seed  | level seed (never 0 while a mission runs) |
| 160 | u32 | dv_seen  | bit r = room r was left (cleared) |
| 164 | u8  | dv_min   | mission minutes so far (0..60) |
| 165 | u8  | dv_moves | room changes so far (0..90) |
| 166 | u8  | dv_day   | low byte of the game day of the last mission start |
| 167 | u8  | dv_cnt   | missions started on dv_day |
| 168 | u8  | reserved[24] | still zeroed |

`gq_save_sanitize`: dv_state > 3 or dv_seed == 0 -> state 0; state 0 zeroes dv_cur/seed/seen/min/moves;
dv_cur > 29 -> 0; dv_min > 60 -> 60; dv_moves > 90 -> 90. (dv_cur >= n is checked after the graph exists:
the mission is dropped.) The static_asserts for the offsets are extended. Old saves hold zeros there = "no mission";
an older firmware zeroes the bytes (mission dropped). No version bump.

### 1.2 RAM only (new .bss)

```
struct DelveGraph {          // delve.cpp, 68 bytes
  uint32_t seed;             // seed the arrays below were generated from (0 = none): lazy regeneration
  uint8_t  n, exit;          // room count 20..30, index of the exit/boss room
  uint8_t  cell[30];         // grid position: x | y << 3   (8x8 grid)
  uint8_t  link[30];         // bits 0-3 doors N,E,S,W; bits 4-5 direction of the next room toward the exit
};
struct DelveRoom {           // hero.cpp, 8 bytes: per-room counters, reset on every room entry
  uint16_t ms_acc;           // minute accumulator for dv_min
  int32_t  room_ms;          // time in this room (could be uint16 in 100 ms units)
  uint8_t  fights, nodes;    // attempts in this room
};
```
Total new static RAM: **76 bytes**. Transient stack: generator 30 (depth) + 8 (occupancy bits) + 30 (BFS queue)
= ~70 bytes, room BFS for "nearest unvisited" 60 bytes. `reach_ok` and `find_path` reuse their existing static arrays.
Actor gets no new field: a monster's palette row is kept in `Actor.bob` (unused for monsters).

## 2. Generator (integer only, deterministic, bounded)

All randomness below comes from a local xorshift32 seeded with dv_seed, or from a stateless hash
`dv_hash(seed + room * 0x9E3779B1u + salt)` (two multiplies, three shifts). The global `gc_rnd()` is never used
for the graph or for tiles, so a level is a pure function of (seed, biome) and a room of (seed, biome, room, season,
dv_seen bit).

### 2.1 Room graph: `delve_gen(seed)`
1. `n = 20 + rnd % 11`. Room 0 at cell (3 or 4, 3 or 4). `depth[0] = 0`. Occupancy = 64-bit map on the stack.
2. Grow a tree, exactly n-1 additions: parent candidate `p = (rnd % 4) ? n-1 : rnd % n` (3 in 4 the newest room:
   long winding corridors with side branches). Scan q = p, p-1, ... (wrapping, at most n rooms); for q try the
   4 directions from a random start; the first free in-grid cell becomes room n with doors both ways and
   `depth[n] = depth[q] + 1`. This always succeeds: a connected set of fewer than 64 cells in an 8x8 grid has a
   free in-grid neighbour. Work: <= 30 x 4 probes per addition.
3. `exit` = room with the largest tree depth (lowest index on a tie). It is a tree leaf by construction.
4. Loops: `n / 6` extra doors; for each, up to 3 tries of (random room, random direction); a door is added when the
   neighbour cell holds a room, there is no door yet, and neither room is `exit` (the exit keeps exactly one door,
   so it is never on the way to anything else).
5. BFS from `exit` over the doors (<= 30 nodes): store in `link` bits 4-5 the direction of the next room toward
   the exit.

Connectivity proof: every room except 0 is created with a door to an existing room (spanning tree); loops only add
doors. Hence every room is reachable from room 0 and step 5 labels every room. Work bound for the whole graph:
< 4,000 simple operations, once per mission (and once after a restart).

Derived, not stored: room kind. `exit` -> exit/boss room; a room with one door (dead end) or `hash % 5 == 0` ->
chest room (the exit room always has one); `hash % 8 == 0` -> den (more monsters); room 0 -> entry room (no monsters).

Helpers: `delve_nb(r, d)` (neighbour in direction d, linear scan of `cell`, <= 30 compares),
`delve_next(cur, exit_mode)` (section 3.3).

### 2.2 Room tiles: `delve_build_room()` in world.cpp
Called from `world_build_area(GQ_AREA_DELVE)`. Uses a 12-byte rule row per biome (flash):

| | forest "DEEP FOREST" | cave "DEEP CAVES" | dungeon "DEEP RUINS" |
|---|---|---|---|
| floor (ground) | T_GRASS (tufts/flowers by the existing season hash, room index mixed in) | WT_CAVE_FLOOR | WT_RUIN_FLOOR |
| border | T_TREE objects | WT_CAVE_WALL ground, row 0 WT_CAVE_WALL_TOP, top 2 tiles thick | WT_RUIN_WALL ground |
| door | gap, T_PATH ground + 2-tile path stub | floor gap | WT_RUIN_DOOR object on the wall (as MAP_RUINS) |
| feature tile | T_WATER ground (pond) or T_TREE clump | WT_CAVE_WALL ground | WT_RUIN_PILLAR object / WT_RUIN_WALL |
| scatter (blocking) | T_TREE 12-18, T_ROCK 2, WT_BUSH 2 (spring/summer) | WT_STALAG 5, T_ROCK 2, WT_ORE_ROCK 3, WT_GEM_ROCK 0-1 | T_ROCK 3, WT_BARREL 2 |
| scatter (walkable) | WT_MUSHROOM 2 (spring/autumn), WT_HERB 2 (not winter) | - | - |
| wall decor | - | WT_TORCH on the top wall (2) | WT_TORCH on the top wall (2) |
| exit marker | path gap in the trees | WT_CAVE_EXIT | WT_STAIRS, plus WT_ALTAR in the middle |

Steps (all through the existing `fill()` and `scatter()`; scatter gets the seed as a parameter instead of deriving
it from `s_area`):
1. Fill floor, border.
2. Doors: for each door bit, position on that edge = `4 + hash(seed, edge id) % 7` (4..10); the edge id is
   canonical (lower room index + direction) so both rooms agree. Register the border tile in `s_exit[]` and the
   tile just inside as spot `SP_TARGET_A + d` (d = N,E,S,W). `SP_ENTRY` = inside tile of the door the hero came
   through (room 0 / restore: (7,12)).
3. Feature: one of 6 shapes picked by hash (none, 3x3 centre block, two 2x2 blocks, four pillars, a bar with a
   gap, an L). Shapes are 2-byte rects (xy, wh) in flash (~40 bytes) and lie inside x 4..10, y 5..9 only.
4. Exit room: marker on the wall opposite its single door, `SP_DOOR` = tile in front of it. Dungeon: altar.
5. Chest room and not yet seen: up to 8 hashed positions in x 3..11, y 3..10; the first where the tile and the
   tile below are free floor and `reach_ok()` still holds gets WT_CHEST, `SP_SITE` = tile below. In a seen room
   the chest is drawn as WT_CHEST_OPEN.
6. Scatter. Resource nodes (ore, gem, mushroom, herb, bush) only in a room not yet seen.
   Total objects per room <= 30.

Guaranteed solvable:
- Before scatter, the ring of tiles 1..3 inside the border is free floor (features stay in x 4..10, y 5..9), and
  every door's inside tile lies on that ring, so all doors, the chest spot and the exit spot are connected.
- `scatter()` already refuses a blocking object unless every spot and every exit tile stays reachable from
  `SP_ENTRY` (`reach_ok()`), and never places on a spot/exit tile (`is_special`) or on T_PATH.
So every door, the chest and the exit are reachable from the entry in every room; the host test proves it by BFS.

Work bound per screen: scatter tries <= 6 x count and runs one 225-tile BFS per blocking try:
**<= 180 BFS + 8 (chest) per room**. The existing forest (34 scattered objects, up to 204 BFS) is the same order,
so a generated room costs no more than entering the forest costs today, and it happens at the same moment: in one
tick while the curtain is closed (black screen). No per-frame work is added. Plainly: this is "no worse than
today", not "zero hitch"; spreading the build over several frames is possible but costs ~150 bytes and I do not
recommend it. A host-only counter (`#if !defined(ARDUINO_ARCH_ESP8266)`) lets the test assert the bound.

Other world.cpp points:
- `world_build_area` and `world_change_area` accept area 8. `world_area_open(8)` = 0 (nothing travels there by
  `tv_start`; the mission code changes rooms itself).
- `world_obj_change` in the delve area writes the object directly (no `s_dyn` slot): otherwise a mined rock's
  timer would reappear at the same tile of the next room.
- `rebuild_current()` (new day, festival) rebuilds the same room from the same inputs; `fix_actors` already
  handles the rest.
- weather.cpp: outdoors also when `area == GQ_AREA_DELVE && dv_state == 1` (rain in the deep forest; none underground).

## 3. Hero behaviour

### 3.1 Life of a mission
1. Start (section 5): `dv_seed = gc_rnd() | 1`, biome chosen among the open ones, `dv_cur = 0`, counters 0,
   `dv_day/dv_cnt` updated, `gq_touch()`. Hero says "INTO THE DEEP|CAVES!".
2. `ACT_DELVE` phase 0 outside the delve area: `tv_start(entrance area, SP_TARGET_D)` - forest: FOREST,
   cave: CAVE, dungeon: RUINS (all existing spots, so the mission is gated by the existing
   `world_area_open`: cave needs Pick I, dungeon the lantern). On arrival: `world_change_area(GQ_AREA_DELVE, SP_ENTRY, 1)`.
   The same phase is the restore path (section 6).
3. In a room: section 3.2. Leaving through a door: set the room's dv_seen bit, `dv_cur = neighbour`, `dv_moves++`,
   `gq_touch()`, `world_change_area(GQ_AREA_DELVE, SP_TARGET_A + opposite(d), 1)`.
4. Exit room done: curtain to the village (`SP_TARGET_B` for forest/dungeon, `SP_TARGET_C` for the cave),
   `dv_state = 0`, then the existing `errand_done()` (given the place name): "BACK FROM THE|CAVES! +9 COINS".

### 3.2 In a room (decide step, run each time control returns to ACT_DELVE)
First visit, while `room_ms < DWELL` (70 s + hash % 30 s) - first match wins:
1. Live monster, hearts >= 2, `fights < 1` (den: 2; boss room: boss first): `begin(h, GQ_ACT_FIGHT)` at phase 2
   (the existing fight machine: approach, 3 swings, win or lose a heart, loot).
2. Closed chest: walk to `SP_SITE`, face it, 1.2 s, chest -> WT_CHEST_OPEN, loot (section 4), cheer.
3. Node in the room, `nodes < 2`: cave -> `GQ_ACT_MINE` at phase 2, forest -> `GQ_ACT_FORAGE` at phase 2
   (existing machines, counter preset so they take one node).
4. Otherwise look around: `find_path` to a random free tile within 4 tiles (as GQ_ACT_WANDER does), pause 1.5-4 s,
   sometimes a "!" emote.
Then, or at once in a room already seen (pass-through, ~10 s): walk to the door chosen by 3.3; in the exit room
walk to `SP_DOOR` instead when 3.3 says "leave".

Sub-activities return through `finish()`: the `ACT_NONE` branch of `hero_step` starts `ACT_DELVE` again instead of
`pick_activity()` while `dv_state != 0`. Each sub-activity keeps its own 120 s phase / 240 s activity watchdog.

### 3.3 Which door (exit bias)
`delve_next(cur, exit_mode)`:
- exit_mode = `dv_min >= 48` or every room other than the exit is seen. Then: the stored direction toward the exit.
- else: BFS over the doors from `cur` (<= 30 rooms, not through the exit room); the first unseen room found gives
  the first step. Ties resolve in N,E,S,W order.

Why he cannot wander forever:
- Each room change either follows a shortest path to the nearest unseen room (<= 29 steps, then one more room is
  seen; at most 30 times) or follows the exit labels (<= 29 steps). 
- `room_ms > 150 s`: he goes to the door whatever he was doing (below the 240 s activity watchdog).
- `dv_min >= 48`: exit mode, no more dwelling (worst walk out: 29 rooms x ~12 s = 6 min).
- **Hard abort** (checked in the decide step and once a minute): `dv_min >= 60`, `dv_moves >= 90`, `sleep_due()`,
  a festival starting, a storm in the forest biome, or `dv_cur >= n`. Abort = hero says "ENOUGH FOR|TODAY. HOME!",
  then `go_home()`.
- `go_home()` (also called by the watchdogs, bed time and the faint path) clears `dv_state`; `ACT_HOME` phase 0
  in the delve area does `world_change_area(GQ_AREA_VILLAGE, SP_ENTRY, 1)`; its existing 60 s watchdog still ends
  in `world_instant_area`. So every existing "drop everything" path also ends a mission, with no new exit route.

Expected length: n x ~95 s (85 s dwell + door walk + curtain) + backtracking = 36-55 minutes, 1.6 min per new screen.

## 4. Monsters, fights, loot

### 4.1 Three types per biome from existing art
The monster set is slime, bat, crab, ghost (2 frames each, chars 40..47) and the boss (60..62). No biome has three
native types, so each biome uses three of the four sprites, the non-native ones through a palette row
(same mechanism as villager clothes: `NPC_REMAP` gets rows 9..11, `world_render` accepts `remap < 12`,
`actor_ops` passes `a->bob` for monsters):

| biome | type 1 | type 2 | type 3 | end room |
|---|---|---|---|---|
| forest | slime (native green) | bat (native) | "beetle": crab, red 12 -> brown 6 (row 9) | pack of 3 at the way out |
| cave | bat (native) | "ooze": slime, greens 3/4/2 -> grey 14 / white 13 / dark 8 (row 10) | "rock crab": crab, red 12 -> grey 14 (row 10) | pack of 3 at WT_CAVE_EXIT |
| dungeon | ghost (native) | "ember slime": slime, greens -> red 12 / pink 11 / brown 5 (row 11) | "night bat": bat, grey 14 -> dark blue 8, eyes -> yellow 15 (row 11) | boss (existing Hollow King sprite) + 2 ghosts, altar, stairs |

Cost: 3 x 16 bytes of palette rows. **New art cost: 0.** If the owner wants a really new monster later: two 16x16
frames cost about 250-320 bytes each pair (32 bytes of row indices per cell plus up to 16 new 8-byte rows); the
chars sheet has exactly one spare cell (63), so a new two-frame monster also needs a ninth sheet row and a
png2tiles/paint_art change. Not recommended for R8.

Monster identity stays the base id (`GQ_MON_SLIME` etc.), so kills count for statistics, the "fought" remarks and
"slay N" quests exactly as today. One exception: the boss killed in the delve area is reported in `GQ_EV_SLAIN`
as `GQ_MON_GHOST`, so it can never advance the story step that waits for the real boss.

### 4.2 Spawn
`npc_populate()` for the delve area spawns only monsters (no villagers, cat or critters), with the existing
`free_tile()` (walkable, >= 3 tiles from the hero, not on another actor) and the existing speeds
(bat 45, slime/crab 70, ghost 100, boss 110 ms/px):
room already seen or room 0: none; normal: 2 + hash % 2; den: 4; exit room: 3 (dungeon: boss + 2).
Types: hash picks among the biome's three. At most 4 monsters + hero <= 8 actor slots.

### 4.3 Fight and loot rules (existing mechanics)
- Fight: unchanged `fight_step` (win chance 55 + 12 x sword + 5 x hearts, boss -30; a loss costs one heart).
  Not started with fewer than 2 hearts, so the hero never faints in a mission: when hurt he walks past monsters.
- Kill: unchanged (1-3 coins under the 10-a-day find cap, slime gel 40 %, `GQ_EV_SLAIN`).
- Chest: 2 goods from a 4-entry biome table through `econ_add_item` (caps apply) - forest: wood, wood, herb, gel;
  cave: stone, stone, ore, ore; dungeon: stone, ore, gel, ore - plus 1-3 coins as `GQ_SRC_FIND` (same daily cap),
  plus 1 heart (up to the maximum). The chest of the exit room adds 1 gem.
- Every 5th new room: +1 heart ("catches his breath", heart emote).
- Mining and foraging in rooms: unchanged machines and events (`GQ_EV_MINED`, `GQ_EV_FORAGED`).
- Completion: `errand_done()` = `GQ_EV_QUEST_DONE 0xFF` = the existing errand payment (6-14 coins, counts as one
  of the 3 paid errands of the day). No new economy code.
- Abort or timeout: he keeps what he gathered, no completion payment.

Tuning constants (one block in hero.cpp): DWELL 70-100 s, fights per room 1/2, SOFT 48 min, HARD 60 min,
missions per day 1.

## 5. Trigger and what the player sees

Trigger: in `pick_activity()` after the breakfast rule:
`delve_due(m)` = no mission today (`dv_day/dv_cnt`), game day >= 2, 09:30 <= m < 18:30, hearts >= 2, no storm,
`G.sleep_mode != 2`, and a 1-in-6 roll (so the start time varies). Biome = random among forest (always),
cave (`world_area_open(GQ_AREA_CAVE)`), dungeon (`world_area_open(GQ_AREA_RUINS)`): the player gets the forest
from day 2, caves with Pick I, dungeons with the lantern. No change to quest.cpp: the mission is an activity that
is paid like an errand. Debug/test/preview: new `GQ_DBG_DELVE` (arg = biome 1..3 | seed << 2) starts one now.

On screen:
- Bottom band (status page): "TO THE DEEP CAVES" while walking to the entrance, then "DEEP CAVES 12/24"
  (rooms seen + 1 / rooms), "GOING HOME" on the way back. Place names reuse the existing `PLACE_NAME` strings.
- HUD unchanged: hearts and coins move as he fights and loots; item emotes above his head as today.
- Curtain between rooms (the existing area transition), dialogue box for the three lines (start, abort, return).
- `GQ_Q_HERO_AREA` returns 8 during a mission; `GQ_Q_HERO_ACT` returns `GQ_ACT_QUEST` (or FIGHT/MINE/FORAGE during those).
- Hourly clock cards etc. behave as in any other area.

## 6. Save and restore

It fits. The 14 saved bytes are seed, biome, room, seen-mask, minutes and moves; `gc_save_export` already copies
`G.s`. store.cpp is unchanged: it saves when the game serial changed, at most every 20 minutes (and on a new day).
After a power cut the game resumes from the last record, i.e. up to 20 minutes back: inventory, hearts, room and
seen-mask are all from that same moment, so nothing is duplicated or lost inconsistently.

Restore needs no special code path: `gq_save_sanitize` puts the hero in the village as today; `dv_state != 0`
makes the scheduler start `ACT_DELVE`, whose phase 0 walks to the entrance and enters room `dv_cur`; the graph
is regenerated lazily because `DelveGraph.seed != dv_seed`. The player sees him walk back to the cave and carry on
at "DEEP CAVES 9/24". `dv_min` is saved, so the 60-minute hard limit holds across restarts. A tighter resume point
than 20 minutes would need a change in store.cpp (recovery layer): not proposed.

Safety on bad data: every field is clamped; `dv_cur >= n` or a failed room entry drops the mission; and the
existing rule "two crashes in a row -> the saved game is not loaded" (store.cpp) covers anything unforeseen.

## 7. Flash cost and files

| part | file | bytes (estimate) |
|---|---|---|
| graph generator, neighbour/next-door helpers, hash | delve.cpp (new), delve.h (new, private) | 550 |
| room builder + hooks (area 8, obj_change guard, remap range, deco hash) | world.cpp, map_data.h untouched | 800 |
| rule tables: 3 biomes x 12, 6 shapes ~40, 3 palette rows 48, loot 12 | delve.cpp / world.cpp | 140 |
| monsters for delve rooms, remap in actor_ops | npc.cpp, world.cpp | 150 |
| ACT_DELVE machine, start/stop/abort, door choice, chest, look-around | hero.cpp | 1,150 |
| hooks: pick_activity, ACT_NONE, go_home, ACT_HOME, status text, errand_done(place), boss alias | hero.cpp | 250 |
| strings (3 lines, "DEEP ", "TO THE DEEP ") | hero.cpp | 80 |
| sanitize clamps, GQ_DBG_DELVE, weather line | save.cpp, game.cpp, weather.cpp | 150 |
| **total** | | **~3.3 KB, expect 3.0-4.5 KB** |

Other files: gq_types.h (GqSave fields, asserts, `GQ_AREA_DELVE`, `GQ_DBG_DELVE`), world_priv.h and gq_mod.h
(declarations), test/test_delve.cpp (new), test/test_save.cpp (reserved bytes), docs, RELEASE-NOTES.md.
Unchanged: everything under firmware/, lib/gqsafe, quest.cpp, econ.cpp, talk.cpp, ui.cpp, the art and assets_gen.h.
Acceptance for size: quest .bin <= 445,300 bytes (+5 KB); if the estimate is exceeded, cut in this order:
door offsets (fixed mid-edge doors), room shapes 6 -> 3, the heart-every-5th-room rule.

## 8. Test plan (host, ASan/UBSan, new test/test_delve.cpp unless noted)

1. Graph, 10,000 seeds (x 3 biomes share one graph): n in 20..30; all cells distinct and inside 8x8; doors
   symmetric (door N of a <=> door S of its north neighbour) and never to an empty cell; BFS from room 0 reaches all
   n rooms; exit != 0, exit has exactly one door; following the stored exit directions from every room reaches the
   exit in < n steps; the exit room is a chest room in every level, and the average is >= 4 chest rooms.
2. Determinism: `delve_gen` twice gives identical bytes; a room built twice (and after building other rooms and
   other areas in between) gives identical `g_ground`/`g_obj`; the global RNG state (`gc_rng_get`) is unchanged
   by graph generation and by the tile build; a golden hash of 3 fixed (seed, biome, room) builds catches
   accidental generator changes (levels of a saved mission must not change silently).
3. Room solvability: for 10,000 seeds x 3 rooms each (room 0, the exit, one hashed room) and for every room of 300
   seeds, in all 3 biomes and 4 seasons spread over the seeds: BFS from `SP_ENTRY` reaches every door tile, every
   `SP_TARGET_A..D` that exists, `SP_SITE` and `SP_DOOR`; both sides of every door agree on the position
   (build room a and its neighbour b, compare offsets); each entry spot is walkable.
4. Work bound: the host-only BFS counter after each build in test 3 is <= 188; object count <= 30.
5. Actors: enter 2,000 rooms through `GQ_DBG_DELVE` + direct room set: every actor is on a walkable tile, no two
   on the same tile, none on the hero's tile, count <= 5; after 200 ticks the same holds (monsters wander).
6. Missions end: 60 missions (20 seeds x 3 biomes) in the simulator: each ends in the village within 62
   simulated minutes; `GQ_Q_HERO_PRESENCE` is never 0; `dv_moves <= 90`; no faint (`GQ_STAT_FAINTS` unchanged);
   at least 80 % of them reach the exit; rooms seen >= 15 on average; per new room 80..130 s on average.
7. Abort paths: force bed time (sleep mode 2), a festival, a storm, `dv_min = 59`, and a forced activity during a
   mission: the hero is back in the village within 2 minutes and `dv_state == 0` (forced activity: mission resumes).
8. Save (test_save.cpp): export in room k, import into a fresh core: same `dv_*`; after the walk to the entrance
   the room tiles equal the tiles before the export; sanitize of random bytes (existing fuzz loop) leaves a state
   in which a mission either runs to its end or is dropped; a record from R7 (zeros) means no mission.
9. Economy (test_econ.cpp, existing limits must still pass): `max_day_income <= 132`, all goals day 50..90.
   New check: a mission day earns at most 14 (errand) + 10 (finds) coins directly.
10. Soak (test_soak.cpp as is) and test_world.cpp `test_maps` (unchanged because `GQ_AREA_COUNT` stays 8).
Run time: tests 3-5 are ~40,000 room builds, the same order as today's 4,096 village builds x seasons.

## 9. Implementation chunks (in order)

**C1 - graph and save layout** (delve.cpp/.h, gq_types.h, save.cpp, test_delve.cpp part 1-2, test_save.cpp).
`delve_gen`, `delve_nb`, `delve_next`, `dv_hash`; GqSave fields + sanitize; nothing calls it yet (the linker drops
it: image size unchanged). Accept: tests 1, 2 (graph part), 8 (sanitize part) pass; all existing tests pass;
quest .bin within +100 bytes.

**C2 - rooms and monsters** (world.cpp, npc.cpp, weather.cpp, world_priv.h, game.cpp for `GQ_DBG_DELVE`
which for now only teleports the hero into room 0 of a given seed/biome). Area 8, `delve_build_room`, biome and
shape tables, scatter seed parameter, obj_change guard, palette rows, monster spawn. Accept: tests 2-5 pass;
preview screenshots of 4 rooms per biome looked at (doors line up, walls closed, recolours readable against the
floor); +1.2 KB or less.

**C3 - the mission** (hero.cpp, game.cpp). ACT_DELVE, hooks, loot, status text, lines, trigger, completion and
abort. Accept: tests 6, 7, 9, 10 pass; a preview run of one full mission per biome watched at speed; +1.7 KB or less.

**C4 - restore, tuning, release** (hero.cpp small, tests, docs/DESIGN-R8.md, RELEASE-NOTES.md). Lazy graph
regeneration on restore, test 8 complete, tuning of dwell/fights/hearts against test 6, final size check
(<= 445,300), 3-day soak with missions. Accept: full CI green, size line in the report.

C1 and the art-free part of C2 (tables) can start in parallel; C3 needs C2; C4 needs C3.

## 10. Risks and recovery layer

Recovery layer: **nothing touched, nothing unavoidable.** No change to firmware/safety.cpp, config.cpp, prefs,
main.cpp, store.cpp or the slot log; the record stays 192 bytes; the image stays far below store's 512,000-byte
limit; `tools/recovery_guard.py` contract files are not modified, so no `GQ_RECOVERY_REV` bump.

Risks:
1. Economy and pacing: a mission replaces ~50 minutes of normal activity and brings ~12 chest goods, 1 gem and one
   errand payment. The existing test limits (`max_day_income <= 132`, goal pacing) decide; if they move, lower
   chest goods to 1 or drop the gem. "MONSTER BANE" (25 kills) will come after one or two missions.
2. The scheduler change shifts the global RNG sequence: any test with fixed expectations on a seed may need its
   expected value re-checked (not weakened).
3. One-tick room build under the curtain equals today's forest build; if the device shows a visible hitch there
   today, it will show it once per room. Mitigation if needed: lower scatter counts (linear effect).
4. Less village life: with one mission a day the hero is away from the villagers for up to an hour. That is the
   owner's request, but the count per day is one constant.
5. Monsters spawn by the global RNG (as everywhere today), so only tiles are a pure function of the seed. A monster
   in a pocket the hero cannot reach just costs one failed fight attempt (attempts are counted, not wins).
6. `GQ_Q_HERO_AREA` can now return 8: the preview page and the status page must not index an 8-entry name table
   with it (hero.cpp already masks with `& 7`; check preview JS in C2).
7. Readability of recolours on the 240x240 panel (grey ooze on grey cave floor): check on screenshots in C2 and
   pick other palette indices if needed; cost-free.

Requirements that are met only in a reduced form (said plainly):
- "30 fps loop never stalls": bounded and hidden by the curtain, same as existing areas, not spread over frames.
- "Power cut": resumes at the room of the last flash record (up to 20 minutes old), the level itself is identical.
- "Boss room": only the dungeon has the boss sprite; forest and cave end with a guarded way out (no new art).

Open questions for the owner:
1. Missions per day: 1 (default) or 2 (morning and afternoon)?
2. Is "DEEP FOREST / DEEP CAVES / DEEP RUINS" acceptable naming (it reuses strings already in flash)?
3. Should a mission be shown in the quest band/card as well (costs ~150 bytes in quest.cpp/ui.cpp), or is the
   bottom band enough?
