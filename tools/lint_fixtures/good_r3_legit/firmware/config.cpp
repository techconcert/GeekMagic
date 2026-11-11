#include <Arduino.h>
namespace x {
void f() { safety::web().on("/config", 0); uint32_t w; ESP.rtcUserMemoryRead(32, &w, 4); }
}
