// Glue for the browser preview build (clang --target=wasm32, no libc).
#include "../lib/gamecore/src/gamecore.h"

extern "C" {
void* memset(void* d, int c, unsigned long n) { unsigned char* p = (unsigned char*)d; while (n--) *p++ = (unsigned char)c; return d; }
void* memcpy(void* d, const void* s, unsigned long n) { unsigned char* a = (unsigned char*)d; const unsigned char* b = (const unsigned char*)s; while (n--) *a++ = *b++; return d; }

static uint16_t strip_buf[GC_STRIP_PIXELS];
static char text_buf[64];
static uint8_t save_buf[GC_SAVE_BYTES];

__attribute__((export_name("strip_ptr"))) uint16_t* strip_ptr(void) { return strip_buf; }
__attribute__((export_name("text_ptr"))) char* text_ptr(void) { return text_buf; }
__attribute__((export_name("init"))) void w_init(int scene, uint32_t seed) { gc_init(scene, seed, 0); }
__attribute__((export_name("tick"))) void w_tick(uint32_t dt) { gc_tick(dt); }
__attribute__((export_name("set_clock"))) void w_clock(int h, int m, int v) { gc_set_clock(h, m, v); }
__attribute__((export_name("set_status"))) void w_status(int line) { gc_set_status(line, text_buf); }
__attribute__((export_name("set_depth"))) void w_depth(int d) { gc_set_depth(d); }
__attribute__((export_name("set_fps"))) void w_fps(int f) { gc_set_fps(f); }
__attribute__((export_name("dirty"))) uint32_t w_dirty(void) { return gc_dirty_mask(); }
__attribute__((export_name("clear_dirty"))) void w_clear(void) { gc_clear_dirty(); }
__attribute__((export_name("save_ptr"))) uint8_t* save_ptr(void) { return save_buf; }
__attribute__((export_name("save_export"))) int w_save_export(void) { return gc_save_export(save_buf, GC_SAVE_BYTES); }
__attribute__((export_name("save_import"))) int w_save_import(int len) { return gc_save_import(save_buf, len); }
__attribute__((export_name("query"))) int w_query(int key) { return (int)gc_query(key); }
__attribute__((export_name("debug"))) void w_debug(int cmd, int arg) { gc_debug(cmd, arg); }
__attribute__((export_name("render"))) void w_render(int s) { gc_render_strip(s, strip_buf); }
}
