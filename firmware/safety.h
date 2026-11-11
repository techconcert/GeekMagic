// Recovery layer. This is the part that keeps the clock flashable over WiFi
// no matter what the game code does. It must stay independent of the game
// and the display: if either breaks, WiFi + the update page still work.
#pragma once
#include <Arduino.h>

#ifndef GQ_MDNS
#define GQ_MDNS 0   // 1 = answer geekquest.local (about 19 KB of code); the clock shows its IP address either way
#endif

#include <ESP8266WebServer.h>   // ESP8266WebServer is a using-alias in the core: it cannot be forward declared

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

// ---- RECOVERY-CHANGE (chunk1): restarts (design 8.3, 8.6, 8.7)
enum RestartReason : uint8_t {
  RR_NONE, RR_USER, RR_UPDATE, RR_WIFI, RR_HEAP, RR_MAINT, RR_SAFEMODE, RR_UNUSED7,   // 7: was "factory reset" (numbers stay stable)
  RR_CRASH,   // logged at boot after an unplanned reset
  RR_POWER    // logged at boot after a power-on
};
// Carried out from safety::loop() as soon as it is allowed. User-caused reasons (USER, UPDATE,
// SAFEMODE) are allowed at once. WIFI, HEAP and MAINT are automatic: they only restart
// when the build has GQ_AUTO_RESTART=1 (approved in R2) and autoRestartAllowed() is true (never during an
// upload); otherwise they are only logged.
void requestRestart(RestartReason why);
// uptime >= 30 min, not in the golden window, no AP client, not updating, not safe mode.
bool autoRestartAllowed();
// Writes only the RTC stage word (cheap; used every frame for 9, 10 and 11).
void tickMarker(uint8_t stage);

// Time zone: validated (design 7.3). Returns false and changes nothing when invalid.
bool setTimeZone(const char* tz);
void setDepth(int d);
// ms since the last web request (0xFFFFFFFF = never).
uint32_t msSinceRequest();
// Unplanned resets in a row before this boot (store.cpp keeps a suspect saved game aside from 2 on).
unsigned crashes();
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

// ---- RECOVERY-CHANGE (R3-0F): plumbing for the R3 modules (docs/DESIGN-R3.md section 6)
// The one web server. Only config.cpp (lint L12) registers routes on it, and not in safe mode.
ESP8266WebServer& web();
// Why the clock started. BK_POWER: power-on or reset button. BK_CRASH: the previous run ended without a planned restart.
// BK_AUTO: the previous restart was automatic (WiFi, heap, weekly). BK_USER: restart button, update or safe mode request.
enum BootKind : uint8_t { BK_POWER, BK_CRASH, BK_AUTO, BK_USER };
BootKind bootKind();
// Weather fetch bookkeeping kept in two spare RTC words: wxMark(true) before a fetch, wxMark(false) after it. A boot that
// follows an unplanned reset while it was set adds a strike (cleared by power-on or wxClearStrikes()).
void wxMark(bool busy);
uint8_t wxStrikes();
void wxClearStrikes();

}  // namespace safety
