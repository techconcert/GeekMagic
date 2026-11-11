// GeekQuest firmware entry point.
// Order matters: the recovery layer (WiFi + update page) starts before the
// display or the game, so a bug in either can never lock you out.
#include <Arduino.h>
#include <time.h>
#include "safety.h"
#include "st7789.h"
#include "version.h"
#include "gamecore.h"
// RECOVERY-CHANGE (R2): settings in RTC memory (no game saving) and the game's query keys (public, design 10.3)
#include "prefs.h"
#include "gq_types.h"

#ifndef GQ_SCENE
#define GQ_SCENE GC_SCENE_TEST
#endif

alignas(4) static uint16_t s_strip[GC_STRIP_PIXELS];   // one 240x16 strip, 7.5 KB
static bool s_display = false;
static uint32_t s_lastTick = 0, s_fpsT = 0, s_frames = 0, s_lastStatus = 0;
// RECOVERY-CHANGE (chunk1): loop timing for the status page
static uint32_t s_loopMax = 0, s_loopMaxT = 0, s_loopMaxShown = 0;
static uint16_t s_fps10 = 0;

static bool heroAsleep() { return gc_query(GQ_Q_HERO_ACT) == GQ_ACT_SLEEP; }

static void updateStatus() {
  char buf[32];
  String ip = safety::ipString();
  if (ip.length()) snprintf(buf, sizeof(buf), "IP %s", ip.c_str());
  else snprintf(buf, sizeof(buf), "WIFI: NOT CONNECTED");
  gc_set_status(0, buf);
  if (safety::apActive()) {
    snprintf(buf, sizeof(buf), "AP %s", safety::apName());
    gc_set_status(1, buf);
    snprintf(buf, sizeof(buf), "PASS %s", safety::apPassword());
    gc_set_status(2, buf);
  } else {
    gc_set_status(1, "");
    gc_set_status(2, "");
  }
  snprintf(buf, sizeof(buf), "FW %s", GQ_VERSION);
  gc_set_status(3, buf);

  // RECOVERY-CHANGE (chunk1): gc_set_net exists since chunk 0 (the old GQ_HAVE_SET_NET guard never fired)
  gc_set_net(safety::staConnected() ? GC_NET_ONLINE : (safety::apActive() ? GC_NET_AP_ONLY : GC_NET_OFFLINE));

  // date (design 5.1, 7.3)
  int h, m;
  if (safety::localTime(h, m)) {
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    gc_set_date(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_wday, (uint32_t)now);
    gc_set_clock_quality(0);
    gc_set_clock(h, m, 1);
  } else {
    gc_set_clock(0, 0, 0);
  }
}

void setup() {
  Serial.begin(115200);
  safety::begin();                       // 1. WiFi, rescue hotspot, update page
  safety::stage(1);
  prefs::begin();                        // RECOVERY-CHANGE (R2): settings from RTC memory, else defaults; the game is never saved
  safety::setAsleepFn(heroAsleep);

  if (!safety::noDisplay()) {            // 2. display (skipped after 6 crashes)
    safety::stage(2);
    lcd::begin();
    s_display = true;
    safety::stage(3);
  }
  if (safety::safeMode()) gc_init(GC_SCENE_INFO, 1, 1);   // 3. game, unless in safe mode
  else gc_init(GQ_SCENE, ESP.random(), 1);
  prefs::apply(s_display, !safety::safeMode());   // brightness, card period, night dimming
  gc_set_depth(safety::depth());
  safety::stage(4);
  updateStatus();
  s_lastTick = s_fpsT = millis();
  safety::stage(5);
}

void loop() {
  safety::loop();
  {                                      // RECOVERY-CHANGE (chunk1): longest gap between loop iterations (includes the 33 ms pacing)
    static uint32_t prevEntry = 0;
    uint32_t e = millis();
    if (prevEntry) {
      if (e - prevEntry > s_loopMax) s_loopMax = e - prevEntry;
      if (e - s_loopMaxT >= 60000) { s_loopMaxShown = s_loopMax; s_loopMax = 0; s_loopMaxT = e; }
      safety::setHealth(s_fps10, s_loopMaxShown > s_loopMax ? s_loopMaxShown : s_loopMax);
    }
    prevEntry = e;
  }
  if (!s_display || safety::updating()) { delay(2); return; }

  gc_set_depth(safety::depth());
  uint32_t now = millis();
  safety::tickMarker(10);                // RECOVERY-CHANGE (chunk1): stage 10 = game tick
  gc_tick(now - s_lastTick);
  s_lastTick = now;

  if (now - s_lastStatus > 1000) { s_lastStatus = now; updateStatus(); }

  uint32_t mask = gc_dirty_mask();
  gc_clear_dirty();
  static bool firstFrame = true;
  for (int s = 0; s < GC_ROWS; s++) {
    if (!(mask & (1u << s))) continue;
    if (firstFrame) safety::stage(6);
    safety::tickMarker(11);              // RECOVERY-CHANGE (chunk1): stage 11 = render
    gc_render_strip(s, s_strip);
    if (firstFrame) safety::stage(7);
    lcd::pushRows(s * GC_STRIP_H, GC_STRIP_H, s_strip);
    if (firstFrame) safety::stage(8);
  }
  if (firstFrame) { firstFrame = false; safety::stage(9); }
  else safety::tickMarker(9);            // RECOVERY-CHANGE (chunk1): stage 9 = frame done

  s_frames++;
  if (now - s_fpsT >= 1000) {
    s_fps10 = (uint16_t)(s_frames * 10000UL / (now - s_fpsT));
    gc_set_fps((int)s_fps10);
    s_frames = 0;
    s_fpsT = now;
  }

  // ~30 fps cap; delay() also gives the WiFi stack its time.
  uint32_t spent = millis() - now;
  delay(spent < 33 ? 33 - spent : 1);
}
