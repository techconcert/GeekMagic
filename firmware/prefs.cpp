// See prefs.h. RTC user memory map (blocks of 4 bytes; 0..31 belong to the boot loader and are never touched):
//   80..91  GqSettings (48 bytes)        96..127  owned by safety.cpp and netwatch.cpp
// The only flash access in this file is ESP.flashRead() of the probe: nothing here writes or erases flash.
#include "prefs.h"
#include "safety.h"
#include "st7789.h"
#include "gamecore.h"

// The linker script of the stock 4M3M layout defines these. Only their addresses are used.
extern "C" uint32_t _FS_end;
extern "C" uint32_t _EEPROM_start;

namespace prefs {

static const uint32_t RTC_SET = 80;
static const uint32_t SAVE_ADDR = 0x3FA000UL;   // the 4 KB gap between stock's file system and its EEPROM sector

static GqSettings s_set;
static bool s_display = false, s_game = false;

void begin() {
  GqSettings r;
  ESP.rtcUserMemoryRead(RTC_SET, (uint32_t*)&r, sizeof(r));
  if (gqs_check(&r) == 0) s_set = r;
  else gqs_defaults(&s_set);
  safety::setTimeZone(s_set.tz);
  safety::setDepth(s_set.depth);
}

const GqSettings& settings() { return s_set; }

void apply(bool display, bool game) {
  s_display = display;
  s_game = game;
  if (display) lcd::setBrightness(s_set.brightness);
  if (game) {
    gc_set_option(GC_OPT_CARD_PERIOD, s_set.card_period);
    gc_set_option(GC_OPT_NIGHT_QUIET, (s_set.flags & GQS_FLAG_NIGHT) ? 1 : 0);
  }
}

bool set(const GqSettings& s) {
  GqSettings t = s;
  gqs_seal(&t);
  if (gqs_check(&t) != 0) return false;
  s_set = t;
  safety::setTimeZone(s_set.tz);
  safety::setDepth(s_set.depth);
  apply(s_display, s_game);
  ESP.rtcUserMemoryWrite(RTC_SET, (uint32_t*)&s_set, sizeof(s_set));
  return true;
}

void reset() {
  GqSettings d;
  gqs_defaults(&d);
  set(d);
}

void probeText(char* buf, size_t n) {
  if (((uint32_t)(uintptr_t)&_FS_end - 0x40200000UL) / 4096 != 0x3FA || ((uint32_t)(uintptr_t)&_EEPROM_start - 0x40200000UL) / 4096 != 0x3FB ||
      ESP.getFlashChipRealSize() != 4UL * 1024 * 1024) {
    snprintf_P(buf, n, PSTR("Save sector: the flash layout is not the expected one."));
    return;
  }
  alignas(4) uint8_t b[64];
  uint8_t head[16];
  memset(head, 0, sizeof(head));
  bool blank = true, ok = true;
  for (uint32_t off = 0; off < 4096 && ok; off += sizeof(b)) {   // read only
    ok = ESP.flashRead(SAVE_ADDR + off, (uint32_t*)b, sizeof(b));
    if (off == 0) memcpy(head, b, sizeof(head));
    for (uint32_t i = 0; ok && i < sizeof(b); i++)
      if (b[i] != 0xFF) blank = false;
  }
  if (!ok) {
    snprintf_P(buf, n, PSTR("Save sector 0x3FA000: could not be read."));
  } else if (blank) {
    snprintf_P(buf, n, PSTR("Save sector 0x3FA000: blank."));
  } else {
    char hex[33];
    for (int i = 0; i < 16; i++) {
      hex[i * 2] = "0123456789ABCDEF"[head[i] >> 4];
      hex[i * 2 + 1] = "0123456789ABCDEF"[head[i] & 15];
    }
    hex[32] = 0;
    snprintf_P(buf, n, PSTR("Save sector 0x3FA000: in use by something else (first 16 bytes %s)."), hex);
  }
}

}  // namespace prefs
