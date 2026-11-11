#include <Arduino.h>
namespace x {
void f() { system_update_cpu_freq(80); lcd::sleep(); lcd::wake(); WiFi.setSleepMode(WIFI_NONE_SLEEP); }
}
