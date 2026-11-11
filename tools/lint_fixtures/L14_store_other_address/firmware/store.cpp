#include <Arduino.h>
namespace store {
static const uint32_t BASE = 0x3FA000UL;
static const uint32_t MAX_IMAGE = 0x7D000UL;
static bool s_on = false;
void begin(bool safeMode) { s_on = !safeMode && ESP.getSketchSize() <= MAX_IMAGE; }
void loop() {
  if (!s_on || safety::updating()) return;
  ESP.flashEraseSector(BASE / 4096);
  ESP.flashWrite(BASE, 0, 0);
  ESP.flashRead(BASE, 0, 0);
}
}
