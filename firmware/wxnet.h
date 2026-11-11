// R3 live weather client (DESIGN-R3.md 2.6). FINAL interface; R3-0F ships a stub body that does nothing, chunk W1 fills it in.
// This is the only file that may use tcp_*, dns_gethostbyname or pbuf_* (lint L11).
#pragma once
#include <stddef.h>

namespace wxnet {
void begin(bool safeMode);
void loop();                               // called on every iteration of loop(); does its work once per second
// The newest valid reading. wx is a GQ_W_* state 0..6, temp_c10 tenths of a degree C, flags the GC_WXF_* bits.
// False when there is none (not configured, never fetched, or expired).
bool reading(int* wx, int* temp_c10, int* flags);
void kick();                               // settings changed: fetch again soon
void statusLine(char* buf, size_t n);      // plain text; empty string = nothing to show
}  // namespace wxnet
