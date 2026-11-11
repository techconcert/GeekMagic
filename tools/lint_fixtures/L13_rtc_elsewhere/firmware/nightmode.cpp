#include <Arduino.h>
namespace x {
void f() { uint32_t w; ESP.rtcUserMemoryWrite(48, &w, 4); }
}
