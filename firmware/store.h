// R5 power-cut saving (approved by the owner on 2026-11-11): settings, weather/sleep record and the game are kept in
// flash sectors 0x80000..0x82FFF as a ring of checksummed records (lib/gqsafe/src/slotlog.h). Those sectors lie above the
// running image (CI cap 500 KB) and below the lowest start of a staged firmware update, and are used by nothing else.
//   - Loaded once at boot. Never written in safe mode, while a firmware upload runs, or when the image is larger than 500 KB.
//   - Written when the settings change (a few seconds later), then at most every 20 minutes for the game and when a new
//     game day starts. A write that fails verification three times switches saving off until the next restart.
//   - After two crashes in a row the saved game is not loaded and not overwritten (the settings still are).
// This is the only file that erases or writes flash (lint L2).
#pragma once
#include <Arduino.h>
#include "settings.h"
#include "gqconf.h"

namespace store {

// Reads the newest valid record (read only). Call after safety::begin(), before prefs::begin().
void begin(bool safeMode);
void loop();                                   // call every loop(); does its work at most once a second

// What the record held at boot (null = no valid record, or the part is quarantined).
const GqSettings* savedSettings();
const GqConf* savedConf();
const uint8_t* savedGame();                    // GC_SAVE_BYTES bytes

bool active();                                 // saving is on (not safe mode, flash verified)

// Status text for the status page ("Saved to flash 12 min ago (record 40)."), empty when there is nothing to say.
void statusLine(char* buf, size_t n);

}  // namespace store
