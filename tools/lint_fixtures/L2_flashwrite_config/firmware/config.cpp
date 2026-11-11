#include <Arduino.h>
namespace x {
void f() { uint32_t b[4]; ESP.flashWrite(0x3FA000, b, 16); }
}
