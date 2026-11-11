#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  lcd::begin(); safety::begin();
}
void loop() {
  safety::loop();
}
