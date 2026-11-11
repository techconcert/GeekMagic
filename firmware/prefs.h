// Settings kept in RTC user memory (time zone, brightness, colour depth, card period, flags), and the READ-ONLY
// probe of the flash sector 0x3FA000 that a later release may use for saving (R2 never writes or erases flash).
//   RTC memory survives restarts, crashes and updates, not a power cut. The game itself is not saved at all.
// This file replaces persist.* of release R1; it contains no flash write and no game state.
#pragma once
#include <Arduino.h>
#include "settings.h"

namespace prefs {

// Loads the settings from RTC (else defaults) and applies time zone and colour depth. Call right after safety::begin().
void begin();
const GqSettings& settings();
// Validates and applies a new record (false and no change when invalid) and stores it in RTC.
bool set(const GqSettings& s);
void reset();
// Pushes brightness, card period and night dimming to the display and the game. `display` / `game` are false when skipped.
void apply(bool display, bool game);

// One line for the status page, e.g. "Save sector 0x3FA000: blank." Read only. buf is plain text.
void probeText(char* buf, size_t n);
// RECOVERY-CHANGE (R3-0F): same for the last sector of the program area, 0xFF000, with where the image ends. Read only. buf >= 160.
void spareText(char* buf, size_t n);

}  // namespace prefs
