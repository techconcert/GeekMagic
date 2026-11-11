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

// Colour depth tuning value (0..100) set from the status page. Not saved yet.
int depth();

// Boot progress marker, kept in RTC memory. After a crash the status page
// shows the last stage reached, which tells us where the crash happened.
void stage(uint8_t n);

// Restart in a way the crash counter does not count.
void plannedRestart(bool intoSafeMode = false);

// RECOVERY-CHANGE (0B): first 10 minutes of uptime (latched; never true again).
bool golden();

// RECOVERY-CHANGE (0B): hotspot control, used by netwatch only.
void startAP();
void stopAP();

// Wall clock from NTP. Returns false until synced.
bool localTime(int& hour, int& minute);

// ---- RECOVERY-CHANGE (chunk1): restarts, quarantine, persistence hooks (design 8.3, 8.6, 8.7)
enum RestartReason : uint8_t {
  RR_NONE, RR_USER, RR_UPDATE, RR_WIFI, RR_HEAP, RR_MAINT, RR_SAFEMODE, RR_FACTORY,
  RR_CRASH,   // logged at boot after an unplanned reset
  RR_POWER    // logged at boot after a power-on
};
// Carried out from safety::loop() as soon as it is allowed. User-caused reasons (USER, UPDATE,
// SAFEMODE, FACTORY) are allowed at once. WIFI, HEAP and MAINT are automatic: they only restart
// when the build has GQ_AUTO_RESTART=1 and autoRestartAllowed() is true; otherwise they are only logged.
void requestRestart(RestartReason why);
// Called just before a planned restart (main.cpp registers persist::beforeRestart).
void onBeforeRestart(void (*fn)());
// uptime >= 30 min, not in the golden window, no AP client, not updating, not safe mode.
bool autoRestartAllowed();
// The saved game is not loaded for this boot (crash loop with a restored save).
bool saveQuarantined();
// Writes only the RTC stage word (cheap; used every frame for 9, 10 and 11).
void tickMarker(uint8_t stage);

// persist.cpp reports that a game save was loaded this boot (quarantine rule 8.3).
void noteGameRestored();
// The user chose "try again" or "discard" (or restored a backup): the quarantine ends.
void clearQuarantine();
// Time zone: validated (design 7.3). Returns false and changes nothing when invalid.
bool setTimeZone(const char* tz);
void setDepth(int d);
// ms since the last web request (0xFFFFFFFF = never).
uint32_t msSinceRequest();
// Power-on (not a restart): RTC contents were not ours.
bool coldBoot();

// WiFi restart bookkeeping kept in RTC (feeds NetIn.restarts_done and ever_connected).
uint8_t wifiRestarts();
void noteLinkGood();        // link has been up long enough: restarts_done back to 0
bool everConnected();
void noteConnected();

// main.cpp reports health numbers for the status page.
void setHealth(uint16_t fps_x10, uint32_t loopMaxMs);
// main.cpp: hero is asleep (used by the maintenance restart when there is no clock).
void setAsleepFn(bool (*fn)());
// Short text of the last restart reasons for the status page and /info.
const char* restartName(uint8_t reason);

}  // namespace safety
