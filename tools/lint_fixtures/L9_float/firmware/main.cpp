#include <Arduino.h>
void setup() {
  Serial.begin(115200);
  safety::begin();
}
void loop() {
  safety::loop();
}
void g(char* b) { snprintf(b, 8, "%.1f", 1.5); }
