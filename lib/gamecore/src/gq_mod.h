// Functions the modules of the game core call on each other (docs/DESIGN.md 10.4).
// Chunk 0 provides working stubs for everything; chunks 2..6 replace the bodies
// without changing these signatures.
#pragma once
#include "gq_types.h"

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
// Additions of chunk 0 (needed by gq_save_sanitize to clamp q_active / q_step; stubs return 0):
int  quest_count(void);                          // number of quests in the table
int  quest_steps(int quest);                     // number of steps of that quest

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
