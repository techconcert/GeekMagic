# GeekQuest design: "Elderwick" (village life, quests, and a clock that never needs a cable)

Status: DESIGN ONLY. Nothing in this document is implemented yet. No code was changed to write it.
Audience: the user (owner of the clock) and the engineers (coding models) who implement the chunks in section 11.

Words used in a fixed sense:

- **Recovery layer**: `firmware/safety.cpp`, the update page, the rescue hotspot, the crash counter, and `tools/check_bin.py`. Everything that keeps the clock reflashable over WiFi.
- **Golden window**: the first 10 minutes after every boot, in which only the simplest network code runs and the rescue hotspot is always on (section 8.4).
- **Core**: `lib/gamecore/`. No heap, no libc, no floating point; builds for ESP8266, host (ASan/UBSan) and WebAssembly.
- **NEEDS USER APPROVAL**: a decision the user must explicitly accept before any engineer implements or enables it. The full list is in section 12.2.

## 0. Decisions at a glance

| Topic | Decision |
|---|---|
| Economy | Wallet has a cap (99, then 250, 500, 999 with purse upgrades). Income is bounded per day (shop till 60 coins/day, at most 3 paid errands/day). 22 purchases (12 tools, 10 village projects) that change the picture on screen. Overflow turns into stars. The HUD never shows more than 3 digits; lifetime totals use `1.2K` formatting. |
| Hero off screen | The camera follows the hero. Quests are played out visibly in 4 outdoor areas; shop, tavern and home are 3 visible interiors. A bottom band always shows the current goal with progress. Chunk 0 ships a stopgap (quest postcard and door markers) before the areas exist. |
| WiFi | Own reconnect state machine with backoff (3 s up to 5 min), radio cycle every 30 min, planned restart after 6 h/12 h/24 h. SDK auto-reconnect is switched off because it starves the rescue hotspot. Nothing blocks the render loop. |
| Persistence | Stage P0: RTC memory only (no flash writes, survives restarts, crashes and updates; lost on power cut). Stage P1: one 4 KB flash sector we believe nobody uses (0x3FA000), written at most about once a day, and only after the user presses "Enable saving" on the status page. **P1 NEEDS USER APPROVAL.** Stock's files and stock's settings sector are never written. |
| Recovery | Golden window, slow-crash detector, saved-game quarantine, heap watchdog, update page that needs no heap, CRC check of uploads, refusal of images that have no update page, CI gates on symbols and RAM, backup/restore of the game through the browser. |
| Calendar | A game year is 28 days (4 seasons of 7 days). One game day is one real day. Seasons are palette swaps; weather is 7 states with cheap particles. |
| Work split | 8 chunks. Chunk 0 first (two packages that can run side by side), then chunks 1 to 7 in parallel, then an integration pass. |

## 1. Baseline: what exists today (read from the code)

Facts the design relies on. Engineers: re-check these in the code before you start.

1. `firmware/main.cpp`: `setup()` calls `safety::begin()` first, then the display, then `gc_init`. `loop()` calls `safety::loop()`, ticks the core, renders dirty strips (one static 7.5 KB strip buffer), then `delay()` to cap at about 30 fps.
2. `firmware/safety.cpp`: RTC user memory blocks 96..100 hold `{magic, crashes, planned, forceSafe, stage}`. Safe mode after 3 unplanned resets, no display after 6. The counter is cleared after 60 s of uptime. AP is on for the first 10 min, and again 60 s after the station link is lost. `WiFi.persistent(false)` everywhere except the WiFi form. `WiFi.setAutoReconnect(true)` is the only reconnect logic. Settings `s_depth`, `s_tz`, brightness are RAM only.
3. `lib/gamecore/src/village.cpp` (918 lines): one map, 4 actors (hero + Mira, Bram, Pip, drawn from the hero sheet through palette remaps), 9 activities, BFS path finding, dirty tracking by sprite signature, a dialogue box on strips 12..14, HUD on strip 0.
4. Art: 32 tile cells, 9 hero cells, 16 item cells, each 16x16 at 4 bits per pixel = 128 bytes. 57 cells = 7.3 KB. Font 64 glyphs, 5x7, upper case only.
5. `tools/check_bin.py`: header bytes, 500 KB budget, both OTA directions fit, 5 required strings, `_FS_start == 0x40300000`, name starts with `SDP`.
6. CI: asset freshness, one host smoke test (ASan/UBSan), wasm preview, firmware build for `test` and `quest`, publish to the `builds` branch (older `.bin` files are moved to `firmware/obsolete/`, which is what makes rollback possible).

### 1.1 Root causes of the three reported bugs

| Report | Root cause in the code | Fixed by |
|---|---|---|
| 0 wood, 2831 coins after a day | `ACT_SHOP` sells everything (`g_gold += g_wood + g_fish*3; g_wood = g_fish = 0`), `ACT_QUEST` pays 5..20 with no limit, `ACT_TAVERN` costs 1. A real day has roughly 600 activity slots, so income is about 2800/day and wood is always sold down to 0. No cap, no formatting (`itoa_into` would overflow the HUD area into the clock at 5 digits). | Chunk 0 (stopgap economy), chunk 4 (full economy) |
| Hero disappears | `ACT_QUEST` phase 3 sets `visible = 0` for 40..90 s. `ACT_SHOP` (3 s), `ACT_TAVERN` (15..30 s) and `ACT_SLEEP` (all night) hide the hero at a door. Only sleep shows a marker. | Chunk 0 (postcard, door markers), chunk 3 (areas and interiors) |
| WiFi never came back | The only mechanism is the SDK's auto-reconnect, which is known to stop retrying after some disconnect reasons, and which scans continuously while disconnected (this also makes the soft AP unreliable, because AP and station share one radio and one channel). There is no detection of a link that is "connected" but dead, no escalation, and no diagnostics, so we cannot even tell which case happened. `handleWifiSave` is the only place that passes credentials; after `WiFi.disconnect()` the in-RAM configuration can be empty, so a plain `WiFi.begin()` may have nothing to connect to. | Chunk 0 package B (state machine), chunk 1 (escalation, diagnostics) |

Other problems found while reading (all fixed in chunk 1 unless noted):

- A crash that happens later than 60 s after boot never reaches safe mode, because the counter is cleared at 60 s (section 8.3, "slow crash loop").
- The update page is sent through two `String` copies (about 8 KB of heap). With a fragmented or low heap the page that matters most is the one that fails (section 8.5).
- A firmware bug in the network code that does not crash leaves no way in. This is the gravest gap; the golden window is the answer (section 8.4).
- `millis()` wraps after 49.7 days. `now > AP_GRACE_MS` and `millis() > STABLE_AFTER_MS` are absolute comparisons. They happen to be harmless today, but all new code must use `now - since >= period`.
- Any valid 4 MB ESP8266 image is accepted, including one without an update page.

## 2. Vision

**Elderwick** is a tiny village on a desk. Its one adventurer gets up with the real sun, eats, does chores, runs errands for neighbours, saves up for a better axe, helps build a well, and once every few days walks out of the east gate with a sword to find another shard of the village's silent bell. The clock owner does nothing. They glance over and see something slightly different every time: it is raining, the market stall is new, it is the Harvest Feast, the hero is in the cave with two hearts left.

Design pillars:

1. **Always something to look at.** The hero, or a clear stand-in for the hero, is on screen at all times, and something moves at least every 2 seconds.
2. **Real time, small numbers.** One game day is one real day. Numbers on the HUD stay small and meaningful; progress shows as things in the picture (a bridge, lanterns, a new hat), not as a growing counter.
3. **Weeks of freshness.** A full story takes 4 weeks at the earliest, all purchases take about 10 weeks, the season changes every week, and after that the village keeps going with yearly legends and stars.
4. **Recovery beats features.** No feature may weaken the update path. When a feature and recoverability conflict, the feature loses.

### 2.1 A day in the life (hero schedule)

Times are local wall-clock time when NTP is known, otherwise the game's own clock. The scheduler (chunk 3) picks activities by weight inside each block; weights are shifted by weather, season, the economy (`econ_activity_bias`) and the active quest (`quest_need`).

| Block | Time | Typical activities |
|---|---|---|
| Wake | 06:00 | Home interior: gets out of bed, breakfast, steps outside, stretches |
| Chores | 06:30 to 10:00 | Chop wood, forage, water the garden, feed the cat |
| Errands | 10:00 to 12:00 | Notice board, deliveries, sell at the shop, talk to quest givers |
| Lunch | 12:00 to 13:00 | Tavern interior, chat with Odda |
| Adventure | 13:00 to 16:30 | Story or side quest in forest, shore, cave or ruins; otherwise fish or mine |
| Village time | 16:30 to 18:30 | Shop (sell, buy the goal), build at a project site, gifts, chats |
| Evening | 18:30 to 21:30 | Tavern supper, sit by the fire, stargaze, festival events |
| Night | 22:00 to 06:00 | Sleeps in the home interior; the camera alternates between the bedroom and the night village (fireflies, the cat, lit windows) every 3 to 5 min |

### 2.2 Content plan (target numbers)

| Kind | Today | Target | Notes |
|---|---|---|---|
| Named NPCs | 3 | 8 + cat | Mira (shop), Bram (elder), Pip (kid), Odda (tavern), Finn (fisher), Tess (smith and builder), Luma (herbalist, forest), Wren (travelling merchant, 2 days a week), Miso (cat) |
| Critters | 0 | 5 | chicken, frog, butterfly, firefly, bird |
| Monsters | 0 | 5 | slime, bat, crab, ghost, boss "Mossback" |
| Areas | 1 | 8 | village, forest, shore, cave, ruins; interiors: home, shop, tavern |
| Hero activities | 9 | 16 | wander, chop, fish, forage, mine, garden, sell, buy/build, eat, sit, chat, gift, pursue quest step, fight, play with cat, sleep |
| Quests | 0 | 28 definitions, about 76 steps | 6 story chapters (28 steps), 14 side quests, 8 errand templates |
| Seasons | 0 | 4 x 7 days | 28-day year |
| Weather states | 0 | 7 | clear, cloudy, rain, storm, fog, snow, windy |
| Festivals | 0 | 4 | day 7 of each season |
| Random daily events | 0 | 12 | section 5.4 |
| Item types | 2 | 20 (13 goods, 7 key items), 24 slots | section 3.3 |
| Collectibles | 0 | 32 | 12 fish, 8 forage, 6 treasures, 6 key items |
| Achievements | 0 | 24 | section 3.8 |
| Upgrades / projects | 0 | 12 / 10 | section 3.5 |
| Tile cells | 32 | 96 | sheet 16 x 6 |
| Character cells | 9 | 64 | sheet 8 x 8 |
| Item and icon cells | 16 | 48 | sheet 8 x 6 |
| Text | about 0.7 KB | at most 12 KB | about 400 strings, 30 characters on average |

### 2.3 Budgets

Hard limits: `.bin` at most 500 KB (512,000 bytes, CI gate). Current image is about 373 KB, so about 125 KB is free. This design spends at most **107 KB of flash** and **4.3 KB of RAM** (limit given: 110 KB and 8 KB). The remainder is margin and must not be planned away.

Flash, in KB (1 KB = 1024 bytes). "Code" includes small constant tables.

| Chunk | Code | Art | Text | Total |
|---|---|---|---|---|
| 0 Foundations and the three fixes | 9.0 | 0 | 1.0 | 10.0 |
| 1 Recovery, persistence, network hardening | 10.0 | 0 | 3.0 (web pages) | 13.0 |
| 2 Rendering and UI | 10.0 | 0 | 1.0 | 11.0 |
| 3 World and hero life | 17.0 | 2.0 (map scripts) | 0 | 19.0 |
| 4 Economy and progression | 7.5 | 0 | 1.5 (tables) | 9.0 |
| 5 Quests and story | 6.0 | 0 | 13.0 (1 KB tables, 12 KB strings) | 19.0 |
| 6 Calendar, weather, festivals | 6.0 | 0 | 0.5 | 6.5 |
| 7 Art | 0.2 | 19.3 (151 new cells) | 0 | 19.5 |
| **Sum** | **65.7** | **21.3** | **20.0** | **107.0** |

RAM (static data, bytes):

| Chunk | New static RAM | What |
|---|---|---|
| 0 | 500 | `GqState` (192 saved + about 100 volatile), net state machine (about 80), credentials copy (100) |
| 1 | 700 | one 256-byte journal slot buffer, settings (48), event log (64), heap statistics, restart history |
| 2 | 500 | 3 palettes (96), card and band state, dialogue box (moved, +60), curtain |
| 3 | 1500 | actors 4 -> 8 (about +430), dynamic object table (64), monsters, scheduler, area state |
| 4 | 200 | goal cache, counters not in the save |
| 5 | 300 | text scratch buffer (48), quest volatile state |
| 6 | 400 | 24 particles (96), look/tint state, event state |
| 7 | 0 | art lives in flash |
| **Sum** | **4100** | limit 8192 |

Stack rule: no function in the core may put more than 256 bytes on the stack, and nesting from `gc_tick` or `gc_render_strip` must stay under 1 KB (the ESP8266 stack is 4 KB and shared with the network stack). The sprite list in the renderer moves from the stack to a static array (chunk 3).

Budget enforcement: each chunk states its size delta in its hand-off note; CI prints the `.bin` size and the `.data + .bss` size per build and fails above the limits in section 13.3.

## 3. Economy and progression

### 3.1 Principles

1. **Income is bounded per day, not per action.** However many chores fit in a day, repeatable daily income cannot exceed 132 coins (112 before the market stall).
2. **The wallet has a cap.** Coins shown on the HUD are always 0..999 (0..99 at the start).
3. **Every coin has somewhere to go.** There is always a current goal with a price; when it is affordable the hero walks to the shop or the building site and buys it, and something visible changes.
4. **Nothing is lost.** Income above the cap goes into the village fund; every 100 coins of fund becomes one star.
5. **All counters saturate.** No counter can wrap, ever (`u8` at 99 or 255, `u16` at 999 or 65535, `u32` at 4,294,967,295).

### 3.2 Coin flow per game day (targets, verified by the soak test in section 13.1)

| Flow | Rule | Per day |
|---|---|---|
| Shop sales | Mira's till: she pays out at most `TILL` coins per day (60; 80 after the market stall is built). The hero keeps a reserve of each good that the current goal needs. | 0..60 (80) |
| Errands | At most 3 paid errands per day, 6..14 coins each | 0..42 |
| Story and side quests | One-off rewards, 20..150 coins, on average under 10/day over the story | about 8 |
| Finds | Monster drops and treasure, at most 10/day | 0..10 |
| **Income** | | **typical 70..90; at most 132 from repeatable sources (80 + 42 + 10); the till doubles on the 4 festival days** |
| Meals | Tavern lunch 4, supper 5 (skipped when broke; then eats own fish or crop) | 9 |
| Seeds, small buys | | 0..4 |
| **Upkeep** | | **about 10** |
| **Net** | | **about +60..80** |

Total one-off sinks: tools 2,510 + projects 2,140 = 4,650 coins, about 66 days at +70/day (9.5 weeks, 2.4 game years). After that: stars (section 3.6).

### 3.3 Items (`inv[24]`, one `uint8_t` count each)

| Id | Name | Cap | Sell price | Source |
|---|---|---|---|---|
| 0 | WOOD | 30 | 1 | chop: 1 per tree (2 with Axe II, 3 with Axe III) |
| 1 | STONE | 30 | 2 | mine |
| 2 | FISH | 20 | 3 | fish (species go to the collection) |
| 3 | MUSHROOM | 20 | 2 | forage, forest, spring and autumn |
| 4 | BERRY | 20 | 2 | forage, summer |
| 5 | FLOWER | 20 | 1 | forage, spring and summer |
| 6 | SHELL | 20 | 2 | shore |
| 7 | ORE | 20 | 5 | mine, needs Pick II |
| 8 | GEM | 9 | 20 | mine (rare), boss drops |
| 9 | CROP | 20 | 4 | garden |
| 10 | SEED | 9 | (buy 2) | shop |
| 11 | HERB | 20 | 3 | forest |
| 12 | GEL | 20 | 2 | slimes |
| 13..15 | reserved goods | | | |
| 16 | MAP | 1 | | key item |
| 17 | KEY | 1 | | key item |
| 18 | SHARD | 4 | | key item, one per story chapter 1..4 |
| 19 | PARCEL | 1 | | errand deliveries |
| 20 | ROPE | 1 | | key item (finale) |
| 21 | CHARM | 1 | | key item (Luma) |
| 22 | LANTERN OIL | 9 | (buy 3) | used 1 per ruins trip |
| 23 | reserved | | | |

In-season goods sell for +1 coin, out-of-season forage cannot be found. A festival day doubles the till.

Selling rule (`econ_sell_all`): for each good in id order, sell `count - reserve(item)` units while the till has coins; `reserve(item)` is the amount of that item the current goal still needs, capped at the item cap, and at least 3 for WOOD. This fixes "0 wood": the HUD shows the stock the hero is building towards.

### 3.4 Wallet cap and overflow

