// Host shim of just enough Arduino API to compile and run firmware/config.cpp (scratch tool, not part of the repo).
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <string>
#include <map>
#include <vector>
#define PROGMEM
#define PSTR(x) (x)
typedef const char* PGM_P;
class __FlashStringHelper;
#define F(x) ((const __FlashStringHelper*)(x))
#define strlen_P strlen
#define memcpy_P memcpy
#define vsnprintf_P vsnprintf
#define snprintf_P snprintf
struct String : std::string {
  String() {}
  String(const char* s) : std::string(s) {}
  String(const __FlashStringHelper* s) : std::string((const char*)s) {}
  unsigned length() const { return (unsigned)size(); }
  bool operator==(const char* o) const { return compare(o) == 0; }
};
struct EspClass {
  uint32_t heap = 20000;
  uint32_t rtc[128];
  int failRtcWrite = 0;
  uint32_t getFreeHeap() { return heap; }
  bool rtcUserMemoryRead(uint32_t off, uint32_t* d, size_t sz) { if (off + sz / 4 > 128 || (off & 0) ) return false; memcpy(d, rtc + off, sz); return true; }
  bool rtcUserMemoryWrite(uint32_t off, uint32_t* d, size_t sz) { if (failRtcWrite || off < 32 || off + sz / 4 > 128) return false; memcpy(rtc + off, d, sz); return true; }
};
extern EspClass ESP;
