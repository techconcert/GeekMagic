#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  safety::begin();
}
void loop() {
  delay(1); safety::loop();
}
