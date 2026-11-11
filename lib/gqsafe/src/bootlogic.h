// Decisions of the recovery layer that need no hardware (design 8.3, 8.6, 8.7).
// Pure C++, host-tested (test_persist.cpp). firmware/safety.cpp reads and
// writes the RTC words and calls these.
#pragma once
#include <stdint.h>

// ---- crash counting (8.3)
#define BOOT_CRASHES_SAFE        3u
#define BOOT_CRASHES_NO_DISPLAY  6u
#define BOOT_SLOW_SAFE           5u
#define BOOT_COUNTER_CAP       1000u

typedef struct {
  uint32_t crashes;        // unplanned resets in a row, cleared after 60 s of uptime
  uint32_t slow;           // unplanned resets, cleared after 6 h of uptime
  uint32_t planned;        // the previous restart was ours
  uint32_t force_safe;     // the user asked for safe mode
} BootCounters;

typedef struct {
  uint8_t safe, no_display;
} BootDecision;

// Call once per boot with the counters read from RTC (zeroed if RTC was invalid).
// `cold` = power-on, reset button or invalid RTC. Updates the counters for storing
// (planned and force_safe are cleared) and fills the decision.
void boot_decide(BootCounters* c, int cold, BootDecision* d);

// ---- heap watchdog (8.6): call every 10 s
#define HW_WARN_FREE      12288u
#define HW_RESTART_FREE    8192u
#define HW_RESTART_BLOCK   4096u
#define HW_HOLD_MS        60000u

typedef struct {
  uint32_t low_since, crit_since, min_free;
  uint8_t  low, crit, primed;
} HeapWatch;

enum { HW_WARN = 1, HW_RESTART = 2 };
void    heapwatch_init(HeapWatch* w);
// Returns HW_WARN when free heap has been under 12 KB for 60 s without interruption,
// HW_RESTART when free < 8 KB or largest block < 4 KB for 60 s. Also tracks min_free.
uint8_t heapwatch_step(HeapWatch* w, uint32_t now_ms, uint32_t free_heap, uint32_t max_block);

// ---- maintenance restart (8.7)
#define MAINT_UPTIME_MS  (7UL * 24 * 3600 * 1000)
#define MAINT_QUIET_MS   (5UL * 60 * 1000)

typedef struct {
  uint8_t  enabled;        // setting
  uint32_t uptime_ms;
  uint8_t  clock_valid;
  uint8_t  hour, minute;
  uint8_t  hero_asleep;    // used when there is no clock
  uint8_t  ap_clients;
  uint8_t  updating;
  uint32_t since_http_ms;  // ms since the last request, 0xFFFFFFFF = never
} MaintIn;

int maint_due(const MaintIn* in);
