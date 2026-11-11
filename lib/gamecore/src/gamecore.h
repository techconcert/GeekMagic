// GeekQuest game core: portable, allocation-free, no libc.
// The firmware and the browser preview both drive it through this API.
#pragma once
#include "platform.h"

#define GC_W 240
#define GC_H 240
#define GC_TILE 16
#define GC_COLS 15
#define GC_ROWS 15
#define GC_STRIP_H 16
#define GC_STRIP_PIXELS (GC_W * GC_STRIP_H)

#ifdef __cplusplus
extern "C" {
#endif

// GC_SCENE_INFO: plain text screen used by the firmware's safe mode.
enum { GC_SCENE_TEST = 0, GC_SCENE_VILLAGE = 1, GC_SCENE_INFO = 2 };

// Start a scene. swap_bytes=1 makes the renderer emit big-endian RGB565
// (what the ST7789 wants over SPI); the browser preview passes 0.
void gc_init(int scene, uint32_t seed, int swap_bytes);

// Advance the simulation by dt milliseconds.
void gc_tick(uint32_t dt_ms);

// Wall clock (from NTP). valid=0 shows --:--.
void gc_set_clock(int hour, int minute, int valid);

// Short status messages rotated in the bottom bar (IP address, AP name...).
// line 0..3, text is copied (max 31 chars). Pass "" to clear.
void gc_set_status(int line, const char* text);

// Colour depth 0..100: darkens mid-tones to fight a washed-out panel (0 = off).
void gc_set_depth(int percent);

// Frames per second x10, shown in the test scene.
void gc_set_fps(int fps_x10);

// Bit n set = strip n (pixel rows n*16..n*16+15) needs re-sending.
uint32_t gc_dirty_mask(void);
void gc_clear_dirty(void);
void gc_mark_all_dirty(void);

// Render one 240x16 strip into out[240*16].
void gc_render_strip(int strip, uint16_t* out);

#include "gq_night.h"   // GC_NIGHT_START_MIN / GC_NIGHT_END_MIN: the built-in night

// ---- persistence (R5). The core does no I/O; firmware/store.cpp keeps the bytes in flash.
#define GC_SAVE_BYTES 192
enum { GC_SAVE_OK = 0, GC_SAVE_ERR_LEN, GC_SAVE_ERR_MAGIC, GC_SAVE_ERR_VERSION, GC_SAVE_ERR_CRC };
int      gc_save_export(uint8_t* buf, int cap);        // village scene only. Returns GC_SAVE_BYTES, or 0.
int      gc_save_import(const uint8_t* buf, int len);  // 1 = accepted, sanitized, applied. 0 = rejected, nothing changed.
uint32_t gc_save_serial(void);                         // changes whenever the saved state changed
// Day-1 defaults (same as a first boot).
void     gc_reset_game(void);

// ---- environment
enum { GC_NET_ONLINE = 0, GC_NET_OFFLINE = 1, GC_NET_AP_ONLY = 2 };
enum { GC_OPT_CARD_PERIOD = 0, GC_OPT_NIGHT_QUIET = 1,
       GC_OPT_SLEEP_MODE = 2 };   // 0 = built-in night (default), 1 = schedule in use: stay up, 2 = go to bed and sleep
void gc_set_date(int year, int month, int day, int weekday, uint32_t epoch);  // year 0 = unknown; weekday 0 = Sunday
void gc_set_clock_quality(int approximate);     // 1 = restored after a restart, not NTP-synced yet
void gc_set_net(int state);                     // GC_NET_ONLINE 0, GC_NET_OFFLINE 1, GC_NET_AP_ONLY 2
void gc_set_option(int key, int value);         // GC_OPT_CARD_PERIOD (minutes, 0 = off), GC_OPT_NIGHT_QUIET (0/1),
                                                // GC_OPT_SLEEP_MODE (0..2, other values are ignored)

// Live weather (R3). Call once a second with the current reading; the values are clamped.
// wx = GQ_W_* 0..6 or GC_WX_NONE (anything else counts as none); temp_c10 = tenths of a degree Celsius
// (-999..999); flags = GC_WXF_*. Not saved; game rules ignore it until calendar.cpp uses it.
#define GC_WX_NONE  (-1)
#define GC_WXF_FAHRENHEIT 1     // display unit
#define GC_WXF_NIGHT      2     // is_day == 0
#define GC_WXF_STALE      4     // age class 2
void gc_set_live_weather(int wx, int temp_c10, int flags);

// ---- introspection for tests, the preview and the status page
// Keys are GQ_Q_* and commands GQ_DBG_* (gq_types.h). Unknown key returns 0, unknown command does nothing.
int32_t gc_query(int key);
void    gc_debug(int cmd, int32_t arg);

#ifdef __cplusplus
}
#endif
