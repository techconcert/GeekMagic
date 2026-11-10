// Recovery layer. This is the part that keeps the clock flashable over WiFi
// no matter what the game code does. It must stay independent of the game
// and the display: if either breaks, WiFi + the update page still work.
#pragma once
#include <Arduino.h>

namespace safety {

// Call FIRST in setup(), before touching the display or the game.
void begin();
// Call every loop() iteration (handles web requests, DNS, WiFi policy).
void loop();

// Crash-loop protection: true after 3 unplanned resets in a row, or when the
// user asked for safe mode. The game must not run in safe mode.
bool safeMode();
// After 6 unplanned resets in a row: don't even initialise the display.
bool noDisplay();
// A firmware upload is in progress: pause the game.
bool updating();

bool staConnected();
bool apActive();
String ipString();       // station IP, or "" when not connected
const char* apName();
const char* apPassword();

// Restart in a way the crash counter does not count.
void plannedRestart(bool intoSafeMode = false);

// Wall clock from NTP. Returns false until synced.
bool localTime(int& hour, int& minute);

}  // namespace safety
