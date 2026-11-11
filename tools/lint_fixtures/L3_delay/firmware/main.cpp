#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  safety::begin();
}
void loop() {
  safety::loop();
}
void g() { delay(501); }
