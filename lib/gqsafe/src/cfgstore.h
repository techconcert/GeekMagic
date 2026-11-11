// Storage of the settings record behind a small backend interface (docs/DESIGN-R3.md 5.3, R3 addendum item 3).
// R3 has ONE backend: RTC user memory (implemented in firmware/config.cpp). A flash backend may be added later by writing
// another CfgBackend; nothing here touches flash. Pure C++: no Arduino, no heap.
#pragma once
#include <stdint.h>
#include "gqconf.h"

typedef struct CfgBackend {
  const char* name;                                              // "rtc"
  int (*read)(void* ctx, uint8_t* buf, uint32_t n);              // fill n bytes, return 1 on success
  int (*write)(void* ctx, const uint8_t* buf, uint32_t n);       // store n bytes, return 1 on success
  void* ctx;
} CfgBackend;

#ifdef __cplusplus
// 1 = a valid record was stored (*out holds it, sanitized). 0 = nothing usable (read failed, bad magic/version/CRC):
// *out holds the factory defaults.
int cfg_load(const CfgBackend* b, GqConf* out);
// Seals a sanitized copy of *c, writes it, reads it back and compares. 1 = stored and verified. On 0 the stored data may
// be damaged; the next cfg_load then falls back to the defaults. *c is updated to the sealed copy on success.
int cfg_save(const CfgBackend* b, GqConf* c);
#endif