`cap = {99, 250, 500, 999}[tool[TOOL_PURSE]]`. `econ_earn(n, src)`: `room = cap - coins; coins += min(n, room); fund += n - min(n, room); while (fund >= 100) { fund -= 100; stars = min(stars + 1, 999); emit GQ_EV_STAR }`. `earned_total += n` (saturating `u32`).

### 3.5 Goals: upgrades and village projects

One ordered table `GOALS[22]`, 10 bytes per entry (`kind, id, price_lo, price_hi, mat_item, mat_count, mat2_item, mat2_count, prereq_goal, min_day`). The current goal is the first entry not yet bought whose prerequisite is bought and whose `min_day` has passed. The hero buys it at the shop (tools) or at the site with Tess (projects), with a 10 to 15 s visible scene and a dialogue line.

| # | Goal | Coins | Materials | Earliest day | Visible effect |
|---|---|---|---|---|---|
| 1 | Purse II (cap 250) | 60 | | 1 | bigger pouch icon on the HUD |
| 2 | Notice board | 40 | 5 wood | 2 | board tile on the plaza; errands unlock |
| 3 | Axe II | 100 | | 3 | axe sprite changes; 2 wood per tree |
| 4 | Garden plot | 80 | 10 wood | 4 | 4 crop tiles by the home; gardening unlocks |
| 5 | Rod II | 100 | | 5 | better catch odds; summer fish |
| 6 | Bridge | 120 | 20 wood | 7 | bridge tiles south; shore area opens |
| 7 | Pick I | 80 | | 9 | mining unlocks (cave, stone) |
| 8 | Purse III (cap 500) | 200 | | 10 | |
| 9 | Well | 150 | 20 stone | 12 | well on the plaza; villagers fetch water |
| 10 | Lantern | 120 | | 14 | hero carries a light at dusk; ruins unlock |
| 11 | Boots | 150 | | 16 | hero walks 25% faster (24 -> 18 ms per pixel) |
| 12 | Market stall | 250 | 25 wood | 18 | stall tiles; Wren visits; till 80 |
| 13 | Street lanterns | 200 | 10 ore | 20 | 4 lantern tiles that glow at night |
| 14 | Sword II | 250 | | 21 | fights are shorter and safer |
| 15 | Pick II | 300 | | 23 | ore and gems |
| 16 | Flower beds | 100 | 20 flowers | 24 | flower tiles along the street |
| 17 | Bell tower repair | 400 | 30 stone, 4 shards | 27 | story finale; bell chimes on the hour |
| 18 | Purse IV (cap 999) | 450 | | 30 | |
| 19 | Axe III | 350 | | 34 | 3 wood per tree |
| 20 | Rod III | 350 | | 38 | rare fish |
| 21 | Festival stage | 300 | 30 wood | 42 | stage tiles; bigger festival scenes |
| 22 | Fountain | 500 | 30 stone, 2 gems | 50 | animated fountain on the plaza |

Sum of coins: 4,650. `projects` is a `u16` bit mask (bit = project index 0..9); tool levels are in `tool[8]`.

### 3.6 After the last goal

The goal becomes "VILLAGE FUND": whenever the wallet reaches the cap, the hero donates 100 coins at the notice board and gains a star. Stars show on the HUD and on the village card. At 4 game years of maximum play the star counter reaches about 400; it saturates at 999.

### 3.7 Number formatting

`void fmt_compact(char* out, uint32_t v)` writes at most 4 characters plus NUL:

| Input | Output | | Input | Output |
|---|---|---|---|---|
| 0 | `0` | | 10000 | `10K` |
| 999 | `999` | | 999999 | `999K` |
| 1000 | `1.0K` | | 1000000 | `1.0M` |
| 1234 | `1.2K` | | 12345678 | `12M` |
| 2831 | `2.8K` | | 999999999 | `999M` |
| 9999 | `9.9K` | | 4294967295 | `4.2B` |

Truncation, never rounding up. The HUD coin field is always at most 3 digits (cap 999); `fmt_compact` is used for lifetime totals on the cards and the status page.

### 3.8 Friendship, collection, achievements, statistics

- **Friendship** `friend[12]`, 0..100 per NPC. +2 per chat (once a day per NPC), +5 per gift (once a day in total), +10 per finished quest for that NPC. Thresholds 25/50/75 unlock that NPC's side quests and new lines. Shown as 0..4 hearts on the village card.
- **Collection** `seen[8]` (64 bits, 32 used): set when a species or treasure is first obtained; the hero shows it above his head with a "NEW!" line.
- **Statistics** `stat[12]` (`u16`, saturating): trees, fish, forage, mined, monsters, quests, errands, chats, meals, festivals, faints, gifts.
- **Achievements** `ach[4]` (32 bits, 24 used). Table of 24 entries `{kind, index, threshold_lo, threshold_hi, text_id}` (5 bytes): 12 statistic thresholds (for example 100 trees, 50 fish, 25 monsters, 20 quests), 4 collection sets (all fish of a season), 4 story milestones, 4 others (first star, all projects, one full year, friend of everyone). On unlock: a 6 s banner in the bottom band and `GQ_EV_ACHIEVEMENT`.

## 4. Quests and story

### 4.1 Story arc: "The Evenbell"

The bell in Elderwick's tower has been silent since the four season shards of its clapper were scattered. One chapter per shard, then the repair.

| Chapter | Earliest day | Needs | Place | Giver | Monster | Reward |
|---|---|---|---|---|---|---|
| 0 A quiet bell | 1 | | village | Bram | | 20 coins, MAP |
| 1 Spring shard | 4 | Axe II | forest | Luma | slimes | SHARD, CHARM, 40 |
| 2 Summer shard | 9 | Bridge | shore | Finn | crabs | SHARD, 60 |
| 3 Autumn shard | 15 | Pick I | cave | Tess | bats | SHARD, 80 |
| 4 Winter shard | 21 | Lantern | ruins | Bram | ghost, Mossback | SHARD, 100 |
| 5 The Evenbell rings | 27 | Bell tower repair, festival day | village | everyone | | 150, bell chimes hourly |

