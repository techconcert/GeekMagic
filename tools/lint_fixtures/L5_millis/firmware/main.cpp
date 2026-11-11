#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  safety::begin();
}
void loop() {
  safety::loop();
}
bool g() { return millis() > 5000; }
