// STUB (R3-0F): does nothing; the screen is never switched off. Chunk S1 replaces this body.
#include "nightmode.h"

namespace nightmode {

void begin(bool safeMode, bool display) { (void)safeMode; (void)display; }
void loop() {}
bool screenOff() { return false; }
bool wakeSoon() { return false; }
void command(uint8_t cmd) { (void)cmd; }
void statusLine(char* buf, size_t n) { if (n) buf[0] = 0; }

}  // namespace nightmode