At most one story step advances per game day (`QS_WAIT_DAY` steps), so the story takes at least 27 days. After chapter 5, at the top of every hour the bell tile swings and "DING" floats up: the reward is a clock feature. From year 2, four "legend" quests (one per season, the area's boss gets one more heart each year, capped at 12) repeat with new lines.

Side quests (14): two per NPC for six NPCs plus two for the cat; unlocked by friendship 25 and 50. Errands (8 templates): fetch N of an item for an NPC, deliver a PARCEL, clear M monsters, catch a fish before noon, and so on; three are drawn each morning once the notice board exists.

### 4.2 Data format (flash)

```c
// quest_data.h (chunk 5). All tables GC_ROM_ATTR, read with GC_RD8.
typedef struct {            // 8 bytes
  uint8_t giver;            // GQ_NPC_*
  uint8_t first_step;       // index into QSTEPS
  uint8_t n_steps;
  uint8_t req_kind;         // QR_NONE, QR_DAY, QR_QUEST, QR_TOOL, QR_FRIEND, QR_PROJECT, QR_SEASON
  uint8_t req_val;          // day, quest id, (tool<<4)|level, (npc<<4)|(level/25), project, season
  uint8_t reward_coins5;    // coins / 5 (0..255 -> 0..1275)
  uint8_t reward_item;      // GQ_ITEM_* or 0xFF
  uint8_t title_txt;        // text id, at most 20 characters
} QuestDef;

typedef struct {            // 6 bytes
  uint8_t type;             // QS_TALK, QS_FETCH, QS_DELIVER, QS_GOTO, QS_SLAY, QS_DO, QS_WAIT_DAY, QS_SCENE
  uint8_t a, b, c;          // see table below
  uint8_t txt_start;        // dialogue shown when the step starts (0xFF none)
  uint8_t txt_band;         // short text for the bottom band, at most 14 characters
} QuestStep;
```

| Step type | a | b | c | Completes on |
|---|---|---|---|---|
| QS_TALK | npc | | | `GQ_EV_TALKED(npc)` |
| QS_FETCH | item | count | | inventory holds count |
| QS_DELIVER | npc | item | count | `GQ_EV_TALKED(npc)` while holding the items (they are taken) |
| QS_GOTO | area | tile x | tile y | `GQ_EV_ARRIVED(area, tile)` |
| QS_SLAY | monster | count | area | count times `GQ_EV_SLAIN(monster)` |
| QS_DO | activity | count | | count times the matching activity event |
| QS_WAIT_DAY | | | | next `GQ_EV_NEW_DAY` |
| QS_SCENE | scene id | | | `GQ_EV_SCENE_DONE(scene)` (scripted moment, chunk 3) |

Strings: one blob `TEXT_BLOB[]` plus `uint16_t TEXT_OFF[]`; `gq_text(id, buf, cap)` copies one string to RAM. Upper case, the 64 font glyphs only, `|` is the line break, at most 19 characters per line, 2 lines. A host test checks every string against these rules.

### 4.3 State (RAM, saved)

`chapter`, `q_active` (quest id or 0xFF), `q_step` (step within the quest), `q_count` (progress counter), `q_done[8]` (64 bits), `errands_today`. Only one quest is active at a time: priority story > side > errand. The quest module never moves the hero. It answers one question, `quest_need(GqNeed*)` ("to make progress, be at area/tile, or talk to this NPC, or hold N of item"), and listens to events. The hero scheduler (chunk 3) decides when to follow the need.

### 4.4 The hero never vanishes: camera and presence rules

1. **The camera is where the hero is.** Leaving through an exit or a door closes a curtain (0.3 s), loads the other area into the same two map arrays, and opens the curtain (0.3 s). The village, the forest, the shore, the cave, the ruins and three interiors are all 15x15 maps.
2. **Quests are acted out.** In a quest area the hero walks to the target, fights (sword swing, monster flashes, poof, a coin pops out), chops, opens a chest, talks. Outcomes are rolled at the start of an encounter and then animated; there is no collision-based combat. Hearts (3 to 6) are drawn in the bottom band. At 0 hearts the hero says "OUCH! HOME I GO." and walks home; nothing is lost except the step, which is retried another day.
3. **Bottom band (strip 14).** Always visible unless a dialogue box is open: an icon, a text of at most 20 characters and a 2-pixel progress line. Content priority: achievement banner, quest step (`SLIMES 2/3`), goal savings (`AXE II 73/100`), festival name. When WiFi is down, the network status lines alternate with it every 5 s.
4. **Night.** The hero sleeps in the home interior (bed, Zzz, moon in the window). Every 3 to 5 min the camera cuts to the night village for 60 s and back. In the village a Zzz bubble sits over the home door.
5. **Invariant (host-tested, section 13.1).** Sampled once per simulated second over 3 days: `gc_query(GQ_Q_HERO_PRESENCE) != 0` except during a curtain (at most 700 ms), and at least one strip becomes dirty in every 2 s window.

Stopgap in chunk 0 (before areas exist): while the hero is "away", strips 12..14 show a **quest postcard**: the place name, a track with a small walking hero moving out and back in proportion to elapsed time, and a status line that changes by thirds ("ON THE ROAD", "FIGHTING SLIMES!", "HEADING HOME"). Shop and tavern stays show a door marker (hero face icon alternating with an activity icon above the door) and the tavern stay drops from 15..30 s to 8..12 s.

## 5. Calendar, seasons, weather, festivals, events

### 5.1 Time

- `minute` (0..1439) comes from the wall clock when valid, otherwise from the game's own clock (60 s per minute), starting from the saved minute.
- `day` (`u16`, saturating at 65535) increases by one only on a natural midnight crossing: the previous minute was at least 1435 and the new one is at most 5. Any other jump (NTP arriving, time zone or DST change, restore from a save) changes `minute` but never `day`. Daily quotas reset only when `day` increases. This makes time zone edits and daylight saving harmless: at worst one game day is 23 or 25 hours long.
- While the clock is unplugged the world is asleep: it resumes on the saved day.
- Season = `(day / 7) % 4` (spring, summer, autumn, winter); day of season = `day % 7 + 1`; year = `day / 28 + 1`.

### 5.2 Seasons

A season is (a) a 16-entry palette remap applied to world tiles only, (b) a different forage table and fish table, (c) different weather odds, (d) four seasonal tile substitutions (flower tile, tuft tile, tree top, water in winter becomes ice near the shore). Actors and items are drawn with a palette that is not season-remapped, so clothes keep their colours.

| Season | Palette remap (world only) | Look |
|---|---|---|
| Spring | identity | green, flowers |
| Summer | light green -> yellow, green slightly warmer | dry, bright |
| Autumn | dark green -> brown, green -> red-brown, light green -> yellow | orange trees |
| Winter | greens -> white / foam / grey, sand -> white | snow; water tiles animate slower |

### 5.3 Weather

7 states. A state lasts 2 to 8 game hours (`weather_hours`), then the next is drawn from a season row of 7 weights (28 bytes). Festival days are forced clear until 22:00.

| State | Palette effect (wash colour, strength; tint) | Particles | Behaviour |
|---|---|---|---|
| CLEAR | none | none (butterflies in spring, fireflies on summer nights) | default |
| CLOUDY | grey 15% | none | none |
| RAIN | blue-grey 25%, tint 215/225/240 | 16 streaks | no bench or fire sitting; fishing odds +20%; NPCs keep closer to home |
| STORM | blue-grey 40%, tint 170/180/210, lightning flash every 20..60 s | 24 streaks | hero prefers indoor activities; no quests outdoors |
| FOG | white 45% | none | wander radius halved |
| SNOW | white 20% | 16 flakes, slow | winter only; snowman appears on the plaza |
| WINDY | none | 8 leaves or petals moving sideways | none |

### 5.4 Festivals and daily events

Festivals (day 7 of each season, 16:00 to 22:00, all NPCs on the plaza, special tiles, one festival quest step, +1 `stat[FESTIVALS]`): **Blossom Fair** (spring, petals, flower gifts), **Lantern Night** (summer, lantern tiles, fireflies), **Harvest Feast** (autumn, table tiles, everyone eats), **Snow Lights** (winter, lights on houses).

One random event per day is drawn at 06:00 (40% chance of none): market sale (prices +1), bumper demand (till x1.5), fish frenzy, mushroom bloom, slime trouble at the east gate, lost chicken errand, message in a bottle (treasure), travelling bard at the tavern, Pip's hide and seek, the cat brings a gift, shooting stars (night), ghost lights in the ruins (night). Stored in `event_today`.

## 6. Rendering, performance and UI

### 6.1 Frame budget

There is no framebuffer and there will not be one. Assumed cost per 240x16 strip: about 1.6 ms to send at 40 MHz plus 1.5 to 3 ms to render, so about 4 ms; a full screen is about 60 ms. These numbers are estimates: the hardware checklist (section 13.5) asks the user to read the real FPS and `loop_max_ms` from the status page, and chunk 2 adds those fields.

| Rule | Limit |
|---|---|
| Average dirty strips per frame in steady state | at most 5 |
| Full-screen redraws (tint step, card open or close, lightning) | at most 1 per 2 s, except curtains |
| Weather particles | at most 64 strip redraws per second |
| Longest time in `loop()` without returning | 80 ms target, 250 ms hard (soft watchdog is about 3 s) |
| Governor | when the FPS passed in through `gc_set_fps` is under 12 for 5 s, weather drops to half density and half rate; under 8, particles stop. Restores above 18. |

### 6.2 Palette pipeline (chunk 2, `gamecore.cpp`)

Three palettes of 16 RGB565 entries: `g_pal` (world), `g_act` (actors and items), `g_ui` (HUD, text). Built in this order whenever the look changes:

```
base[i]                          (GC_PALETTE, flash)
 -> season remap   (world only)  idx = SEASON_REMAP[season][i]
 -> wash           (world+act)   c = c + (wash_rgb - c) * wash_k / 256      per channel
 -> tint           (world+act)   c = min(max, c * tint / 256)               per channel (day/night, weather)
 -> depth curve    (all)         existing curve() in gamecore.cpp
```

```c
typedef struct {
  uint8_t  season;        // 0..3
  uint8_t  wash_k;        // 0..255
  uint16_t wash_rgb565;
  int16_t  tr, tg, tb;    // tint, 256 = 1.0, range 0..320
} GqLook;
void gfx_set_look(const GqLook* look);   // rebuilds the 3 palettes and marks all strips dirty
                                         // only if the quantised look changed
```

Quantisation: tint to 5 bits per channel (as today), `wash_k` to 4 bits, so dawn and dusk cause a full redraw about once a minute at most. Lightning is two calls 80 ms apart.

`GqLook` is declared in `gq_types.h`. Until chunk 2 lands, chunk 0's `gfx_set_look` ignores `season` and the wash and calls today's `set_tint(tr, tg, tb)`.

### 6.3 Draw order per strip (`game.cpp`, chunk 0, frozen)

```
if (ui_card_covers(strip))  ui_render(strip, out)         // card fully covers rows 2..12: skip the world
else { world_render(strip, out);                          // ground, objects, actors (chunk 3)
       weather_render(strip, out);                        // particles (chunk 6)
       ui_render(strip, out); }                           // HUD, band, dialogue, curtain, cards (chunk 2)
```

When the curtain covers a strip completely, `ui_curtain_covers(strip)` lets `game.cpp` skip the world as well and fill black.

### 6.4 Weather particles without a framebuffer

Particle positions are a pure function of `(index, weather_phase)`, so nothing is stored per frame. Rain: every 120 ms `weather_phase++` and the odd or even strips (alternating) are marked dirty: about 62 strip redraws per second. Snow and leaves: every 200 ms. `weather_render` draws at most 24 particles of 1x4 (rain), 2x2 (snow) or one 8x8 leaf cell.

### 6.5 Curtain (area transitions)

`ui_curtain(1)` closes in 8 steps of 40 ms: step `k` makes strips `k-1` and `15-k` black (2 strips dirty per step, the last step 1 strip). While closed, the world swaps the map. `ui_curtain(0)` opens the same way in reverse. Cost: one full redraw spread over 16 frames.

### 6.6 Screen layout

```
strip 0   HUD:  [coin icon][coins, 3 digits]  [item icon][count, 2 digits]  [weather icon] [SPR/D3] [!]  [HH:MM]
                 x=0        x=17..52           x=56       x=73..96          x=100          x=120    x=150 x=178
strips 1..13    world (13 visible tile rows)
strip 14  band: [icon 16 px] [text, 18 characters at scale 2] and a 2 px progress line at y=238
strips 12..14   dialogue box when someone speaks (covers the band)
```

- The second HUD field is the **focus item**: the material the current goal or quest step needs (wood by default).
- `SPR/D3` is two lines at scale 1: season (SPR, SUM, AUT, WIN) and day of season.
- `!` in red only when the station link is down.
- When the clock is only approximately known (restored from RTC after a restart, NTP not yet synced) the time is drawn in grey.
- Bottom-band network status keeps today's behaviour (first 90 s after boot and whenever WiFi is down) but alternates with the quest band every 5 s.

### 6.7 Info cards

A card is a panel of 224x176 pixels (strips 2..12, x 8..231) drawn over the darkened world; the game keeps running underneath. Cards never open during a dialogue, a curtain, a fight or a festival scene; they wait up to 60 s and are then skipped.

| Card | Shown | Content |
|---|---|---|
| CLOCK | 15 s at every full hour, and in rotation | time at scale 6 (180 px wide), date line `WED 11 NOV`, season and day, weather name, NTP age if the clock is approximate |
| PACK | rotation | 4x3 grid of item icons with counts, current goal with a bar (`AXE II 73/100`), wallet and cap |
| QUEST | rotation | chapter pips (6), quest title, current step text, giver name |
| VILLAGE | rotation | projects built `n/10`, stars, friends with hearts, achievements `n/24`, day and year, lifetime coins with `fmt_compact` |

Rotation: one card for 10 s every `card_period` minutes (default 10; 0 = never; setting on the status page). Real-world weather from the internet is **out of scope**: it needs an HTTP client that can block the loop and adds 10 to 15 KB; it would need its own approval.

### 6.8 Browser preview (chunk 2)

The preview gets a debug panel: time scale (x1, x10, x60, x600), jump to day, set season and weather, give coins, start quest N, show any card, "export save" (hex) and "import save" (simulates a restart through `gc_save_export` and `gc_save_import`), and a live read-out of `gc_query` values. This is the main tool for the user to see content before flashing.

## 7. Network, time and offline behaviour

### 7.1 Principles

1. The reconnect logic is a **pure state machine** (`lib/gqsafe/src/netfsm.cpp`, no Arduino calls) stepped once per second. It returns a bit mask of actions; `firmware/netwatch.cpp` carries them out. This makes weeks of outages testable on the host.
2. **Nothing blocks.** No `while (WiFi.status() ...)`, no `delay()` longer than the existing ones, no synchronous DNS or HTTP client. `WiFi.begin()` returns immediately.
3. **The rescue hotspot wins.** No action that could disturb a phone connected to the hotspot runs while a client is connected, during an upload, or (except a plain reconnect attempt) in the golden window.
4. **Credentials are held in RAM.** At boot the stored station configuration is read once (`wifi_station_get_config_default`) into `s_ssid[33]` and `s_pass[65]`; every attempt is `WiFi.begin(s_ssid, s_pass)` with `WiFi.persistent(false)`, so no attempt writes flash and none depends on what the SDK currently holds. The WiFi form updates the RAM copy as well as the SDK's stored copy (as today).
5. SDK auto-reconnect is **off** (`WiFi.setAutoReconnect(false)`). Its continuous scanning while the router is away is the main reason a soft AP in AP+STA mode becomes unusable, and its give-up cases are the likely reason the clock never came back.
6. Modem sleep is off (`WiFi.setSleepMode(WIFI_NONE_SLEEP)`): fewer beacon timeouts and the web page answers reliably. Cost: about 50 mA more on average (about 0.25 W). **NEEDS USER APPROVAL** (power and warmth; not a brick risk).

### 7.2 State machine

Inputs and outputs:

```c
// lib/gqsafe/src/netfsm.h
typedef struct {
  uint32_t now_ms;          // millis()
  uint8_t  sta_up;          // WiFi.status()==WL_CONNECTED and IP != 0
  uint8_t  have_creds;      // s_ssid[0] != 0
  uint8_t  ap_clients;      // WiFi.softAPgetStationNum()
  uint8_t  updating;        // upload in progress
  uint8_t  safe_mode;
  uint8_t  ever_connected;  // since power-on (kept in RTC)
  uint8_t  restarts_done;   // WiFi-caused restarts since the last good link (RTC)
  uint32_t last_http_ms;    // last request served (0 = never)
  uint32_t last_ntp_ms;     // last successful NTP sync (0 = never)
} NetIn;

enum {                      // action bits
  NA_BEGIN        = 1,      // WiFi.begin(s_ssid, s_pass)
  NA_STA_STOP     = 2,      // wifi_station_disconnect(): stop scanning, keep config
  NA_STA_CYCLE    = 4,      // WiFi.enableSTA(false); then true on the next step
  NA_AP_ON        = 8,
  NA_AP_OFF       = 16,
  NA_MDNS_RESTART = 32,
  NA_SNTP_KICK    = 64,     // call configTime() again
  NA_RESTART      = 128     // ask safety::requestRestart(RR_WIFI)
};

typedef struct NetFsm NetFsm;                       // about 64 bytes, all uint32_t/uint8_t
void    netfsm_init(NetFsm* f, uint32_t now_ms, uint32_t seed);
uint8_t netfsm_step(NetFsm* f, const NetIn* in);    // call once per second
void    netfsm_kick(NetFsm* f);                     // user saved new credentials: attempt now
uint8_t netfsm_stage(const NetFsm* f);              // 0 online, 1 retrying, 2 cycling, 3 restart pending, 4 no credentials
uint32_t netfsm_attempts(const NetFsm* f);
```

Ladder (all times measured from the moment the link was lost; every comparison is `now - since >= period`):

| Stage | When | Action |
|---|---|---|
| 0 ONLINE | link up | Nothing. On the up-edge: `NA_MDNS_RESTART`. AP off once the link has been up 60 s, the golden window is over, no AP client, no hotspot-side request for 5 min. |
| 1 RETRY | link down, credentials known | `NA_BEGIN` at 3 s, then after 15, 30, 60, 120, 300 s and every 300 s from then on (each delay with up to 10% jitter from the seeded generator). Each attempt gets 20 s; if still down, `NA_STA_STOP` so the radio stops scanning and the hotspot stays steady. `NA_AP_ON` after 60 s down. While an AP client is connected, attempts are at least 300 s apart. |
| 2 CYCLE | down 10 min, then every 30 min | `NA_STA_CYCLE` before the next attempt. Only when no AP client, not updating, golden window over. |
| 3 RESTART | down 6 h (first), 12 h (second), 24 h (third and later) | `NA_RESTART`. Only when: uptime at least 30 min, no AP client, not updating, not safe mode, and (`ever_connected` or down at least 24 h). The game state is saved to RTC first, so the restart costs about 5 s of dark screen. |
| 4 IDLE | no credentials | AP on permanently. Nothing else. |
| Zombie check | link "up" but no NTP success and no served request for 6 h (or NTP never synced after 30 min up) | `NA_STA_STOP`, then `NA_BEGIN` 2 s later, plus `NA_SNTP_KICK`. At most once per 6 h. |
| Safe mode | always | AP on. `NA_BEGIN` every 300 s when no AP client. Nothing else. |
| Golden window | first 10 min of uptime | Only `NA_BEGIN`, `NA_STA_STOP`, `NA_AP_ON`, `NA_MDNS_RESTART`. Never `NA_AP_OFF`, `NA_STA_CYCLE`, `NA_RESTART`. |

Why a restart at all: it is the only remedy for a stuck SDK. Why so late and so rarely: a router that is off for a holiday must not cause a reboot storm (worst case 1 per day).

### 7.3 Time

- NTP: `configTime(tz, "pool.ntp.org", "time.google.com")` as today; the SDK resyncs hourly. `settimeofday_cb` records `last_ntp_ms`.
- After a restart without NTP, the clock is restored from the RTC save (`epoch` at save time plus 5 s) and marked approximate (grey digits) until the first sync. After a power cut the HUD shows `--:--` and the game runs on its own clock from the saved minute.
- Time zone input is validated before `setenv`: length 3..34, characters `A-Z a-z 0-9 < > + - : , . /` only. An invalid value is rejected with a message; the previous value stays.
- Offline for days: nothing stops. The system clock free-runs (drift of a few seconds per day), the game continues, the band shows the hotspot name and password in rotation with the quest text, and the HUD shows `!`.

### 7.4 Status page additions (chunk 1)

- **Network**: ladder stage, attempts since loss, seconds since loss, last 8 network events (time, kind, SDK reason code), RSSI, channel, BSSID, seconds since the last NTP sync, counts of reconnects and WiFi restarts since power-on.
- **Health**: free heap, lowest free heap since boot, largest free block, uptime, FPS, `loop_max_ms`, last 4 restarts with reason (user, update, WiFi, heap, maintenance, crash).
- **Game**: day, season, coins, stars, current goal, save status (section 9), buttons (section 8.8).
- `/info` (JSON) gets the same values so the user can paste one line into a bug report.
- The same 8 network events are kept in RTC memory (blocks 112..127), so they survive the restart that the problem may cause.

## 8. Backup and recovery (the most important section)

### 8.1 What we can and cannot guarantee

The ESP8266 with the stock 4M3M layout has **one** firmware slot. An update is received into the free part of the 1 MB code area, verified, and then the boot loader copies it over the running firmware at the next restart. Consequences:

- There is **no automatic rollback**. Once the copy has happened, the old firmware is gone. "Rollback" always means: the *new* firmware's update page is used to upload the old `.bin`. So the one thing every build must be able to do is serve the update page.
- **We guarantee** (by design plus CI): every published `.bin` contains the update page, the rescue hotspot and the updater; fits so that both a GeekQuest build and the 488,304-byte stock image can be installed from it; uses the stock flash layout; and contains no code that writes stock's file area or stock's settings sector.
- **We guarantee by behaviour** (tested on the host where possible): after every power-on the hotspot is on for 10 minutes and nothing the game or the WiFi ladder does can turn it off or restart the clock in that time; a crash loop ends in safe mode; a damaged save cannot keep the game from starting.
- **We cannot guarantee**: (1) survival of a power cut during the boot loader's copy (about 3 to 6 s, once per update; the copy command lives in RTC memory, which a power cut erases, so the copy is not resumed); (2) a bug in the recovery layer itself that neither crashes nor serves the page (reduced by keeping that code small, frozen and reviewed, and by the golden window, but not eliminated); (3) hardware faults; (4) that the stock firmware never uses the flash sector we propose for saving (section 9; verified by a read-only probe before first use).

### 8.2 Threat model

| # | Threat | Protected today by | Gap | New protection (owner chunk) |
|---|---|---|---|---|
| 1 | Bad flash: wrong file, truncated file, image without an update page | Magic byte, 4 MB header, size check, `Update.end` verify | Truncation inside a valid length is not detected; any foreign ESP8266 image is accepted | CRC32 computed by the page and checked by the clock before commit; marker scan: an image without `GQ-OTA-PAGE-v1` is refused unless it is exactly 488,304 bytes (stock) or the user ticks "install anyway" (chunk 1). **NEEDS USER APPROVAL** because it touches the update path. |
| 2 | Boot loop (crash before 60 s) | Crash counter: safe mode at 3, no display at 6 | None found | Keep. Add stage markers 10 (game tick) and 11 (render) so the status page tells game crashes from network crashes (chunk 1). |
| 3 | Slow crash loop (crash after 60 s, forever) | Nothing: the counter is cleared at 60 s | Never reaches safe mode | Second RTC counter `slow`: +1 on every unplanned reset, cleared only after 6 h of uptime; safe mode at 5 (chunk 1). |
| 4 | Display crash | Stages 20..24, no display after 6 crashes | None found | Keep. |
| 5 | WiFi lost | SDK auto-reconnect, hotspot after 60 s | Does not always recover; no diagnostics | Ladder of section 7.2 (chunk 0B), restart stage and diagnostics (chunk 1). |
| 6 | Network code broken but not crashing | Hotspot for 10 min after boot | New ladder code could interfere | Golden window as a hard rule, host-tested on the state machine; safe mode uses the minimal policy (chunks 0B, 1). |
| 7 | Corrupted or hostile saved state | Nothing is saved | n/a | Magic, version, size, CRC32, then range clamping of every field; quarantine after one crash; factory reset button; safe mode never loads a game save (chunks 0, 1). |
| 8 | Update interrupted (WiFi drop, browser closed) | Staged update: nothing is replaced until verified | None for the upload. Power cut during the boot loader copy cannot be covered. | Page text tells the user to keep power for 60 s; state is saved to RTC before the restart; the game pauses during the upload (as today). |
| 9 | Watchdog resets (loop stuck) | Software and hardware watchdogs reset; crash counter counts them | Cause invisible | `loop_max_ms` on the status page; stage marker shows where; rule: no loop in the core without a bound (lint, chunk 1). |
| 10 | Heap exhaustion over weeks | Nothing | Web page and updater need about 10 KB of heap | Heap watchdog with planned restart (8.6); update page streamed from flash with no heap copies (8.5); weekly maintenance restart (8.7). |
| 11 | NTP failure | `--:--`, game runs on its own clock | Clock lost on every restart | Approximate time restored from RTC; zombie-link check uses NTP age (chunks 0B, 1). |
| 12 | Time zone change, DST | Applies immediately | Could skip or repeat game days; bad string | Day only advances on a natural midnight (5.1); input validation (7.3) (chunks 6, 1). |
| 13 | `millis()` wrap at 49.7 days | Mostly harmless today | New code could get it wrong | Rule and lint: only `now - since >= period`; host test steps the state machine across the wrap (chunk 0B). |
| 14 | A feature update passes CI but misbehaves on hardware | Safe mode, previous builds kept in `firmware/obsolete/` | Depends on the case | Section 8.9 procedure; "known good" list (chunk 1 docs). |
| 15 | Flash wear or a write to the wrong place | No writes at all | n/a | Section 9: one fixed sector, address from a constant checked against linker symbols, at most about 1 erase per 2 days, off until the user enables it. |
| 16 | Someone else on the WiFi or near the hotspot uploads firmware | Nothing (no login; hotspot password is printed on screen) | Accepted risk | No change: a login could lock the owner out, which is worse. Listed in the risk register. |

### 8.3 Crash counting (changes to `bootGuard`)

RTC block map after chunk 1 (block = 4 bytes; blocks 0..31 are used by the boot loader for the update command and must never be touched):

| Blocks | Bytes | Content | Owner |
|---|---|---|---|
| 32..79 | 192 | `GqSave` (game) | `persist.cpp` |
| 80..91 | 48 | `GqSettings` | `persist.cpp` |
| 92..95 | 16 | spare | |
| 96..100 | 20 | existing `RtcState` `{magic, crashes, planned, forceSafe, stage}`: layout unchanged, so the counter keeps working across updates between old and new builds | `safety.cpp` |
| 101..111 | 44 | `RtcState2` `{magic2, slow, restoredSave, wifiRestarts, everConnected, lastReason[4], bootCount, crc}` | `safety.cpp` |
| 112..127 | 64 | network event log, 8 entries of 8 bytes | `netwatch.cpp` |

Rules:

- `crashes`: as today (+1 per unplanned reset, cleared after 60 s of uptime; safe mode at 3, no display at 6).
- `slow`: +1 per unplanned reset, set to 0 after 6 h of uptime; **safe mode at 5**.
- Saved-game quarantine: `restoredSave` is set to 1 when a boot loaded a game save. On an unplanned reset with `restoredSave == 1` and `crashes >= 2`, the game starts from defaults and the stored save is left untouched ("quarantined") for this boot; the status page says so and offers "Try the saved game again" and "Discard the saved game". At `crashes >= 3` safe mode takes over as today.
- A cold boot (power-on) clears all counters, as today.

### 8.4 The golden window

For the first 10 minutes after every boot:

1. The hotspot is on (as today).
2. The state machine may only issue `NA_BEGIN`, `NA_STA_STOP`, `NA_AP_ON`, `NA_MDNS_RESTART`.
3. No automatic restart of any kind (WiFi, heap, maintenance) is allowed; automatic restarts additionally require 30 min of uptime.
4. No flash save happens.

The window is implemented with a latched flag (`s_golden`, set at boot, cleared when `millis() - bootMs >= 600000`), not with an absolute comparison.

What this gives the user: **unplug the clock, plug it in, join `GEEKQUEST-XXXX` within 10 minutes, open `http://192.168.4.1/update`.** That path depends only on `safety::begin()`, the soft AP and the web server, which is code that has already been proven on the user's clock and that game chunks are not allowed to edit. A host test on the state machine and a lint rule (section 13.3) keep it that way.

### 8.5 Web pages under low memory

- `/update` and `/update_ota` are sent with `server.setContentLength(CONTENT_LENGTH_UNKNOWN)` and three `server.sendContent_P()` calls (head, body, tail) straight from flash: zero `String` allocations.
- The status page is built in pieces of at most 512 bytes and streamed the same way.
- If free heap is under 6 KB when `/` is requested, a 300-byte flash-only page is sent instead: firmware version, "low memory", and links to `/update` and "Restart".
- Before an upload starts (`UPLOAD_FILE_START`): the game is paused (as today), mDNS and UDP are stopped, and free heap is logged. If `Update.begin` fails, the reply names the free heap.

### 8.6 Heap watchdog

Sampled every 10 s in `safety::loop()`: `ESP.getFreeHeap()` and `ESP.getMaxFreeBlockSize()`; the minimum since boot is kept for the status page.

| Condition (for 60 s without interruption) | Action |
|---|---|
| free heap under 12 KB | warning on the status page, entry in the restart-reason log as "heap low" (no restart) |
| free heap under 8 KB, or largest block under 4 KB | save state to RTC, planned restart with reason HEAP |

Not during an upload, not with an AP client connected, not in the first 30 min. If the heap runs out sooner than that on every boot it is a bug; the resulting crash is handled by the crash counter.

### 8.7 Maintenance restart

Once every 7 days of uptime, at the next time when: local time is between 04:00 and 04:30 (or, without a clock, the hero is asleep), no AP client, no request in the last 5 min, no upload. State is saved to RTC; the restart is planned, so no counter moves; the screen is dark for about 5 s. It clears slow leaks, heap fragmentation, stuck SDK state, and the `millis()` wrap. Can be switched off on the status page. **NEEDS USER APPROVAL** (together with the other automatic restarts: WiFi stage 3 and heap).

### 8.8 Status page controls (chunk 1)

| Control | Effect |
|---|---|
| Restart, Restart in safe mode | as today |
| Save now | writes the RTC copy; also the flash copy when flash saving is enabled |
| Download backup | `GET /backup` returns the 240-byte snapshot (settings + game) as a file `geekquest-dayN.gqs` |
| Restore backup | upload a `.gqs` file; it goes through the same validation as a boot-time load; rejected files change nothing |
| Factory reset game | game state back to day 1; settings kept; RTC copy and, if enabled, a new flash slot are written |
| Reset settings | brightness, colour depth, time zone, card period back to defaults |
| Enable saving to flash / Disable and erase | section 9.4; only visible in builds made with `GQ_FLASH_SAVE=1` |
| Try saved game again / Discard saved game | only while a save is quarantined |
| Maintenance restart on/off, night dimming on/off, card period | settings |

"Download backup" is the user's own backup, independent of the clock's memory: it works in every stage of the persistence decision, including "no flash writes at all".

### 8.9 Rolling back a build that passes CI but misbehaves

Old builds stay available on the `builds` branch (`firmware/obsolete/`). Chunk 1 adds `firmware/known-good/`: the newest build the user has confirmed after 24 h of running (the user says so, a maintainer moves the file; no automation).

| Symptom | What the user does |
|---|---|
| Clock works, feature is wrong | Open `http://<ip>/update` or `http://geekquest.local/update`, upload the known-good `.bin`. |
| Game crashes at start | Wait: after 3 crashes the clock shows SAFE MODE with the hotspot name. Open the page, upload. |
| Game crashes every few minutes | After 5 crashes safe mode starts by itself (new). Or press "Restart in safe mode" in a quiet moment. Then upload. |
| Screen black or garbled | The web page does not depend on the display. Find the IP in the router, or use the hotspot (on for 10 min after power-on). |
| Clock not on the home WiFi any more | Power-cycle, join `GEEKQUEST-XXXX` (password `GEEKQUEST`) within 10 min, open `http://192.168.4.1/update`. |
| Game state is nonsense | Status page, "Factory reset game". If the page says "saved game quarantined", choose "Discard". |
| Want the original clock back | Upload `SDPro_V1.0.6....bin` on the update page. If flash saving was enabled, press "Disable and erase" first so the save sector is blank again. |
| Nothing above works: no hotspot after power-on, no web page | This is the case that cannot be fixed without a serial adapter. Everything in this section exists to make it not happen. |

### 8.10 Rules for every chunk ("recovery contract")

1. Only chunk 0B and chunk 1 may edit `firmware/safety.cpp`, `firmware/safety.h`, `firmware/netwatch.*`, `firmware/persist.*`, `firmware/main.cpp`, `platformio.ini`, `tools/check_bin.py`, `tools/pio_post.py`, `tools/lint_safety.py`, `.github/workflows/build.yml`.
2. `safety::begin()` stays the first call in `setup()` after `Serial.begin`. `safety::loop()` stays the first call in `loop()`.
3. The core never calls anything outside itself: no includes other than its own headers, no heap, no libc, no floating point, no recursion, no unbounded loop (every `while` has a counter or a provable bound), no array index that is not range-checked or masked.
4. Every table index that comes from saved state is clamped at load (`gq_save_sanitize`) and the tables are sized to the clamp.
5. Every chunk keeps all CI gates of section 13.3 green and adds its host tests under `test/`.
6. A change to any file in rule 1 needs a line `RECOVERY-CHANGE: <why>` in the commit message and an increment of `GQ_RECOVERY_REV` in `safety.cpp`; CI fails when those files change without it.

## 9. Persistence: options, recommendation, approval

### 9.1 What would be saved

Two records, both versioned and CRC-protected:

- `GqSettings`, 48 bytes: `u32 magic "GQST"`, `u32 crc32` (over bytes 8..47), `u8 version`, `u8 brightness`, `u8 depth`, `u8 flags` (bit 0 maintenance restart, bit 1 night dimming), `u8 card_period`, `char tz[35]` (NUL-terminated). Owned by the firmware.
- `GqSave`, 192 bytes: the whole game (layout in section 10.3). Owned by the core.

### 9.2 Flash map (stock 4M3M layout) and who owns what

| Flash offset | Size | Content | May GeekQuest write it? |
|---|---|---|---|
| 0x000000..0x0FFFFF | 1 MB | Running firmware, then free space that the updater uses as staging | Only through the updater. Never directly: a wrong erase here kills the running firmware. |
| 0x100000..0x3F9FFF | 3048 KB | Stock firmware's LittleFS (web files, images) | **Never.** Not even mounted: mounting LittleFS on ESP8266 formats the area when the mount fails. |
| 0x3FA000..0x3FAFFF | 4 KB | Gap between the file system and the EEPROM sector in every standard 4 MB Arduino layout | Proposed save sector (P1). To be verified, see 9.4. |
| 0x3FB000..0x3FBFFF | 4 KB | EEPROM emulation sector: stock firmware's settings | **Never.** |
| 0x3FC000..0x3FFFFF | 16 KB | RF calibration and the SDK's WiFi configuration | Only the SDK writes here (when the WiFi form saves credentials, as today). |

The offsets come from the Arduino linker script `eagle.flash.4m3m.ld` (`_FS_start = 0x40300000`, `_FS_end = 0x405FA000`, `_EEPROM_start = 0x405FB000`). They could not be checked in the development container (no toolchain); chunk 1 adds a CI check that reads the three symbols from the ELF and fails if they differ.

### 9.3 Options

| Option | Survives | Flash wear | Can it harm stock or brick? | Verdict |
|---|---|---|---|---|
| A. No persistence (today) | nothing | none | no | Game resets at every restart; a weekly maintenance restart or an update would wipe progress. Rejected as the end state, kept as the fallback. |
| B. RTC user memory (240 bytes in blocks 32..91) | planned restarts, updates, crashes, watchdog resets | none (it is RAM) | no | **Recommended as stage P0, always on.** Lost on power cut. |
| C. EEPROM library | power cut | low | **Yes: it is stock's settings sector.** | Forbidden. |
| D. LittleFS in the 3 MB area | power cut | low | **Yes: stock's files; auto-format on mount failure; about 50 KB of code.** | Forbidden. |
| E. A sector inside the 1 MB code area above our image | power cut, but erased by updates of large images | low | A wrong address erases running code: the only way a save bug could brick the clock. | Rejected. |
| F. Gap sector 0x3FA000 with an append-only journal | power cut | about 1 erase per 2 days at most | Neighbours are stock's last file-system block and stock's EEPROM sector; a wrong address would damage stock data, not the boot path. Not a brick risk. Unverified whether stock uses this sector. | **Recommended as stage P1, off until the user enables it. NEEDS USER APPROVAL.** |
| G. Backup file through the browser | everything, on the user's phone or PC | none | no | **Included in every stage** (section 8.8). |

### 9.4 Stage P1 in detail (NEEDS USER APPROVAL)

**Gate 1, build time.** The code is only compiled with `-DGQ_FLASH_SAVE=1`. Default 0 until the user approves in writing.

**Gate 2, probe (read-only).** With the flag on, the status page shows a line computed with `ESP.flashRead` only: "Save sector 0x3FA000: blank" (all 0xFF), "GeekQuest journal, N slots used, erased M times", or "IN USE BY SOMETHING ELSE" with the first 16 bytes in hex. It also shows a CRC32 of the stock settings sector 0x3FB000, so the user can see that it never changes. In the third case every write function refuses to run and there is no override.

**Gate 3, user action.** Nothing is ever written until the user presses "Enable saving to flash" on the status page. That press writes the 16-byte journal header. The presence of a valid header *is* the enabled flag. "Disable and erase" erases the sector back to all 0xFF, which is the state it was found in.

**Address safety.** One constant `GQ_SAVE_SECTOR = 0x3FA`. No function takes a sector number as a parameter. At boot, persistence disables itself for this boot (and says so on the status page) unless all of these hold: `((uint32_t)&_FS_end - 0x40200000) / 4096 == 0x3FA`, `((uint32_t)&_EEPROM_start - 0x40200000) / 4096 == 0x3FB`, `ESP.getFlashChipRealSize() == 4 * 1024 * 1024`.

**Journal format** (pure logic in `lib/gqsafe/src/journal.cpp`, host-tested against a simulated flash):

```
sector (4096 bytes):
  offset 0     header, 16 bytes: magic "GQJ1", u32 erase_count, u32 reserved (0xFFFFFFFF), u32 crc32(first 8 bytes)
  offset 256*k slot k, k = 1..15, 256 bytes each:
      u8  type (1 = snapshot)   u8 format (1)   u16 payload_len (240)
      u32 seq
      u8  payload[240]          = GqSettings (48) followed by GqSave (192)
      u32 crc32                 over the first 248 bytes of the slot
      u32 commit                0x4B4F5147 ("GQOK"), written last, in a separate write call
```

- Write: find the first slot that reads all 0xFF, write 252 bytes, read back and compare, then write the commit word, read back. NOR flash can only clear bits, so a torn write leaves a bad CRC or a missing commit word and the slot is ignored.
- Read: the valid slot with the highest `seq` wins.
- Sector full: erase, rewrite the header with `erase_count + 1`, write the snapshot to slot 1. This is the only erase, about once per 15 saves.
- A slot that is neither blank nor valid is skipped. If no blank slot is left, erase.

**Write policy.** Automatic flash saves are at least 60 minutes apart: at the game's day change, 5 minutes after a settings change, and before a planned restart if the last flash save is older than 6 hours. "Save now" is limited to once per 10 s. Never during an upload, in the golden window, within 2 s of an HTTP request, or in safe mode (safe mode reads settings, never writes). Expected: 1 to 3 slot writes per day, one erase every 5 to 15 days. Even the worst case allowed by the policy (24 writes per day, 1.6 erases per day) gives more than 17 years at a pessimistic 10,000 erase cycles.

**Load order at boot.** Settings: RTC copy if valid, else the newest valid flash slot, else defaults. Game: of the valid candidates (RTC, flash) the one with the higher `seq`; each must pass `gc_save_import` (CRC, version, range clamp). In safe mode only settings are loaded.

**Failure modes and recovery:**

| Failure | Effect | Recovery |
|---|---|---|
| Power cut while a slot is written | That slot is invalid; the previous slot is used | Automatic. At most one day of progress lost (less with the RTC copy if it was only a restart). |
| Power cut during the sector erase (about 50 ms, once per 5 to 15 days) | All slots gone | Automatic: defaults (day 1, default time zone). The user can restore a downloaded backup file. Not a brick. |
| Flash sector worn out or stuck bits | Read-back mismatch; the slot is skipped; after 3 consecutive failed writes flash saving turns itself off for this boot and the status page shows "save sector failing" | RTC and backup file still work. |
| Save content is valid but crashes the game (a bug) | One crash, then quarantine (8.3), then safe mode | "Discard saved game" or "Factory reset game" on the status page; fix by update. |
| Save written by a newer build, then downgrade | Older build sees an unknown version, ignores it, starts from defaults, and appends its own slots | Progress from the newer build is lost unless a backup file is restored on a build that understands it. |
| Stock firmware turns out to use sector 0x3FA000 | The probe shows "in use by something else"; nothing is ever written | No harm done. P1 stays off; P0 and backup file remain. |
| A bug writes the wrong sector | Prevented by a single constant, the boot-time symbol checks, the lint rule that flash-write calls exist in one file only, and the host tests. If it happened anyway, the neighbours are stock's data, which stock can rebuild from its own update files; the boot path and the update page are in a different megabyte. | Reflash stock; re-enter stock settings. |
| Returning to stock | The journal stays in the gap sector unless erased | Press "Disable and erase" before flashing stock. If forgotten: harmless if stock really does not use the sector. |

### 9.5 Recommendation

1. Ship **P0 (RTC) and the backup file** in chunk 1. No flash write, no approval needed for safety; they are listed in 12.2 for transparency.
2. Ship **P1 compiled out**. After the user approves, build with the flag, let the user read the probe line for a few days (including one stock round trip if they are willing), then press "Enable".
3. If the user declines P1: settings and progress survive restarts and updates but not power cuts; the weekly restart and the WiFi and heap restarts stay safe because of P0.

## 10. Architecture and interfaces

### 10.1 Module map and file ownership

Phase A (chunk 0) may edit any file. From phase B on, each file has exactly one owner. "Frozen" means: the owner chunk is finished with it; a later change needs the lead's agreement and goes through the integration pass.

| File | Created by | Owner from phase B | Content |
|---|---|---|---|
| `lib/gamecore/src/gamecore.h` | exists | frozen (chunk 0) | public API (10.2) |
| `lib/gamecore/src/gq_types.h` | 0A | frozen (chunk 0) | ids, enums, `GqSave`, `GqState`, events, query keys (10.3) |
| `lib/gamecore/src/gq_mod.h` | 0A | frozen (chunk 0) | functions the modules call on each other (10.4) |
| `lib/gamecore/src/gc_internal.h` | exists | frozen (chunk 0) | drawing primitives; includes `gq_types.h`, `gq_mod.h`, `assets_gen.h` |
| `lib/gamecore/src/game.cpp` | 0A | frozen (chunk 0) | scene glue: init, tick and render order, event dispatch, save export/import, `gc_query`, `gc_debug` |
| `lib/gamecore/src/save.cpp` | 0A | frozen (chunk 0) | CRC32, validate, sanitize, migrate |
| `lib/gamecore/src/gamecore.cpp` | exists | chunk 2 | API entry points, primitives, palette pipeline, test and info scenes |
| `lib/gamecore/src/ui.cpp` | 0A | chunk 2 | HUD, band, dialogue box, postcard, curtain, cards, `fmt_compact` |
| `lib/gamecore/src/world.cpp` | 0A (from `village.cpp`) | chunk 3 | maps, areas, walk grid, path finding, actors, sprite list, dirty tracking |
| `lib/gamecore/src/hero.cpp` | 0A (from `village.cpp`) | chunk 3 | scheduler and activities |
| `lib/gamecore/src/npc.cpp` | 0A (from `village.cpp`) | chunk 3 | villagers, critters, monsters |
| `lib/gamecore/src/map_data.h` | chunk 3 | chunk 3 | area scripts |
| `lib/gamecore/src/econ.cpp`, `econ_data.h` | 0A | chunk 4 | economy, goals, achievements |
| `lib/gamecore/src/quest.cpp`, `quest_data.h`, `text_data.h` | 0A | chunk 5 | quests, all strings |
| `lib/gamecore/src/calendar.cpp`, `weather.cpp` | 0A | chunk 6 | time, seasons, weather, festivals, events, look |
| `lib/gamecore/src/assets_gen.h` (generated), `assets/src/*.png`, `assets/palette.gpl`, `tools/make_art.py`, `tools/png2tiles.py`, `tools/palette.py`, `tools/font5x7.py`, `tools/expand_sheets.py` | exists / 0A | chunk 7 | art |
| `lib/gamecore/src/village.cpp` | exists | deleted by 0A | |
| `lib/gqsafe/src/netfsm.h`, `netfsm.cpp` | 0B | chunk 1 | WiFi state machine (pure) |
| `lib/gqsafe/src/journal.h`, `journal.cpp`, `gqcrc.h` | chunk 1 | chunk 1 | flash journal (pure) |
| `firmware/*` (all files), `platformio.ini` | exists / 0B | chunk 1 | recovery layer, drivers, entry point |
| `tools/check_bin.py`, `tools/pio_post.py`, `tools/version.py`, `tools/lint_safety.py`, `tools/check_bin_selftest.py`, `.github/workflows/build.yml`, `README.md`, `docs/RECOVERY.md` | exists / 0A / chunk 1 | chunk 1 | gates and user docs |
| `preview/*`, `tools/build_preview.sh` | exists | chunk 2 | browser preview |
| `test/core_smoke.cpp`, `test/test_save.cpp`, `test/test_presence.cpp`, `test/gq_test.h` | exists / 0A | frozen (chunk 0) | shared tests |
| `test/test_net.cpp`, `test/test_journal.cpp` | 0B / chunk 1 | chunk 1 | |
| `test/test_ui.cpp` / `test_world.cpp` / `test_econ.cpp` / `test_quest.cpp` / `test_calendar.cpp` / `test_art.py` | each chunk | chunks 2 / 3 / 4 / 5 / 6 / 7 | one test file per chunk |
| `test/test_soak.cpp` | 0A (skeleton) | integration pass | 30-day whole-game invariants |
| `docs/DESIGN.md` | this document | lead | |

### 10.2 Public API additions (`gamecore.h`, written by 0A)

```c
#define GC_SAVE_BYTES 192

// ---- persistence. The core does no I/O; the caller stores the bytes.
int      gc_save_export(uint8_t* buf, int cap);        // village scene only. Returns GC_SAVE_BYTES, or 0.
int      gc_save_check(const uint8_t* buf, int len);   // 0 = would be accepted; else GC_SAVE_ERR_*. No side effects.
int      gc_save_import(const uint8_t* buf, int len);  // 1 = accepted, sanitized, applied. 0 = rejected, nothing changed.
uint32_t gc_save_serial(void);                         // changes whenever saved state changed and is worth saving
void     gc_reset_game(void);                          // day 1 defaults (same as a first boot)
enum { GC_SAVE_OK = 0, GC_SAVE_ERR_LEN, GC_SAVE_ERR_MAGIC, GC_SAVE_ERR_VERSION, GC_SAVE_ERR_CRC };

// ---- environment
void gc_set_date(int year, int month, int day, int weekday, uint32_t epoch);  // year 0 = unknown; weekday 0 = Sunday
void gc_set_clock_quality(int approximate);     // 1 = restored after a restart, not NTP-synced yet
void gc_set_net(int state);                     // GC_NET_ONLINE 0, GC_NET_OFFLINE 1, GC_NET_AP_ONLY 2
void gc_set_option(int key, int value);         // GC_OPT_CARD_PERIOD (minutes, 0 = off), GC_OPT_NIGHT_QUIET (0/1)

// ---- introspection for tests, the preview and the status page
int32_t gc_query(int key);                      // GQ_Q_* (10.3). Unknown key returns 0.
void    gc_debug(int cmd, int32_t arg);         // GQ_DBG_* (10.3). Unknown command does nothing.
```

Existing functions keep their signatures. `gc_init(GC_SCENE_VILLAGE, seed, swap)` starts a day-1 game; the firmware then calls `gc_save_import` if it has a save.

### 10.3 Shared types (`gq_types.h`, written by 0A; the values below are final)

```c
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
  GQ_Q_ITEM_BASE = 100, // + GQ_ITEM_*  -> count
  GQ_Q_TOOL_BASE = 130, // + GQ_TOOL_*  -> level
  GQ_Q_STAT_BASE = 140, // + GQ_STAT_*  -> value
  GQ_Q_FRIEND_BASE = 160 // + GQ_NPC_*  -> 0..100
};

// ---- gc_debug commands (preview and tests; they go through the same clamps as normal play)
enum { GQ_DBG_ADD_COINS, GQ_DBG_SET_DAY, GQ_DBG_SET_WEATHER, GQ_DBG_ADD_ITEM /*arg=item<<8|n*/,
       GQ_DBG_START_QUEST, GQ_DBG_SHOW_CARD, GQ_DBG_FORCE_ACT, GQ_DBG_BUY_GOAL, GQ_DBG_NEXT_DAY };
```

`GqSave` (192 bytes, little-endian, no padding; `static_assert(sizeof(GqSave) == 192)` and one `static_assert(offsetof(...))` per field):

| Offset | Type | Field | Range after sanitize | Writer |
|---|---|---|---|---|
| 0 | u32 | `magic` = 0x53514747 ("GGQS") | | save |
| 4 | u16 | `version` = 1 | | save |
| 6 | u16 | `size` = 192 | | save |
| 8 | u32 | `crc32` over bytes 12..191 (reflected polynomial 0xEDB88320, init and final XOR 0xFFFFFFFF) | | save |
| 12 | u32 | `seq` (+1 per export) | | save |
| 16 | u32 | `epoch` (unix time at export, 0 unknown) | | game |
| 20 | u32 | `play_min` (minutes simulated, saturating) | | game |
| 24 | u32 | `rng` | 0 becomes 0x1234567 | game |
| 28 | u32 | `earned_total` | | econ |
| 32 | u32 | `spent_total` | | econ |
| 36 | u16 | `day` | | calendar |
| 38 | u16 | `minute` | 0..1439 | calendar |
| 40 | u16 | `coins` | 0..cap | econ |
| 42 | u16 | `stars` | 0..999 | econ |
| 44 | u16 | `projects` (bit mask) | bits 0..9 | econ |
| 46 | u16 | `fund` | 0..99 | econ |
| 48 | u16[12] | `stat` | | econ |
| 72 | u8 | `weather` | < GQ_W_COUNT | calendar |
| 73 | u8 | `weather_hours` | 0..8 | calendar |
| 74 | u8 | `festival_flags` | | calendar |
| 75 | u8 | `event_today` | 0..12 | calendar |
| 76 | u8[24] | `inv` | each 0..item cap | econ |
| 100 | u8 | `till_left` | 0..160 | econ |
| 101 | u8 | `errands_today` | 0..3 | quest |
| 102 | u8 | `meals_today` | 0..3 | econ |
| 103 | u8 | `gifts_today` | 0..1 | econ |
| 104 | u8[8] | `tool` | each 0..3 (purse 0..3) | econ |
| 112 | u8 | `goal` | 0..22 | econ |
| 113 | u8 | `chapter` | 0..6 | quest |
| 114 | u8 | `q_active` | < quest count, or 0xFF | quest |
| 115 | u8 | `q_step` | < steps of that quest | quest |
| 116 | u8 | `q_count` | 0..99 | quest |
| 117 | u8 | `hero_hearts` | 1..6 | world |
| 118 | u8 | `hero_area` | always loaded as VILLAGE in version 1 | world |
| 119 | u8 | `year_legends` | 0..255 | quest |
| 120 | u8[8] | `q_done` (bits) | | quest |
| 128 | u8[12] | `friend` | each 0..100 | econ |
| 140 | u8[8] | `seen` (bits) | | econ |
| 148 | u8[4] | `ach` (bits) | | econ |
| 152 | u16 | `chat_today` (bit per NPC) | | econ |
| 154 | u8[38] | `reserved`, written as 0, ignored on load | | |

A restored game always resumes with the hero standing at his home door in the village, with no active activity. Mid-activity state is deliberately not saved: fewer states to validate.

```c
typedef struct {
  GqSave   s;             // the saved part
  uint32_t serial;        // gc_save_serial()
  uint8_t  presence;      // GQ_Q_HERO_PRESENCE
  uint8_t  net, clock_approx, card_period, night_quiet;
  int16_t  year; uint8_t month, mday, wday;
} GqState;
extern GqState G;

typedef struct { uint8_t kind, area, tx, ty, npc, item, count, activity; } GqNeed;
enum { GQ_NEED_NONE, GQ_NEED_TALK, GQ_NEED_ITEM, GQ_NEED_GOTO, GQ_NEED_SLAY, GQ_NEED_DO, GQ_NEED_WAIT };
typedef struct { uint8_t index, is_project, mat_item, mat_need, mat2_item, mat2_need, affordable, name_txt;
                 uint16_t price; } GqGoal;
```

Rule: a module writes only the `GqSave` fields listed with its name. Everyone may read everything. To change another module's field, call that module (for example a quest reward calls `econ_earn`).

### 10.4 Functions between modules (`gq_mod.h`, written by 0A with working stubs)

```c
// ---- game.cpp (chunk 0)
void gq_emit(int ev, int a, int b);              // never re-entrant: events raised inside a handler are queued (8 deep) and
                                                 // delivered after the current one; a full queue drops the event and counts it
void gq_touch(void);                             // G.serial++ : call after any change worth saving
uint32_t gq_crc32(const uint8_t* p, int n);

// ---- save.cpp (chunk 0)
int  gq_save_validate(const uint8_t* buf, int len);      // GC_SAVE_*
void gq_save_sanitize(GqSave* s);                        // clamps every field to the ranges in 10.3
void gq_save_defaults(GqSave* s);                        // day-1 state

// ---- calendar.cpp, weather.cpp (chunk 6)
void cal_init(void);                             // from G.s
void cal_tick(uint32_t dt);                      // minute, day, weather; emits HOUR, NEW_DAY, WEATHER, FESTIVAL
void cal_on_event(int ev, int a, int b);
int  cal_minute(void);  int cal_is_night(void);  // night = 22:00..05:59
int  cal_season(void);  int cal_day_of_season(void);  int cal_year(void);
int  cal_weather(void); int cal_festival(void);  /* -1 or 0..3, only between 16:00 and 22:00 */  int cal_event(void);
void cal_look(GqLook* out);                      // palette inputs for this minute
void weather_tick(uint32_t dt);                  // marks strips dirty within the budget of 6.1
void weather_render(int strip, uint16_t* out);
void weather_set_fps(int fps_x10);               // governor

// ---- econ.cpp (chunk 4)
void econ_init(void);
void econ_on_event(int ev, int a, int b);        // statistics, achievements, daily reset on NEW_DAY
int  econ_coin_cap(void);
void econ_earn(int coins, int source);           // cap, overflow to fund and stars
int  econ_pay(int coins);                        // 1 = paid; 0 = not enough (nothing changes)
int  econ_item(int item);                        // count
int  econ_item_cap(int item);
int  econ_add_item(int item, int n);             // returns how many fitted
int  econ_take_item(int item, int n);            // 1 = taken; 0 = not enough (nothing changes)
int  econ_sell_all(void);                        // at the shop; returns coins earned; emits SOLD per item
int  econ_goal(GqGoal* out);                     // 0 = all goals bought (village fund mode)
int  econ_buy_goal(void);                        // 1 = bought (coins and materials taken, BOUGHT or BUILT emitted)
int  econ_tool(int tool);                        // level
int  econ_project_built(int project);
int  econ_activity_bias(int act);                // -9..+3, added to the scheduler weight (-9 = pointless now)
int  econ_focus_item(void);                      // item to show on the HUD
void econ_friend_add(int npc, int delta);
int  econ_friend(int npc);
void econ_collect(int collectible);              // sets seen bit, emits NEW_COLLECTIBLE once
int  econ_meal_price(void);                      // 0 = already ate enough today

// ---- quest.cpp (chunk 5)
void quest_init(void);
void quest_on_event(int ev, int a, int b);
int  quest_need(GqNeed* out);                    // kind != NONE when the hero can make progress now
int  quest_talk(int npc, char* buf, int cap);    // hero talks to npc: returns 1 and a line if the quest system has
                                                 // something to say (offer, progress, thanks); else 0
void quest_ambient_line(int npc, char* buf, int cap);   // ordinary chat line (season, weather, friendship aware)
void quest_reply_line(char* buf, int cap);               // hero's reply
int  quest_band(char* buf, int cap, int* num, int* den); // 1 = quest text for the band, with progress
int  quest_title(char* buf, int cap);
const char* gq_text(int id, char* buf, int cap); // copies string id from flash; returns buf
const char* gq_npc_name(int npc);                // RAM pointer to a static 8-byte buffer

// ---- world.cpp, hero.cpp, npc.cpp (chunk 3)
void world_init(void);                           // builds the village from G.s (projects, season)
void world_tick(uint32_t dt);
void world_render(int strip, uint16_t* out);
void world_on_event(int ev, int a, int b);       // BUILT -> place project tiles; NEW_DAY -> seasonal tiles
int  world_area(void);
int  hero_activity(void);                        // GQ_ACT_*
int  hero_away_progress(int* place_txt);         // chunk 0 stopgap: 0..255 while off screen, -1 otherwise
int  world_actor_count(void);

// ---- ui.cpp (chunk 2)
void ui_init(void);
void ui_tick(uint32_t dt);
void ui_on_event(int ev, int a, int b);          // banners
void ui_render(int strip, uint16_t* out);
void ui_say(int who, const char* text, int32_t hold_ms);   // dialogue box; text is copied (40 bytes)
int  ui_dialogue_done(void);
void ui_curtain(int close);                      // 1 = start closing, 0 = start opening
int  ui_curtain_closed(void);                    // fully closed
int  ui_curtain_covers(int strip);
int  ui_card_covers(int strip);
int  ui_busy(void);                              // dialogue, curtain or card on screen
void ui_status_changed(void);                    // network status lines changed
void fmt_compact(char* out, uint32_t v);         // 3.7
void fmt_uint(char* out, uint32_t v);            // plain decimal, at most 10 digits

// ---- gamecore.cpp (chunk 2)
void gfx_set_look(const GqLook* look);
extern uint16_t g_act[16];
```

`game.cpp` order per tick: `cal_tick`, `weather_tick`, `world_tick` (which runs hero and NPCs), `ui_tick`, then delivery of queued events. Order per event: `econ_on_event`, `quest_on_event`, `cal_on_event`, `world_on_event`, `ui_on_event`.

### 10.5 Art ids (final; 0A creates the sheets with placeholders, chunk 7 paints them)

Tiles (`tiles.png`, 16 x 6 = 96 cells). 0..28 keep today's meaning.

| Ids | Names |
|---|---|
| 29..31 | `T_VOID` (black), `T_ICE`, `T_SNOWMAN` |
| 32..40 | `T_BOARD`, `T_WELL`, `T_LANTERN_POST`, `T_LANTERN_LIT`, `T_BRIDGE_H`, `T_BRIDGE_V`, `T_CROP_0`, `T_CROP_1`, `T_CROP_2` |
| 41..48 | `T_STALL_L`, `T_STALL_R`, `T_FLOWERBED`, `T_BELL_BASE`, `T_BELL_TOP`, `T_BELL_TOP2`, `T_FOUNTAIN`, `T_FOUNTAIN2` |
| 49..55 | `T_STAGE_L`, `T_STAGE_R`, `T_FEST_TABLE`, `T_FEST_LIGHTS`, `T_BUSH`, `T_MUSHROOM`, `T_HERB` |
| 56..63 | `T_PINE`, `T_SAND`, `T_SAND_SHELL`, `T_WAVE`, `T_WAVE2`, `T_REEDS`, `T_BOAT`, `T_CHEST` |
| 64..70 | `T_CAVE_FLOOR`, `T_CAVE_WALL`, `T_CAVE_WALL_TOP`, `T_ORE_ROCK`, `T_GEM_ROCK`, `T_STALAG`, `T_CAVE_EXIT` |
| 71..79 | `T_RUIN_FLOOR`, `T_RUIN_WALL`, `T_RUIN_PILLAR`, `T_RUIN_DOOR`, `T_ALTAR`, `T_CHEST_OPEN`, `T_TORCH`, `T_TORCH2`, `T_STAIRS` |
| 80..95 | `T_FLOOR_WOOD`, `T_WALL_IN`, `T_WALL_IN_WINDOW`, `T_DOOR_MAT`, `T_BED_TOP`, `T_BED_BOT`, `T_TABLE`, `T_STOOL`, `T_COUNTER`, `T_SHELF`, `T_BARREL`, `T_HEARTH`, `T_HEARTH2`, `T_RUG`, `T_PLANT`, `T_WINDOW_MOON` |

Characters (`chars.png`, 8 x 8 = 64 cells; replaces `hero.png`; C name `GC_CHARS`):

| Ids | Content |
|---|---|
| 0..2, 3..5 | hero facing down (stand, step A, step B); facing up |
| 6, 7 | hero in bed; hero sitting |
| 8..10 | hero facing left (right = flipped) |
| 11, 12, 15 | sword swing left, down, up |
| 13, 14 | hero hurt; hero cheering (holds an item up) |
| 16..39 | head overlays: `16 + head*3 + dir` (dir 0 down, 1 up, 2 left); heads 0 Mira, 1 Bram, 2 Pip, 3 Odda, 4 Finn, 5 Tess, 6 Luma, 7 Wren. A villager = hero body through a palette remap + head overlay on top. |
| 40..47 | slime A/B, bat A/B, crab A/B, ghost A/B |
| 48..55 | chicken A/B, frog A/B, butterfly A/B, firefly, bird |
| 56..59 | cat down, cat side A, cat side B, cat asleep |
| 60..62 | boss A, boss B, boss hurt |
| 63 | spare |

Items and icons (`items.png`, 8 x 6 = 48 cells). 0..10 keep today's meaning.

| Ids | Names |
|---|---|
| 11..15 | `I_SWORD_UP`, `I_SWORD_DOWN`, `I_PICK_UP`, `I_PICK_DOWN`, `I_HAMMER` |
| 16..31 | `I_STONE`, `I_MUSHROOM`, `I_BERRY`, `I_FLOWER`, `I_SHELL`, `I_ORE`, `I_GEM`, `I_CROP`, `I_SEED`, `I_HERB`, `I_GEL`, `I_MAP`, `I_KEY`, `I_SHARD`, `I_PARCEL`, `I_CHARM` |
| 32..39 | `I_STAR`, `I_HEART_EMPTY`, `I_FACE` (hero face), `I_EXCL`, `I_SCROLL`, `I_NOTE`, `I_POOF`, `I_POOF2` |
| 40..47 | `I_W_CLEAR`, `I_W_CLOUDY`, `I_W_RAIN`, `I_W_STORM`, `I_W_FOG`, `I_W_SNOW`, `I_W_WINDY`, `I_MOON` |

Item id to icon: `ITEM_ICON[24]` in `econ_data.h` (`GQ_ITEM_WOOD -> I_WOOD`, `GQ_ITEM_FISH -> I_FISH`, `GQ_ITEM_ROPE -> I_KEY`, `GQ_ITEM_OIL -> I_SEED`, the rest by name).

## 11. Work breakdown

### 11.1 Order

```
Phase A   chunk 0A (core)  ||  chunk 0B (firmware WiFi)        -> release R0 to the clock (bug fixes)
Phase B   chunks 1, 2, 3, 4, 5, 6, 7 in parallel                 (chunk 1 alone -> release R1: recovery hardening)
Phase C   integration pass (lead + chunk 3 owner)              -> release R2 (content)
```

- 0A and 0B touch disjoint files and can run at the same time. Everything else waits for both.
- In phase B every chunk compiles and passes its tests against the stubs of the others. Suggested merge order: 7, 6, 4, 5, 2, 3; chunk 1 is independent of the game chunks and should be flashed on its own (R1) before any content, so that the content release runs on the hardened recovery layer.
- Three flashes in total (R0, R1, R2). Each one goes through the pre-flash checklist (13.4).

Common rules for all chunks: section 8.10; C++ as used today (no exceptions, no RTTI, no STL in the core); every new table is `static const ... GC_ROM_ATTR` and read through `GC_RD8`/`GC_RD16`; every test file is `test/test_<name>.cpp` with its own `main()` returning 0 on success, built by CI as `g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_<name>.cpp lib/gamecore/src/*.cpp lib/gqsafe/src/*.cpp`. Tests may include `gq_mod.h` and `gq_types.h` to call modules directly. `test/gq_test.h` (0A) provides `sim_minutes(n)`, `sim_days(n)` (250 ms ticks, wall clock fed through `gc_set_clock` every simulated minute, renders every 97th frame with the overrun guard), `frame_hash()` and `CHECK(cond)`.

### 11.2 Chunk 0: foundations and the three fixes

#### Package 0A (core)

**Goal.** Split `village.cpp` into the modules of 10.1 with working stubs; define every shared id and the save format; fix the economy and the vanishing hero with the stopgaps; make CI run any test file.

**Files.** Everything under `lib/gamecore/`, `test/`, `assets/`, `tools/png2tiles.py`, `tools/expand_sheets.py` (new), `preview/wasm_shim.cpp` (new exports only), `.github/workflows/build.yml` (test loop only).

**Budget.** 6 KB code, 1 KB text, 300 B RAM (package 0B: 3 KB, 200 B; together the 10 KB and 500 B of section 2.3).

**Steps.**

1. *Reference hash.* Before changing anything, add `test/gq_test.h` and record `frame_hash()` of the village for seeds 1..3 after 10 simulated minutes (print them; keep them in the test as constants for step 3).
2. *Sheets.* `tools/expand_sheets.py`: pad `tiles.png` to 256x96 and `items.png` to 128x96; create `chars.png` 128x128 with today's hero cells moved to ids 0..5 and 8..10 (10.5); fill unused cells with the placeholder (2x2 checker of palette entries 1 and 14). Paint two real cells now, because R0 shows them: `I_STAR` (32) and `I_FACE` (34). Update `SHEETS` in `png2tiles.py` (`CHARS` replaces `HERO`), regenerate `assets_gen.h`. Hero cell lookup becomes `{0, 3, 8}[row] + frame`.
3. *Pure split.* Move code from `village.cpp` into `world.cpp`, `hero.cpp`, `npc.cpp`, `ui.cpp` (HUD, dialogue box, status band), `calendar.cpp` (minute, tint keyframes), `game.cpp` (`village_init/tick/render` in the order of 6.3 and 10.4). No behaviour change: the hashes of step 1 must still match. Delete `village.cpp`. Commit.
4. *Types and state.* Write `gq_types.h`, `gq_mod.h` exactly as in 10.3 and 10.4. Move `g_wood`, `g_gold`, `g_fish`, `g_minute` into `G.s`. Implement `gq_emit` with the 8-entry queue. Change `blit_sprite` to take the palette (`const uint16_t* pal`) as its last parameter and add `g_act` (for now a copy of `g_pal`) and a first `gfx_set_look` that calls `set_tint`, so chunk 2 can change the palette code without touching callers.
5. *Save.* `save.cpp`: `gq_crc32` (bitwise, no table), `gq_save_defaults`, `gq_save_validate`, `gq_save_sanitize` (one clamp per row of the table in 10.3; unknown bits in masks are cleared; `hero_area` forced to VILLAGE). `game.cpp`: `gc_save_export` (fills header, `seq + 1`, `rng`, CRC), `gc_save_check`, `gc_save_import` (validate into a local copy, sanitize, then copy to `G.s` and call every `*_init()`), `gc_reset_game`, `gc_save_serial`.
6. *Economy stopgap* (`econ.cpp`, real code that chunk 4 extends): `econ_coin_cap`, `econ_earn`, `econ_pay`, item functions for WOOD and FISH with caps, `econ_sell_all` (till 60, prices 1 and 3, wood reserve 3), `econ_meal_price` (4, then 5, then 0), a 3-entry goal table (Purse II 60, Purse III 200, Purse IV 450), `econ_goal`, `econ_buy_goal`, daily reset on `GQ_EV_NEW_DAY`. `quest.cpp` stub: on `GQ_EV_QUEST_DONE` with `a == 0xFF`, if `errands_today < 3` then `errands_today++` and `econ_earn(6 + rnd % 9, GQ_SRC_QUEST)`. `hero.cpp`: shop uses `econ_sell_all`; tavern uses `econ_meal_price` and `econ_pay`; the quest activity has weight 0 when `errands_today >= 3`; new activity `GQ_ACT_BUY` (weight 6 whenever `econ_goal` reports `affordable`): walk to the shop door, 6 s, `econ_buy_goal()`, line "NEW PURSE!|HOLDS 250 COINS". `calendar.cpp`: `day` and the natural-midnight rule of 5.1, emitting `GQ_EV_NEW_DAY` and `GQ_EV_HOUR`.
7. *Presence stopgap.* `hero.cpp` sets `G.presence` every tick: 1 sprite visible; 2 door marker (emote at the door alternating every 600 ms between `I_FACE` and `I_COIN` (shop), `I_HEART` (tavern), `I_ZZZ` (home)); 3 postcard. Tavern stay becomes 8..12 s. `hero_away_progress()` returns 0..255 during quest phase 3. `ui.cpp` draws the postcard on strips 12..14 in the dialogue frame: line 1 "QUEST: <PLACE>", a dotted track from x=24 to x=200 with the walking hero cell at `x = 24 + 176 * tri(progress)` (out and back), the place's icon at the right end, line 2 by thirds "ON THE ROAD", "FIGHTING SLIMES!", "HEADING HOME". The return dialogue becomes "BACK FROM THE|<PLACE>! +N COINS" or "... A FINE WALK." when unpaid.
8. *HUD.* `fmt_compact`, `fmt_uint`. HUD: coin icon and coins (at most 3 digits), wood icon and count, and when `stars > 0` the star icon at x=100 with up to 3 digits; clock unchanged.
9. *API.* `gc_query` for every key (keys whose module is still a stub return 0), `gc_debug` (`ADD_COINS`, `NEXT_DAY`, `SET_DAY`, `FORCE_ACT`), `gc_set_date`, `gc_set_net`, `gc_set_clock_quality`, `gc_set_option` (stored only). Export `query`, `debug`, `save_export`, `save_import` from the wasm shim.
10. *CI.* Replace the single smoke step with a loop over `test/*.cpp` (command in 11.1; `lib/gqsafe/src/*.cpp` may not exist yet: use a shell glob that tolerates that). Keep `core_smoke.cpp`.

**Host tests and acceptance.**

- `core_smoke.cpp` passes unchanged in spirit (all scenes, 5 seeds, overrun guard).
- Step 3 hash equality (kept as a test only until step 4; then the constants are removed).
- `test_save.cpp`: export then import gives identical `gc_query` values for all keys; each of the 1,536 single-bit flips of an exported save is rejected; wrong length, magic, version 2 rejected with the right code and `G` unchanged (compare a second export); 10,000 random 192-byte buffers rejected; 2,000 buffers of random bytes with a *correct* header and CRC are accepted, and after each the game runs 10 simulated minutes with rendering under ASan with no finding and every `gc_query` value inside its range.
- `test_presence.cpp` (3 simulated days, seeds 1..5, sampled every simulated second): `GQ_Q_HERO_PRESENCE != 0` in 100% of samples; `GQ_Q_COINS <= GQ_Q_COIN_CAP` always; earned per day at most `60 + 3*14`; after day 1, wood is at least 3 in at least 80% of samples in which the hero has chopped that day; coins after 3 days at most 250.
- `test_soak.cpp` skeleton: same driver, 3 days, prints the daily ledger (earned, spent, coins, stars); the integration pass extends it to 30 days.
- `fmt_compact` vectors of 3.7.
- `.bin` growth at most 10 KB (CI size report), preview builds.

**Recovery requirements.** No firmware file is touched. `gc_save_import` must be total: no input may crash it or leave `G` half-written.

#### Package 0B (firmware WiFi)

**Goal.** The clock rejoins the home network by itself, the hotspot stays usable, nothing blocks.

**Files.** `lib/gqsafe/src/netfsm.h`, `netfsm.cpp` (new), `firmware/netwatch.h`, `netwatch.cpp` (new), `firmware/safety.cpp`, `firmware/safety.h`, `firmware/main.cpp`, `test/test_net.cpp`.

**Adds.** The interface of 7.2, and:

```c
// firmware/netwatch.h
namespace netwatch {
  void begin(bool safeMode);          // reads stored credentials into RAM, registers WiFi event handlers
  void loop();                        // once per second: build NetIn, step, carry out the actions
  void setCredentials(const char* ssid, const char* pass);   // from the WiFi form; calls netfsm_kick
  uint8_t stage();  uint32_t attempts();  uint32_t downSeconds();  uint8_t lastReason();
  void noteNtp();  void noteHttp();
}
// firmware/safety.h additions
bool golden();                        // first 10 minutes of uptime (latched)
```

**Budget.** 3 KB code, 200 B RAM.

**Steps.**

1. `netfsm.cpp` exactly as the table in 7.2. All time arithmetic is `uint32_t` subtraction. Jitter from an xorshift seeded in `netfsm_init`. No Arduino include.
2. `netwatch.cpp`: `begin()` reads `wifi_station_get_config_default` into `s_ssid`/`s_pass`; `WiFi.setAutoReconnect(false)`; `WiFi.setSleepMode(WIFI_NONE_SLEEP)` (only if approved, 12.2 A3); handlers `onStationModeDisconnected` (store reason, set a flag) and `onStationModeGotIP` (set a flag): handlers only set flags, all work happens in `loop()`. Actions: `NA_BEGIN` -> `WiFi.begin(s_ssid, s_pass)`; `NA_STA_STOP` -> `wifi_station_disconnect()` (the SDK call, which keeps the configuration); `NA_STA_CYCLE` -> `WiFi.enableSTA(false)` now and `WiFi.enableSTA(true)` on the next step; `NA_AP_ON/OFF` -> the existing `startAP()`/`stopAP()` (exported from `safety.cpp`); `NA_MDNS_RESTART` -> `MDNS.end(); MDNS.begin("geekquest"); MDNS.addService(...)`; `NA_SNTP_KICK` -> `configTime(...)` again; `NA_RESTART` -> ignored in 0B (logged only; chunk 1 wires it).
3. `safety.cpp`: remove `wifiPolicy()` and `WiFi.setAutoReconnect(true)`; call `netwatch::begin()` after `startAP()` and `netwatch::loop()` from `safety::loop()` once per second; `handleWifiSave` also calls `netwatch::setCredentials`; add `golden()`; call `netwatch::noteHttp()` at the top of every handler and `noteNtp()` in the time callback; show stage, attempts, seconds down and the last reason on the status page and in `/info`. Replace the two absolute `millis()` comparisons with latched flags.
4. `main.cpp`: `gc_set_net(...)` in `updateStatus()`.
5. Check against the Arduino core 3.1.2 sources (in the CI cache, or online) that `WiFi.begin(ssid, pass)` with `persistent(false)` does not write flash and that `wifi_station_disconnect()` keeps the current configuration. The design does not depend on either (credentials are always passed explicitly), but note the result in the hand-off.

**Host tests (`test_net.cpp`) and acceptance.** A driver steps the machine once per simulated second.

| Id | Scenario | Must hold |
|---|---|---|
| N1 | Link up for 60 days, starting at `now = 0xFFF00000` (wraps after 17 minutes) | No action except one `NA_AP_OFF` after the golden window; no `NA_RESTART` |
| N2 | 2-minute outage | `NA_BEGIN` at 3, 18, 48, 108 s (each within +10%); `NA_STA_STOP` 20 s after each failed attempt; `NA_AP_ON` at 60 s; on recovery `NA_MDNS_RESTART`, and `NA_AP_OFF` 60 s later |
| N3 | 3-day outage, restarts simulated by re-initialising with `restarts_done + 1` | `NA_BEGIN` count at most `3 d / 300 s + 10`; first `NA_STA_CYCLE` at 10 min, then every 30 min; `NA_RESTART` after 6 h, then 12 h, then 24 h of the respective boot: at most 4 in total |
| N4 | Random inputs during the first 10 min | Never `NA_AP_OFF`, `NA_STA_CYCLE`, `NA_RESTART` |
| N5 | AP client connected throughout | Never `NA_AP_OFF`, `NA_STA_CYCLE`, `NA_RESTART`; `NA_BEGIN` at least 300 s apart |
| N6 | `updating` set | Only `NA_AP_ON` |
| N7 | Safe mode | Only `NA_AP_ON` and `NA_BEGIN` at least 300 s apart |
| N8 | Link up, last NTP and last request 7 h ago | `NA_STA_STOP`, then `NA_BEGIN` within 3 s, `NA_SNTP_KICK`; not again for 6 h |
| N9 | No credentials | Only `NA_AP_ON` |
| N10 | 1,000,000 random steps | `NA_BEGIN` never less than 3 s apart; at most one `NA_RESTART` per lifetime; no undefined bits |
| N11 | `netfsm_kick` | `NA_BEGIN` on the next step |

Firmware builds for both environments; `check_bin.py` passes; `.bin` growth at most 4 KB.

**Recovery requirements.** `safety::begin()` order unchanged. The AP start in `begin()` and the web server routes are not modified. This package changes proven recovery code: it needs the `RECOVERY-CHANGE` line, a careful read by a second engineer, and the full hardware checklist (13.5) on R0 before anything else is built on it.

### 11.3 Chunk 1: recovery, persistence, network hardening

**Goal.** Everything in sections 7.4, 8 and 9. This is the release (R1) the content will run on.

**Depends on.** Chunk 0 (both packages). Uses only the public core API of 10.2.

**Files.** All of `firmware/`, `lib/gqsafe/`, `platformio.ini`, `tools/check_bin.py`, `tools/pio_post.py`, `tools/lint_safety.py` (new), `tools/check_bin_selftest.py` (new), `.github/workflows/build.yml`, `README.md`, `docs/RECOVERY.md` (new, user-facing: sections 8.9, 13.4 and 13.5 in plain words), `test/test_journal.cpp`, `test/test_net.cpp`.

**Adds.**

```c
// firmware/safety.h
enum RestartReason : uint8_t { RR_NONE, RR_USER, RR_UPDATE, RR_WIFI, RR_HEAP, RR_MAINT, RR_SAFEMODE, RR_FACTORY };
void requestRestart(RestartReason why);      // carried out from safety::loop() as soon as it is allowed
void onBeforeRestart(void (*fn)());          // main.cpp registers persist::saveRtc
bool autoRestartAllowed();                   // uptime >= 30 min, not golden, no AP client, not updating, not safe mode
bool saveQuarantined();
void tickMarker(uint8_t stage);              // writes only the stage word (cheap; used for 10 and 11 every frame)

// firmware/persist.h
namespace persist {
  struct GqSettings;                          // 48 bytes, section 9.1
  void begin(bool safeMode);                  // loads settings: RTC, else flash, else defaults. Never writes.
  const GqSettings& settings();
  void setSettings(const GqSettings& s);      // applies, marks dirty
  bool loadGame(uint8_t out[192]);            // newest valid candidate; false if none, quarantined or safe mode
  void saveRtc();                             // settings + gc_save_export() -> RTC blocks 32..91
  void loop();                                // every 60 s: saveRtc() if gc_save_serial() changed; flash policy 9.4
  bool backupExport(uint8_t out[240]);
  bool backupImport(const uint8_t* in, size_t n);   // validates both records; false changes nothing
  void factoryResetGame();  void resetSettings();
#if GQ_FLASH_SAVE
  enum Probe : uint8_t { PROBE_BLANK, PROBE_JOURNAL, PROBE_FOREIGN, PROBE_LAYOUT_MISMATCH };
  Probe probe();  bool flashEnable();  bool flashDisableAndErase();  bool flashSaveNow();
#endif
}

// lib/gqsafe/src/journal.h (pure C++, no Arduino)
typedef struct {
  int (*read)(void* ctx, uint32_t off, uint8_t* buf, uint32_t n);          // off + n <= 4096
  int (*write)(void* ctx, uint32_t off, const uint8_t* buf, uint32_t n);   // may only clear bits
  int (*erase)(void* ctx);                                                 // whole sector to 0xFF
  void* ctx;
} JournalHal;
enum { JR_OK, JR_BLANK, JR_FOREIGN, JR_IO, JR_NO_VALID };
int journal_probe(const JournalHal* h, uint32_t* slots_used, uint32_t* erase_count);
int journal_format(const JournalHal* h);                                   // only when the sector is blank
int journal_load(const JournalHal* h, uint8_t payload[240], uint32_t* seq);
int journal_append(const JournalHal* h, const uint8_t payload[240], uint32_t seq);   // erases when full
int journal_erase(const JournalHal* h);
```

**Budget.** 10 KB code, 3 KB page text, 700 B RAM. With `GQ_FLASH_SAVE=0` about 2 KB less.

**Steps.**

1. *Boot guard.* `RtcState2` (8.3), `slow` counter, quarantine flag, restart reasons, `requestRestart` and its gate, `tickMarker`. Keep the first five RTC words byte-compatible.
2. *Restart wiring.* `NA_RESTART` -> `requestRestart(RR_WIFI)` with the `wifiRestarts` and `everConnected` RTC fields feeding `NetIn`; network event log in RTC blocks 112..127.
3. *Heap watchdog and maintenance restart* (8.6, 8.7).
4. *Pages.* Streamed update page and status page, low-memory page (8.5), new boxes (7.4), controls (8.8), `/backup` GET and POST, time zone validation (7.3). Every POST handler that changes state redirects to `/` (303) as today.
5. *Upload checks.* The page computes CRC32 of the file in JavaScript (a 20-line table-less routine; `crypto.subtle` is not available on plain HTTP) and posts to `/update?crc=<hex>&size=<n>&force=<0|1>`. The handler computes the CRC over the received bytes and scans the stream for `GQ-OTA-PAGE-v1` with a 14-byte matcher that works across chunk borders. Before `Update.end(true)`: size and CRC must match when given; the marker must have been seen, or the size is exactly 488,304, or `force=1`. Otherwise `Update.end(false)` and an error text that says what to do. The page shows the file's CRC32 so the user can report the stock image's value once; it is then added as `STOCK_CRC32` and the stock rule becomes size and CRC. (12.2 A4.)
6. *Persistence P0.* `persist.cpp` RTC part, load order, `main.cpp`: after `gc_init`, `if (persist::loadGame(buf)) gc_save_import(buf, 192)`; apply settings (brightness, depth, time zone) in `begin()`; `gc_set_date`, `gc_set_clock_quality`; approximate clock restore from the saved epoch; `tickMarker(10)` before `gc_tick`, `tickMarker(11)` before rendering, `tickMarker(9)` after; `loop_max_ms`.
7. *Journal (pure) and tests*, then, behind `GQ_FLASH_SAVE`, the HAL in `persist.cpp` (`ESP.flashRead`, `ESP.flashWrite`, `ESP.flashEraseSector` with the constant sector; the boot-time symbol checks; the write policy of 9.4).
8. *Gates.* `lint_safety.py`, `check_bin.py` additions and its self-test, CI steps, RAM baseline (read the `.data + .bss` size of the build before chunk 0 from the CI log of that commit and store it as `RAM_BASELINE`), recovery-change guard (13.3).
9. *Docs.* `README.md` recovery section updated; `docs/RECOVERY.md`.

**Host tests and acceptance.**

- `test_journal.cpp` with a simulated NOR sector (writes AND into the array; an injected power cut stops a write after k bytes or leaves an erase half done with random bytes): for 10,000 random sequences of append, cut and reboot, `journal_load` returns the last payload whose append completed, or, if the cut hit the following append or erase, either that one or nothing (`JR_NO_VALID` only after a cut erase); never a payload that was not appended; never an access outside 0..4095; erase count at most `appends / 15 + cuts + 1`. A sector pre-filled with foreign data gives `JR_FOREIGN` from `journal_probe`, and `journal_append` refuses.
- `test_net.cpp` extended: restart counts with the RTC fields.
- `check_bin_selftest.py`: synthetic images for each rule (good, 501 KB, wrong magic, wrong flash size nibble, missing each required string, name without `SDP`) give the expected verdicts.
- `lint_safety.py` passes on the tree and fails on a fixture for each rule (fixtures under `tools/lint_fixtures/`).
- Both firmware environments build with `GQ_FLASH_SAVE=0`; a third CI job builds `quest` with `GQ_FLASH_SAVE=1` and runs the gates but is not published until A1 is approved.
- Hardware acceptance: the R1 checklist in 13.5.

**Recovery requirements.** This chunk *is* the recovery layer. Rules: every new page works with 6 KB of free heap; nothing in safe mode writes anywhere but RTC; no code path can erase or write flash without `GQ_FLASH_SAVE`, an enabled journal, a passed layout check and being outside the golden window; the routes `/`, `/update`, `/update_ota`, `/reboot`, `/safemode`, `/wifi`, `/info` keep their URLs and methods.

### 11.4 Chunk 2: rendering and UI

**Goal.** Palette pipeline, final HUD and band, dialogue box, curtain, cards, the preview's debug panel.

**Depends on.** Chunk 0. Reads other modules through `gq_mod.h` (stubs are enough).

**Files.** `lib/gamecore/src/gamecore.cpp`, `ui.cpp`, `preview/*`, `tools/build_preview.sh`, `test/test_ui.cpp`.

**Adds.** Full implementations of the `ui_*`, `fmt_*`, `gfx_set_look` functions of 10.4; `g_act`; `blit_sprite` gets the palette as a parameter (`const uint16_t* pal`; signature change is inside `gc_internal.h`, which chunk 0 already declares in the new form).

**Budget.** 10 KB code, 1 KB text, 500 B RAM.

**Steps.** (1) Palette pipeline of 6.2 with `SEASON_REMAP[4][16]`; with season 0, no wash and tint 256 the output must equal today's `set_tint(256,256,256)` bit for bit. (2) HUD of 6.6. (3) Band with the priority list of 4.4 item 3; text from `quest_band`, else from `econ_goal`, else empty; progress line. (4) Curtain of 6.5. (5) Cards of 6.7 with the scheduler (`card_period`, hourly clock card, never while `ui_busy` for other reasons). (6) Achievement, collectible and star banners from `ui_on_event`. (7) Governor input: forward `gc_set_fps` to `weather_set_fps`. (8) Preview panel of 6.8.

**Host tests (`test_ui.cpp`) and acceptance.** `fmt_compact` vectors; palette identity; every card rendered with extreme state (coins 999, stars 999, all items 99, `earned_total` 4,294,967,295, longest strings) with the overrun guard and no text outside its panel (check by rendering to the strip and scanning for the text colour outside the panel rectangle); curtain fully closed between 280 and 400 ms after `ui_curtain(1)`; steady-state dirty budget: 10 simulated minutes in the village with clear weather give at most 5 dirty strips per frame on average and at most 1 full redraw per 2 s; cards open at most once per period and never during a dialogue.

**Recovery requirements.** The info scene (`GC_SCENE_INFO`, safe mode) and the test scene keep working and are covered by `core_smoke.cpp`. No dependence on game state in the info scene.

### 11.5 Chunk 3: world and hero life

**Goal.** Areas, interiors, the scheduler, 16 activities, villagers with schedules, critters, monsters and acted-out fights, project tiles, the night camera. Removes the chunk 0 postcard.

**Depends on.** Chunk 0. Calls `econ_*`, `quest_*`, `cal_*`, `ui_*` through `gq_mod.h`; must behave sensibly against the stubs (no quests, 3 goals).

**Files.** `lib/gamecore/src/world.cpp`, `hero.cpp`, `npc.cpp`, `map_data.h` (new), `test/test_world.cpp`.

**Data.**

```c
typedef struct { uint8_t op, xy, wh, tile, arg; } MapOp;      // 5 bytes; xy = x<<4|y, wh = w<<4|h (0 means 16)
enum { MO_END, MO_GROUND, MO_OBJ, MO_SCATTER /*arg = count, seeded by area id*/, MO_HOUSE /*tile = roof*/,
       MO_SPOT /*tile = spot id*/, MO_EXIT /*tile = target area, arg = target spot*/,
       MO_IF_PROJECT /*arg = project: next op only if built*/, MO_IF_SEASON /*arg = season*/ };
typedef struct { uint8_t home_xy, work_xy, work_from_h, work_to_h, area, head, remap; } NpcDef;   // 7 bytes
```

Spots are named tiles (`SPOT_DOOR`, `SPOT_COUNTER`, `SPOT_BED`, `SPOT_TABLE`, `SPOT_DOCK`, `SPOT_BOARD`, `SPOT_SITE`, `SPOT_ENTRY`, `SPOT_TARGET_A..D`, 16 at most per area) looked up with `int world_spot(int spot, int* tx, int* ty)`. Village exits: east road (14,4) to the forest; north gap (9,0) to the cave; south over the bridge to the shore; the ruins are reached from the forest's east edge. Each area script is at most 60 ops (300 bytes).

**Budget.** 17 KB code, 2 KB data, 1.5 KB RAM.

**Steps.** (1) Map builder from scripts; rebuild the village map from `(projects, season)`; dynamic object table (16 entries: stumps, crops, chests, with a regrow timer). (2) Area change through the curtain: request, wait for `ui_curtain_closed()`, build, place actors, open. (3) Actor table of 8 with kinds (hero, villager, critter, monster); the sprite list becomes a static array of 48 entries; villagers draw body + head overlay. (4) Scheduler: blocks of 2.1, weights plus `econ_activity_bias`, weather and festival modifiers, a cooldown so the same activity is not chosen three times in a row, and `quest_need` (when it returns a need and the block allows it, weight 8). (5) Activities; each is a small phase machine with a hard time limit of 4 minutes (sleep and festival excepted): when the limit hits, the hero drops everything and walks home. (6) Fights: roll the outcome from sword level and hearts, then play swing, flash, poof, drop; hearts in `G.s.hero_hearts`, restored by sleeping and eating. (7) Interiors and the night camera of 4.4. (8) Villager schedules, the cat, critters by season and weather. (9) `world_on_event`: `GQ_EV_BUILT` places project tiles with a hammering scene; festivals move villagers to the plaza. (10) Remove `hero_away_progress` use and the postcard path (leave the function returning -1).

**Host tests (`test_world.cpp`) and acceptance.** Every area builds for every season and (village) every one of the 1,024 project masks; in each, every spot and exit is reachable from `SPOT_ENTRY` by BFS; no actor ever stands on a non-walkable tile (sampled each second over 3 days, 20 seeds); at most 8 actors; presence invariant of 4.4; every activity forced with `GQ_DBG_FORCE_ACT` ends within 4 min 10 s; area round trips leave the village map identical apart from dynamic objects; with stubs the hero still lives a full day (visits at least 6 different activities per day on average over 5 days).

**Recovery requirements.** Every array index derived from saved state or from a table is range-checked. No phase machine may wait forever: each wait has a timeout. Path finding failure is always handled (already the pattern today).

### 11.6 Chunk 4: economy and progression

**Goal.** Section 3 in full.

**Depends on.** Chunk 0. No rendering.

**Files.** `lib/gamecore/src/econ.cpp`, `econ_data.h`, `test/test_econ.cpp`.

**Budget.** 7.5 KB code, 1.5 KB tables, 200 B RAM.

**Steps.** (1) Item table (cap, price, season mask), `ITEM_ICON`. (2) Till, seasonal price, festival and event modifiers, reserve rule. (3) `GOALS[22]` and prerequisites; `econ_buy_goal` sets tool level or project bit and emits. (4) Fund and stars, village-fund mode. (5) Friendship, daily limits, `chat_today`. (6) Collection and achievements. (7) `econ_activity_bias`: -9 when the activity cannot produce anything (item at cap, tool missing, area locked), +3 when it produces a material the current goal lacks, +2 for selling when goods above reserve are worth at least 10 coins and the till is not empty, +3 for buying when affordable. (8) `econ_focus_item`.

**Host tests (`test_econ.cpp`) and acceptance.** 1,000,000 random calls to the public functions: `coins <= cap`, `fund < 100`, totals never decrease, no counter wraps, every item within its cap; `econ_sell_all` never sells below the reserve and never pays more than the till; sum of goal prices is 4,650; goals are bought in table order and never before `min_day`; a scripted "busy hero" (gathers to the caps, sells and does 3 errands every day) buys all 22 goals between day 50 and day 90; a "lazy hero" (30% of that) still buys goal 6 by day 40 and never deadlocks (there is always a goal whose materials are obtainable); every achievement threshold is reachable; `econ_init` after `gq_save_sanitize` on random input never trips ASan.

**Recovery requirements.** All arithmetic saturating; table lookups clamp their index.

### 11.7 Chunk 5: quests and story

**Goal.** Section 4.1 to 4.3 and all text.

**Depends on.** Chunk 0.

**Files.** `lib/gamecore/src/quest.cpp`, `quest_data.h`, `text_data.h`, `test/test_quest.cpp`.

**Budget.** 6 KB code, 1 KB tables, 12 KB strings (hard limit, `static_assert(sizeof(TEXT_BLOB) <= 12288)`), 300 B RAM.

**Steps.** (1) Text blob and `gq_text`; names. (2) Tables `QUESTS[28]`, `QSTEPS[<=80]`. (3) Selection: story first, then side quests whose requirement holds, then up to 3 errands per day drawn at 06:00 (needs the notice board project). (4) Step logic per the table in 4.2; `quest_need`; `quest_talk`; rewards through `econ_*`. (5) Ambient lines: per NPC 10 general lines, 2 per season, 2 for rain, 2 per friendship tier; hero replies 8. (6) Band text. (7) Legends from year 2.

**Host tests (`test_quest.cpp`) and acceptance.** Table integrity: every index and text id in range; every string uses only font glyphs, at most 2 lines of 19 characters, band texts at most 14; no quest is unreachable (a fixed-point computation over requirements starting from day 1 with all goals buyable reaches all 28); a scripted playthrough that feeds the events each step asks for finishes chapter 5 on day 27 or later and not before; `quest_need` never points to a locked area; at most 3 paid errands per day; random event storms (1,000,000 random `quest_on_event` calls) never trip ASan and never leave `q_step` out of range.

**Recovery requirements.** `q_active`, `q_step` and every table index are validated on every use, not only at load.

### 11.8 Chunk 6: calendar, weather, festivals, events

**Goal.** Section 5 and the weather side of section 6.

**Depends on.** Chunk 0.

**Files.** `lib/gamecore/src/calendar.cpp`, `weather.cpp`, `test/test_calendar.cpp`.

**Budget.** 6 KB code, 0.5 KB tables, 400 B RAM.

**Steps.** (1) Time and day rule of 5.1 (already started in 0A). (2) Weather machine with `WEATHER_ODDS[4][7]`. (3) `cal_look`: day/night keyframes (today's table) combined with the weather row of 5.3; lightning. (4) Particles of 6.4 and the governor. (5) Festivals and daily events; `cal_festival`, `cal_event`. (6) Hourly bell event after chapter 5 (`GQ_EV_HOUR` already exists; the world animates the bell).

**Host tests (`test_calendar.cpp`) and acceptance.** Feeding the wall clock minute by minute for 400 days gives exactly 400 day increments; jumps of -720..+720 minutes at 1,000 random moments never change the day (except a natural crossing in the same call, which counts once); clock invalid then valid then invalid: no day change, minute continuous; season order and lengths; snow only in winter; each weather state occurs in each season it is allowed in, within 3 standard deviations of its odds over 2,000 days; festival flag true exactly on day 7 between 16:00 and 22:00; particle redraws at most 64 strips per second; with `weather_set_fps(70)` particles stop; look values within the ranges of 6.2.

**Recovery requirements.** No loop over elapsed time without a bound (a 250 ms tick may advance at most one minute).

### 11.9 Chunk 7: art

**Goal.** Paint the 151 new cells of 10.5; seasonal remap colours agreed with chunk 2.

**Depends on.** Chunk 0A (sheet layout and ids).

**Files.** `assets/src/*.png`, `assets/palette.gpl`, `tools/make_art.py`, `tools/png2tiles.py`, `tools/palette.py`, `tools/font5x7.py`, `tools/expand_sheets.py`, `lib/gamecore/src/assets_gen.h`, `test/test_art.py`.

**Budget.** 19.3 KB art (the sheets are full: 208 cells, 26,624 bytes), 0.2 KB code. The 16-colour palette does not change (the stock of colours is fixed; seasons come from remaps).

**Steps.** Extend `make_art.py` with one ASCII grid per cell, in the order: interiors (hero is on screen there most), characters and heads, village projects, forest and shore, cave and ruins, monsters and critters, icons. Regenerate. Keep outlines (palette entry 1) on every sprite so it reads on any ground. Add glyphs to `font5x7.py` only if the font range needs none changed (it stays 32..95).

**Host tests (`test_art.py`) and acceptance.** Sheet sizes; every pixel in the palette; no cell in the used-id list equals the placeholder; every character cell has at least 20 outline pixels and at least 30 transparent pixels; `assets_gen.h` is fresh; cell counts 96/64/48. Visual acceptance in the browser preview by the user.

**Recovery requirements.** None beyond the size budget: art cannot break recovery except by size, which CI enforces.

### 11.10 Integration pass (phase C)

1. Merge in the order of 11.1; after each merge all tests pass.
2. Extend `test_soak.cpp` to 30 simulated days, 3 seeds, with the invariants of 13.1.
3. Balance: adjust weights and prices only inside `econ_data.h`, `quest_data.h` and the scheduler tables until the soak ledger matches 3.2.
4. Size: the `quest` image must be at most 485 KB (15 KB under the gate); if not, cut in this order: ambient lines, daily events 12 -> 8, critters, the fountain animation.
5. The user reviews the browser preview at time scale x600 for one full game year, then the R2 checklist.

## 12. Risks and approvals

### 12.1 Risk register

| # | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R1 | The new WiFi ladder has a bug that leaves the clock unreachable | Low | Critical (no cable) | Pure state machine with host tests N1..N11; golden window; hotspot start and web routes untouched; R0 hardware checklist before building further |
| R2 | Power cut during the boot loader copy after an update | Very low | Critical | Cannot be removed. Page text: keep power for 60 s. Update rarely (3 planned flashes). |
| R3 | The save sector 0x3FA000 is used by stock after all | Low | Medium (stock data, not boot) | P1 off by default; read-only probe; refuses on foreign content; erase button; approval A1 |
| R4 | A wrong address in a flash write | Very low | Medium to high | One constant, symbol checks at boot and in CI, lint (flash calls in one file), journal logic host-tested with bounds |
| R5 | Image exceeds 500 KB as content lands | Medium | Blocks release (CI stops it) | Budgets per chunk, size report on every build, cut list in 11.10 |
| R6 | Heap runs low after weeks | Medium | Low with the watchdog | Streamed pages, heap watchdog, weekly restart, statistics on the status page |
| R7 | Frame rate too low with weather and 8 actors | Medium | Low (cosmetic) | Budgets of 6.1, governor, FPS on the status page, user reports the number |
| R8 | A game bug crashes on real data after days | Medium | Low | Crash counters (fast and slow), quarantine, safe mode, sanitised saves, ASan soak |
| R9 | Automatic restarts annoy or loop | Low | Low | Never in the first 30 min, WiFi restarts at most 1/day in steady state, switchable, approval A2 |
| R10 | The upload checks reject a legitimate image (stock) | Low | Medium | Stock passes by size (and by CRC once known); "install anyway" always available; selftest |
| R11 | Unverified facts about the Arduino core (flash symbols, `WiFi.begin` behaviour, RTC blocks 0..31) | Medium | Medium | CI symbol check; design passes credentials explicitly; existing code already relies on the RTC rule and works on the device |
| R12 | Parallel chunks drift apart at the interfaces | Medium | Schedule | Headers frozen by chunk 0 with working stubs; each chunk tested against stubs; integration pass |
| R13 | Open update page (no login) on the LAN and on the hotspot | Low at home | High | Accepted and documented; a login could lock the owner out |
| R14 | Text or art budget overrun | Medium | Low | `static_assert` on the text blob; fixed sheet sizes |
| R15 | Economy balance off on the real clock (different activity counts than in simulation) | Medium | Low | Income is bounded by daily limits, not by activity counts; soak test at real time scale |

### 12.2 Decisions that need the user's approval

Nothing in this list may be implemented or enabled on the assumption that the answer is yes.

| Id | Decision | Why it needs approval | Recommendation | If declined |
|---|---|---|---|---|
| **A1** | **NEEDS USER APPROVAL: save settings and game to flash sector 0x3FA000** (section 9.4) | First code in the project that erases and writes flash outside the updater. Unverified whether the stock firmware uses that sector. Flash wear. | Approve in two steps: (1) allow a build with the read-only probe; (2) after reading the probe line, press "Enable". | P0 only: progress and settings survive restarts and updates, not power cuts; the backup file still works. |
| **A2** | **NEEDS USER APPROVAL: automatic planned restarts** (WiFi lost for 6 h/12 h/24 h; heap low; weekly at 04:00) | The clock restarts without being asked (about 5 s dark). A restart is always a small exposure. | Approve all three; they are rate-limited and never happen in the first 30 minutes. | The ladder stops at the radio cycle; low heap ends in a crash and the crash counter; no weekly restart. |
| **A3** | **NEEDS USER APPROVAL: WiFi modem sleep off** | About 0.25 W more power, slightly warmer. | Approve. | Keep the default sleep mode; the web page may answer slowly, more beacon timeouts. |
| **A4** | **NEEDS USER APPROVAL: stricter upload checks** (CRC, refuse images without the GeekQuest update page unless stock-sized or "install anyway") | Changes the update path itself. | Approve; the override keeps every path open. | Keep today's checks. |
| **A5** | **NEEDS USER APPROVAL: replace SDK auto-reconnect by our own ladder in `safety.cpp`** (chunk 0B) | It is the fix for the WiFi report, but it changes recovery code that is proven on the user's clock. | Approve, and run the R0 checklist immediately after flashing. | Keep auto-reconnect and add only diagnostics; the WiFi problem will likely stay. |
| A6 | Information needed: CRC32 of the stock `SDPro_V1.0.6` file (the new update page will display it when the file is chosen) | Lets the clock recognise the stock image exactly. | Provide once after R1. | Stock is recognised by size only. |
| A7 | For transparency (no flash write, no approval needed for safety): RTC-memory persistence (P0) and backup download | | Ship in chunk 1. | |
| A8 | Design choices to confirm (not safety): 28-day game year instead of real-world seasons; names (Elderwick, the Evenbell); info cards every 10 minutes by default; real-world weather is out of scope | | As written. | |

## 13. Test plan

### 13.1 Host tests (ASan + UBSan, `-Werror`)

| File | Owner | Covers |
|---|---|---|
| `core_smoke.cpp` | 0 | every scene, 5 seeds, 1 h each, strip overrun guard |
| `test_save.cpp` | 0 | save format, rejection, sanitize, round trip |
| `test_presence.cpp` | 0 | hero presence, coin cap, daily income bound |
| `test_net.cpp` | 0B, 1 | N1..N11, restart limits |
| `test_journal.cpp` | 1 | torn writes, cut erases, bounds, foreign content |
| `test_ui.cpp` | 2 | formatting, palette identity, cards at extremes, curtain, dirty budget |
| `test_world.cpp` | 3 | map connectivity for all project masks, activities end, actor limits |
| `test_econ.cpp` | 4 | invariants under random calls, goal pacing |
| `test_quest.cpp` | 5 | table integrity, reachability, playthrough length |
| `test_calendar.cpp` | 6 | day rule under clock jumps, weather statistics, particle budget |
| `test_soak.cpp` | integration | 30 days, 3 seeds, all invariants below |
| `test_art.py`, `check_bin_selftest.py`, `lint_safety.py` | 7, 1 | Python checks |

Soak invariants (checked every simulated second unless stated):

1. Presence: `GQ_Q_HERO_PRESENCE != 0`; curtain never longer than 700 ms.
2. Liveness: at least one dirty strip in every 2 s window; no activity longer than 4 min 10 s except sleep and festivals.
3. Economy: `coins <= cap`; repeatable income per day at most 132 (double till on festival days); by day 30 between 8 and 14 goals bought; no counter decreases that should not.
4. Story: chapter at most `1 + day / 5`; chapter 5 not before day 27.
5. Save: every simulated hour, export, import into a fresh `gc_init`, export again: the two exports are identical except `seq` and CRC.
6. Rendering: average dirty strips per frame at most 6 over each simulated hour; at most 48 sprite list entries; overrun guard intact.
7. Variety: at least 10 different activities and at least 3 areas visited per simulated week.

### 13.2 Preview checks (the user, in a browser, before every content flash)

1. Time scale x600: watch one game week; the hero is always findable; the band text makes sense.
2. Jump through the 4 seasons and the 7 weather states.
3. Open each card.
4. Export the save, reload the page, import: the village looks the same (day, coins, projects).
5. Give 5,000 coins: the HUD stays at the cap and stars go up.

### 13.3 CI gates (every push; a failure produces no `.bin`)

| Gate | Check | Owner |
|---|---|---|
| G1 | `assets_gen.h` is fresh (exists today) | 0 |
| G2 | every `test/*.cpp` builds and passes under ASan/UBSan; every `test/*.py` passes | 0 |
| G3 | wasm preview builds | exists |
| G4 | `tools/lint_safety.py` (rules below) | 1 |
| G5 | `tools/check_bin_selftest.py` | 1 |
| G6 | firmware builds for `test` and `quest` | exists |
| G7 | `tools/check_bin.py` on every `.bin` (rules below) | 1 |
| G8 | recovery-change guard: if a file of 8.10 rule 1 differs from the merge base, a commit message must contain `RECOVERY-CHANGE:` and `GQ_RECOVERY_REV` must differ | 1 |
| G9 | size report in the job summary: `.bin` bytes and `.data + .bss` bytes per environment, with the difference to the previous build | 1 |

`check_bin.py`, in addition to today's rules:

1. Size: fail above 500 KB (today); warn above 485 KB.
2. Required strings, added: `GQ-RECOVERY-REV`, `/safemode`, `/backup`, `/wifi`.
3. Forbidden symbols in the ELF (`nm`): anything matching `EEPROMClass`, `lfs_`, `LittleFS`, `SPIFFS`, `spiffs_`, `ESP8266HTTPUpdate`, `ArduinoOTA`, `HTTPClient`.
4. Linker symbols: `_FS_start == 0x40300000` (today), `_FS_end == 0x405FA000`, `_EEPROM_start == 0x405FB000`.
5. RAM: `.data + .bss` at most `RAM_BASELINE + 8192`.
6. Builds without `GQ_FLASH_SAVE` must not contain the string `GQJ1`; builds with it must contain `GQJ1` and `Save sector`.
7. The marker `GQ-OTA-PAGE-v1` occurs exactly once.

`lint_safety.py` (regular expressions over sources with comments and strings removed):

| Rule | Scope | Forbidden or required |
|---|---|---|
| L1 | `lib/gamecore/src/*` except `platform.h` | no `#include <`; none of `malloc calloc realloc free( new delete printf sprintf memcpy memset strlen float double` |
| L2 | `firmware/*` | `EEPROM`, `LittleFS`, `SPIFFS`, `flashEraseSector`, `flashWrite`, `spi_flash_` only in `persist.cpp`; `ESP.restart`, `ESP.reset` only in `safety.cpp`; `WiFi.persistent(true)` exactly once, in `safety.cpp`; `WiFi.mode(`, `enableAP`, `enableSTA`, `softAP` only in `safety.cpp` and `netwatch.cpp` |
| L3 | `firmware/*` | no `while (WiFi.`; no `delay(` with a literal above 500 outside `st7789.cpp`; no `WiFiClientSecure`, `HTTPClient`, `ArduinoOTA` |
| L4 | `firmware/main.cpp` | the first call in `setup()` after `Serial.begin` is `safety::begin()`; the first statement of `loop()` is `safety::loop();` |
| L5 | `firmware/*`, `lib/gqsafe/*` | no `millis()` followed by `<` or `>` (absolute time comparison) |
| L6 | `platformio.ini` | contains `eagle.flash.4m3m.ld`, `flash_size = 4MB`, `flash_mode = dio` |
| L7 | `lib/gqsafe/src/*` | no `#include <Arduino.h>` and no `ESP.` |

### 13.4 Pre-flash checklist (the user, before every flash)

1. The `.bin` comes from the `builds` branch `firmware/` folder (not `obsolete/`), its name starts with `SDP_GeekQuest_`, and the CI run for that commit is green.
2. You have the previous known-good `.bin` and the stock `SDPro_V1.0.6` file on the device you flash from.
3. The clock is on a stable power supply and will not be moved for two minutes.
4. On the current firmware's status page: "Room for update" is larger than the new file; crash counter is 0.
5. (From R1 on) Press "Download backup" and keep the file.
6. Upload. Wait for "Installed N bytes". Do not unplug for 60 s.
7. If the page does not come back within 2 minutes: power-cycle once, join `GEEKQUEST-XXXX` (password `GEEKQUEST`) within 10 minutes, open `http://192.168.4.1/`.

### 13.5 Hardware smoke checklist (the user, after every flash)

Common to every release:

1. The status page opens over the home WiFi; the firmware version is the new one.
2. For the first 10 minutes the hotspot `GEEKQUEST-XXXX` is visible; joining it and opening `http://192.168.4.1/update` shows the update page.
3. `/update` opens over the home WiFi (do not upload anything).
4. "Restart in safe mode" shows SAFE MODE with the hotspot name; the status page works; "Restart" returns to the game.
5. Report from the status page: free heap, FPS, `loop_max_ms`, last reset reason.

R0 (chunk 0), in addition:

6. Switch the router's WiFi off for 3 minutes, then on: within about 2 minutes of the network being back the clock is reachable again without touching it; the status page shows the reconnect count. While the WiFi was off, the bottom band showed the hotspot name and the hotspot could be joined.
7. Leave the router off for 30 minutes (optional): same result.
8. Watch for 10 minutes: when the hero leaves on a quest the postcard appears; shop and tavern visits show a marker over the door.
9. After one day: coins are below 250 and wood is not stuck at 0. After a week: coins never above the cap, stars appear.

R1 (chunk 1), in addition:

10. Set time zone, brightness and colour depth; press "Restart": they are still set, and the game continues on the same day with the same coins.
11. Unplug and replug: settings and game reset (expected without A1); "Restore backup" with the file from step 5 of 13.4 brings them back.
12. The status page shows the network and health boxes; `/info` returns one JSON line.
13. Choose a random non-firmware file on the update page: it is refused with a clear text and the clock keeps running.
14. Choose the stock `SDPro` file but do not press upload: note the CRC32 the page shows and report it (A6).
15. If A1 was approved for the probe: report the "Save sector" line. Only after that, and a few days of use, press "Enable saving to flash"; then repeat step 11 and expect the settings and the game to survive the power cut.

R2 (content), in addition:

16. Over two days: the hero is on screen or clearly marked at all times; at least one visit each to an interior and to the forest is seen; rain or another weather state appears; the hourly clock card shows.
17. FPS during rain is reported from the status page.
18. After a week: at least 3 goals bought (visible on the village card), the season changed once, no crash counter increase.
