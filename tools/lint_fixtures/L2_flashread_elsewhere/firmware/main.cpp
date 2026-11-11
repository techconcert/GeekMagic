#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  safety::begin();
}
void loop() {
  safety::loop();
}
void g() { alignas(4) unsigned b[4]; ESP.flashRead(0x3FA000, b, 16); }
