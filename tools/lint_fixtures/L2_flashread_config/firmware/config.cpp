#include <Arduino.h>
namespace x {
void f() { uint32_t b[4]; ESP.flashRead(0xFF000, b, 16); }
}
