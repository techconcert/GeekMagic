// Chunked page output shared by the recovery pages (safety.cpp) and the R3 pages (config.cpp).
// RECOVERY-CHANGE (R3-0F): the `Out` writer moved here from safety.cpp. Behaviour is unchanged: one 480-byte buffer,
// pages are streamed from flash or from this buffer, no String of a page is ever built. The only difference is that
// the writer is told which server to send to (safety::web()) instead of using a file-static object.
#pragma once
#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <stdarg.h>

namespace safety {
// Shared page plumbing, defined in safety.cpp (the page head and tail live there, once).
// beginPage(): 200 + chunked + <head> and style; endPage(): tail and end of the chunks.
void beginChunked(int code, const __FlashStringHelper* type);
void endChunked();
void beginPage();
void endPage();
// Every page handler calls this first: notes the request for the 2 s weather rule, the maintenance restart and the WiFi ladder.
void touch();
}  // namespace safety

namespace webout {

// Small buffered writer for chunked pages.
struct Out {
  ESP8266WebServer& server;
  char b[480];
  uint16_t n;
  explicit Out(ESP8266WebServer& srv) : server(srv), n(0) {}
  void flush() { if (n) { server.sendContent_P(b, n); n = 0; } }
  void put(const char* t, size_t l) {
    if (l > sizeof(b) - n) { flush(); if (l > sizeof(b)) { server.sendContent_P(t, l); return; } }
    memcpy(b + n, t, l);
    n += (uint16_t)l;
  }
  void s(const char* t) { put(t, strlen(t)); }                               // RAM string, raw
  void p(PGM_P t) { size_t l = strlen_P(t); if (l > sizeof(b) - n) { flush(); server.sendContent_P(t, l); } else { memcpy_P(b + n, t, l); n += (uint16_t)l; } }
  void esc(const char* t) {                                                  // RAM string, HTML escaped
    for (; *t; t++) {
      const char* r = nullptr;
      if (*t == '<') r = "&lt;"; else if (*t == '>') r = "&gt;"; else if (*t == '&') r = "&amp;"; else if (*t == '\'') r = "&#39;"; else if (*t == '"') r = "&quot;";
      if (r) put(r, strlen(r)); else put(t, 1);
    }
  }
  // printf with a flash format, straight into the buffer (no second stack buffer). One call may produce up to sizeof(b) - 1 characters.
  void f(PGM_P fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    size_t room = sizeof(b) - n;
    int l = vsnprintf_P(b + n, room, fmt, ap);
    if (l >= 0 && (size_t)l >= room) {            // does not fit behind what is buffered: send that first, format again
      flush();
      l = vsnprintf_P(b, sizeof(b), fmt, ap2);
    }
    va_end(ap2);
    va_end(ap);
    if (l > 0) n = (uint16_t)(n + (size_t)l < sizeof(b) ? n + (size_t)l : sizeof(b) - 1);
  }
};

}  // namespace webout
