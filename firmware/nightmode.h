// R3 sleep schedule: screen off and 80 MHz at night, WiFi and the update page untouched (DESIGN-R3.md 3).
// FINAL interface; R3-0F ships a stub body that does nothing, chunk S1 fills it in.
// This is the only file that may call system_update_cpu_freq, lcd::sleep and lcd::wake (lint L12).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace nightmode {
void begin(bool safeMode, bool display);
void loop();
bool screenOff();                          // true while the display is switched off: main.cpp then skips rendering
bool wakeSoon();                           // the last 5 minutes before the wake time
void command(uint8_t cmd);                 // from the settings page: wake now / sleep now (values chosen by S1)
void statusLine(char* buf, size_t n);      // plain text; empty string = nothing to show
}  // namespace nightmode
