// R3 sleep schedule decisions (docs/DESIGN-R3.md 3.2). Pure C++: no Arduino includes, no heap, no libc, no floating point.
// Stepped once per second by firmware/nightmode.cpp. The window is a LEVEL evaluated from the local minute of day on every
// step, never an edge, so a time-zone change, a daylight-saving jump or a late NTP sync corrects itself within one step.
// Every time comparison is `now - since >= period` in uint32_t (49.7 day millis() wrap safe).
#pragma once
#include <stdint.h>

enum { SF_AWAKE = 0, SF_BEDTIME = 1, SF_ASLEEP = 2 };          // BEDTIME: hero sent to bed, screen still on
enum { SF_CMD_NONE = 0, SF_CMD_WAKE = 1, SF_CMD_SLEEP = 2 };   // same values as nightmode::command()
enum { SF_BK_POWER = 0, SF_BK_CRASH = 1, SF_BK_AUTO = 2, SF_BK_USER = 3 };   // same values as safety::BootKind

#define SF_BED_LEAD_MIN     5u        // hero goes to bed this long before the window
#define SF_ABED_MS      10000u        // screen off after the hero has lain in bed this long
#define SF_MAX_WAIT_MS 120000u        // ... and at the latest this long after sleep time, whatever the hero does
#define SF_GRACE_MS    600000u        // boot grace: screen stays lit (golden window)
#define SF_HOLD_MS      60000u        // after an automatic restart: dark for at most this long while the time is unknown
#define SF_FORCE_MAX_MIN  720u        // "Sleep now" lasts until the next wake time, at most 12 h
#define SF_WAKE_SOON_MIN    5u

typedef struct {
  uint32_t now_ms;
  uint16_t minute;        // local minute of day 0..1439 (ignored unless clock_valid)
  uint8_t  clock_valid;   // NTP has synced since boot
  uint8_t  enabled;       // schedule switched on in the settings
  uint16_t sleep_min;     // 0..1439
  uint16_t wake_min;      // 0..1439, must differ from sleep_min (else the schedule counts as off)
  uint8_t  updating;
  uint8_t  safe_mode;
  uint8_t  hero_abed;     // GQ_Q_HERO_ABED
  uint8_t  cmd;           // SF_CMD_*, consumed by this step
} SleepIn;

typedef struct {
  uint32_t start;         // init time
  uint32_t due_since, abed_since, force_until, last_ms;
  uint16_t last_min;
  uint8_t  boot_kind, state, hero_mode, screen_off, wake_soon;
  uint8_t  grace_over, hold_done, ovr, ovr_seen_in, force_active, due, abed, have_min;
} SleepFsm;

void    sleepfsm_init(SleepFsm* f, uint32_t now_ms, uint8_t boot_kind);
// Returns the state (SF_*). Outputs are in the struct: hero_mode (0 built-in night, 1 stay up, 2 go to bed),
// screen_off, wake_soon.
uint8_t sleepfsm_step(SleepFsm* f, const SleepIn* in);

// Window helpers (exported for the tests). A window needs 0 <= s,w < 1440 and s != w.
uint8_t sleepfsm_in_window(uint16_t m, uint16_t s, uint16_t w);   // level test of 3.2
uint8_t sleepfsm_in_lead(uint16_t m, uint16_t s, uint16_t w);     // not in the window, within 5 minutes before it
