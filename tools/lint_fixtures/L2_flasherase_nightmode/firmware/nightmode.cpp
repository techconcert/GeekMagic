#include <Arduino.h>
namespace x {
void f() { ESP.flashEraseSector(0xFF); }
}
