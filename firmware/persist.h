// Persistence (design 9, 8.8): settings and game state.
//   P0 (always on): RTC user memory. Survives restarts, crashes and updates, not a power cut.
//   Backup: the 240-byte snapshot can be downloaded and restored through the browser.
//   P1 (only with -DGQ_FLASH_SAVE=1, and only after the user presses "Enable saving"): one flash sector.
// This is the ONLY file that may touch flash (lint rule L2). Nothing here runs before safety::begin().
#pragma once
#include <Arduino.h>
#include "settings.h"

#ifndef GQ_FLASH_SAVE
#define GQ_FLASH_SAVE 0
#endif

namespace persist {

// Loads settings: RTC copy, else flash journal, else defaults. Applies the time zone and colour depth.
// Never writes anything. Call right after safety::begin().
void begin(bool safeMode);
const GqSettings& settings();
// Validates and applies a new settings record (rejects an invalid one and returns false), stores it in RTC.
bool setSettings(const GqSettings& s);
// Pushes the settings that belong to the display and the game (brightness, card period, night dimming).
// Call after lcd::begin() and gc_init(). `display` is false when the display was skipped.
void apply(bool display);

// Newest valid saved game (RTC, or flash when enabled). False if none, quarantined, or safe mode.
bool loadGame(uint8_t out[GQS_GAME_BYTES]);
// Where the game returned by loadGame() came from: 0 none, 1 RTC memory (a restart: the clock may be restored
// from the saved epoch), 2 flash (a power cut may have happened: the saved time is stale).
uint8_t loadSource();
// Settings + game -> RTC. Does nothing for the game while the saved game is quarantined or in safe mode.
void saveRtc();
// Call every loop iteration: saves to RTC every 60 s when the game changed; flash policy 9.4.
void loop();
// "Save now" button: RTC, and flash when enabled (at most once per 10 s).
void saveNow();
// Called by safety just before a planned restart.
void beforeRestart();

bool backupExport(uint8_t out[GQS_BACKUP_BYTES]);
bool backupImport(const uint8_t* in, size_t n);   // validates both records; false changes nothing
void factoryResetGame();
void resetSettings();
void discardSavedGame();                           // quarantine: throw the stored game away and carry on with the new one

// One short line for the status page: where the game is saved ("RTC only", "RTC + flash", ...).
const char* statusText();
// Game box for the status page, written into buf (plain text, no markup).
void gameSummary(char* buf, size_t n);

#if GQ_FLASH_SAVE
enum Probe : uint8_t { PROBE_BLANK, PROBE_JOURNAL, PROBE_FOREIGN, PROBE_LAYOUT_MISMATCH };
Probe probe();
bool flashEnable();
bool flashDisableAndErase();
bool flashSaveNow();
// Text lines for the status page (probe line, stock-sector CRC). Written into buf.
void probeText(char* buf, size_t n);
#endif

}  // namespace persist
