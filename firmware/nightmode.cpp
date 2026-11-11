// R3 sleep schedule glue (docs/DESIGN-R3.md 3.4). Display and CPU speed only: WiFi, the web server, /update and the rescue
// hotspot are never touched here (lint L10). The decisions are made by lib/gqsafe/src/sleepfsm.cpp, once per second.
// This is the only file that calls system_update_cpu_freq, lcd::sleep and lcd::wake (lint L12).
#include "nightmode.h"
#include <Arduino.h>
#include <stdio.h>
#include <user_interface.h>
#include "safety.h"
#include "config.h"
#include "st7789.h"
#include "sleepfsm.h"       // lib/gqsafe
#include "gamecore.h"
#include "gq_types.h"

namespace nightmode {

static SleepFsm s_fsm;
static bool s_active = false;      // false in safe mode and without a display: the module then does nothing at all
static bool s_off = false;         // the display is really off (screen state we have carried out)
static bool s_stateKnown = false;
static uint8_t s_state = SF_AWAKE;
static uint8_t s_hero = 255;       // last hero mode handed to the core (255 = none yet)
static uint8_t s_cmd = 0;          // pending command from the page, consumed by the next step
static uint8_t s_wakeSoon = 0;
static uint32_t s_nextStep = 0;
static uint16_t s_wakeMin = 0;     // for the status line

void begin(bool safeMode, bool display) {
  s_active = !safeMode && display;
  if (!s_active) return;
  sleepfsm_init(&s_fsm, millis(), (uint8_t)safety::bootKind());
  s_nextStep = millis();           // the first step runs in the first loop()
}

static void goToSleep() {
  lcd::sleep();                    // backlight off, DISPOFF, SLPIN
  system_update_cpu_freq(80);      // 80 MHz; WiFi keeps running (modem sleep policy untouched)
  s_off = true;
}

static void wakeUp() {
  system_update_cpu_freq(160);     // full speed first: the init sequence and the redraw want it
  lcd::wake();                     // hardware reset + init sequence + backlight, ~0.4 s
  gc_mark_all_dirty();             // main.cpp redraws all strips on the next frame
  s_off = false;
}

void loop() {
  if (!s_active) return;
  const uint32_t now = millis();
  if ((int32_t)(now - s_nextStep) < 0) return;
  s_nextStep = now + 1000;         // at most once a second

  SleepIn in;
  int h, m;
  const bool valid = safety::localTime(h, m);
  in.now_ms = now;
  in.clock_valid = valid ? 1 : 0;
  in.minute = valid ? (uint16_t)(h * 60 + m) : 0;
  const GqConf& c = config::get();
  in.enabled = (c.flags & GQC_F_SLEEP) ? 1 : 0;
  in.sleep_min = c.sleep_min;
  in.wake_min = c.wake_min;
  in.updating = safety::updating() ? 1 : 0;
  in.safe_mode = 0;                // s_active is false in safe mode
  in.hero_abed = gc_query(GQ_Q_HERO_ABED) ? 1 : 0;
  in.cmd = s_cmd;
  s_cmd = 0;

  s_state = sleepfsm_step(&s_fsm, &in);
  s_stateKnown = true;
  s_wakeSoon = s_fsm.wake_soon;
  s_wakeMin = in.wake_min;

  if (s_fsm.hero_mode != s_hero) {                      // the core only needs to hear about changes
    s_hero = s_fsm.hero_mode;
    gc_set_option(GC_OPT_SLEEP_MODE, s_hero);
  }
  if (s_fsm.screen_off && !s_off) goToSleep();
  else if (!s_fsm.screen_off && s_off) wakeUp();
}

bool screenOff() { return s_off; }
bool wakeSoon() { return s_off && s_wakeSoon; }

void command(uint8_t cmd) {
  if (cmd == CMD_WAKE_NOW || cmd == CMD_SLEEP_NOW) s_cmd = cmd;   // anything else is ignored
}

void statusLine(char* buf, size_t n) {
  if (!n) return;
  buf[0] = 0;
  if (!s_active || !s_stateKnown) return;
  const unsigned wm = s_wakeMin;
  if (s_off) snprintf(buf, n, "Asleep (screen off) until %02u:%02u", wm / 60 % 24, wm % 60);
  else if (s_state == SF_BEDTIME) snprintf(buf, n, "Going to bed (screen off soon), wake at %02u:%02u", wm / 60 % 24, wm % 60);
}

}  // namespace nightmode
