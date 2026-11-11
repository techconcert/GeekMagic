// Functions the modules of the game core call on each other (docs/DESIGN.md 10.4).
// Chunk 0 provides working stubs for everything; chunks 2..6 replace the bodies
// without changing these signatures.
#pragma once
#include "gq_types.h"

// ---- game.cpp (chunk 0)
void gq_emit(int ev, int a, int b);              // never re-entrant: events raised inside a handler are queued (8 deep) and
                                                 // delivered after the current one; a full queue drops the event and counts it
void gq_touch(void);                             // G.serial++ : call after any change of the game state

// ---- calendar.cpp, weather.cpp (chunk 6)
void cal_init(void);                             // from G.s
void cal_tick(uint32_t dt);                      // minute, day, weather; emits HOUR, NEW_DAY, WEATHER, FESTIVAL
void cal_on_event(int ev, int a, int b);
int  cal_minute(void);  int cal_is_night(void);  // night = 22:00..05:59
int  cal_season(void);  int cal_day_of_season(void);  int cal_year(void);
int  cal_weather(void); int cal_festival(void);  /* -1 or 0..3, only between 16:00 and 22:00 */  int cal_event(void);
void cal_look(GqLook* out);                      // palette inputs for this minute
// R3 live weather (docs/DESIGN-R3.md 2.7). Declared by R3-0C with weak stubs in game.cpp that keep today's
// behaviour (nothing live); chunk W2 defines the real ones in calendar.cpp (a strong definition replaces the stub).
// The input is G.live_wx1 / G.live_temp10 / G.live_flags (set by gc_set_live_weather).
int  cal_live(void);                             // 1 while live data is in use
int  cal_weather_shown(void);                    // what the screen shows: live state if any, else simulated
int  cal_temp(int* out, int* is_f);              // 1 and whole degrees in the display unit (round half away from zero); 0 if none
int  cal_temp_c(int* out);                       // 1 and whole degrees Celsius, for dialogue conditions
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
// additions by chunk 4 (nothing above changed)
int  econ_item_icon(int item);                   // items.png cell for an item id
int  econ_goal_count(void);                      // 22
void econ_goal_name(int goal, char* buf, int cap);   // short name, at most 9 characters
int  econ_goal_band(char* name, int cap, int* num, int* den);  // band: name, coins so far, price; 0 = village fund mode
void econ_ach_name(int index, char* buf, int cap);   // achievement name, at most 13 characters

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
// Table sizes (used by the tests):
int  quest_count(void);                          // number of quests in the table
int  quest_steps(int quest);                     // number of steps of that quest

// ---- world.cpp, hero.cpp, npc.cpp (chunk 3)
void world_init(void);                           // builds the village from G.s (projects, season)
void world_tick(uint32_t dt);
void world_render(int strip, uint16_t* out);
void world_on_event(int ev, int a, int b);       // BUILT -> place project tiles; NEW_DAY -> seasonal tiles
int  world_area(void);
int  hero_activity(void);                        // GQ_ACT_*
void hero_status_text(char* buf, int cap);       // "GOING TO EAT", "QUEST: CAVE", "TALK TO BRAM"... (status band)
int  hero_away_progress(int* place_txt);         // chunk 0 stopgap: 0..255 while off screen, -1 otherwise
int  world_actor_count(void);
int  hero_in_bed(void);                          // 1 while the hero lies in bed (GQ_Q_HERO_ABED)
void hero_force_chat(int npc);                   // gc_debug(GQ_DBG_FORCE_CHAT): chat with villager npc (0 = anyone) now

// ---- talk.cpp (R3 dialogue engine; R3-0C ships a stub, chunk D1 the real one)
void talk_init(void);
void talk_on_event(int ev, int a, int b);        // remembers what the hero just did; called from quest_on_event
int  talk_begin(int npc);                        // picks a script for npc 1..9; returns the number of turns (2..4), 0 = none
int  talk_turn(int i, char* buf, int cap);       // text of turn i with slots filled; returns 1 if the hero speaks, 0 the villager.
                                                 // buf gets at most cap-1 characters; "|" separates the two lines of the box
int  talk_quest_reply(char* buf, int cap);       // hero's answer after a quest line (quest_talk); returns 1 if there is a line
enum { TALK_SOLO_BED, TALK_SOLO_WAKE, TALK_SOLO_DID };
int  talk_solo(int kind, char* buf, int cap);    // hero thinking aloud; 1 if there is a line

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
