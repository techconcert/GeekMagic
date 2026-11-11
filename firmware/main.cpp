// GeekQuest firmware entry point.
// Order matters: the recovery layer (WiFi + update page) starts before the
// display or the game, so a bug in either can never lock you out.
#include <Arduino.h>
#include "safety.h"
#include "st7789.h"
#include "version.h"
#include "gamecore.h"

#ifndef GQ_SCENE
#define GQ_SCENE GC_SCENE_TEST
#endif

alignas(4) static uint16_t s_strip[GC_STRIP_PIXELS];   // one 240x16 strip, 7.5 KB
static bool s_display = false;
static uint32_t s_lastTick = 0, s_fpsT = 0, s_frames = 0, s_lastStatus = 0;

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
    gc_set_status(1, "GEEKQUEST.LOCAL");
    gc_set_status(2, "");
  }
  snprintf(buf, sizeof(buf), "FW %s", GQ_VERSION);
  gc_set_status(3, buf);

#if defined(GQ_HAVE_SET_NET)
  // GC_NET_ONLINE 0, GC_NET_OFFLINE 1, GC_NET_AP_ONLY 2 (defined by the game core, design 10.2)
  gc_set_net(safety::staConnected() ? 0 : (safety::apActive() ? 2 : 1));
#endif

  int h, m;
  if (safety::localTime(h, m)) gc_set_clock(h, m, 1);
  else gc_set_clock(0, 0, 0);
}

void setup() {
  Serial.begin(115200);
  safety::begin();                       // 1. WiFi, rescue hotspot, update page
  safety::stage(1);

  if (!safety::noDisplay()) {            // 2. display (skipped after 6 crashes)
    safety::stage(2);
    lcd::begin();
    s_display = true;
    safety::stage(3);
  }
  if (safety::safeMode()) gc_init(GC_SCENE_INFO, 1, 1);   // 3. game, unless in safe mode
  else gc_init(GQ_SCENE, ESP.random(), 1);
  gc_set_depth(safety::depth());
  safety::stage(4);
  updateStatus();
  s_lastTick = s_fpsT = millis();
  safety::stage(5);
}

void loop() {
  safety::loop();
  if (!s_display || safety::updating()) { delay(2); return; }

  gc_set_depth(safety::depth());
  uint32_t now = millis();
  gc_tick(now - s_lastTick);
  s_lastTick = now;

  if (now - s_lastStatus > 1000) { s_lastStatus = now; updateStatus(); }

  uint32_t mask = gc_dirty_mask();
  gc_clear_dirty();
  static bool firstFrame = true;
  for (int s = 0; s < GC_ROWS; s++) {
    if (!(mask & (1u << s))) continue;
    if (firstFrame) safety::stage(6);
    gc_render_strip(s, s_strip);
    if (firstFrame) safety::stage(7);
    lcd::pushRows(s * GC_STRIP_H, GC_STRIP_H, s_strip);
    if (firstFrame) safety::stage(8);
  }
  if (firstFrame) { firstFrame = false; safety::stage(9); }

  s_frames++;
  if (now - s_fpsT >= 1000) {
    gc_set_fps((int)(s_frames * 10000UL / (now - s_fpsT)));
    s_frames = 0;
    s_fpsT = now;
  }

  // ~30 fps cap; delay() also gives the WiFi stack its time.
  uint32_t spent = millis() - now;
  delay(spent < 33 ? 33 - spent : 1);
}
