// STUB (R3-0F): does nothing and makes no network request. Chunk W1 replaces this body.
#include "wxnet.h"

namespace wxnet {

void begin(bool safeMode) { (void)safeMode; }
void loop() {}
bool reading(int* wx, int* temp_c10, int* flags) { (void)wx; (void)temp_c10; (void)flags; return false; }
void kick() {}
void statusLine(char* buf, size_t n) { if (n) buf[0] = 0; }

}  // namespace wxnet
