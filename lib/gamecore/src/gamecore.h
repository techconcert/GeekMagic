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

// Frames per second x10, shown in the test scene.
void gc_set_fps(int fps_x10);

// Bit n set = strip n (pixel rows n*16..n*16+15) needs re-sending.
uint32_t gc_dirty_mask(void);
void gc_clear_dirty(void);
void gc_mark_all_dirty(void);

// Render one 240x16 strip into out[240*16].
void gc_render_strip(int strip, uint16_t* out);

#ifdef __cplusplus
}
#endif
