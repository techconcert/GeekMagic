// STUB (R3-0F): does nothing. Chunk P1 replaces this body (RTC load/store, /config routes, page).
#include "config.h"
#include "webout.h"   // so the stub already compiles against the page writer P1 will use

namespace config {

static GqConf s_conf;   // zero until begin(); 64 bytes of .bss

void begin(bool safeMode) {
  (void)safeMode;       // P1: no routes in safe mode
  s_conf.magic = GQC_MAGIC;
  s_conf.version = GQC_VERSION;
  s_conf.sleep_min = GQC_DEFAULT_SLEEP_MIN;
  s_conf.wake_min = GQC_DEFAULT_WAKE_MIN;
}
void loop() {}
const GqConf& get() { return s_conf; }
bool isDefault() { return true; }
void statusLine(char* buf, size_t n) { if (n) buf[0] = 0; }

}  // namespace config
