// R3 settings page and record (DESIGN-R3.md 5). FINAL interface; R3-0F ships a stub body that does nothing, chunk P1 fills it in.
// Storage is the RTC backend only (RTC user memory blocks 32..47). There is no flash backend in R3.
#pragma once
#include <stddef.h>
#include "gqconf.h"

// P1 sets this to 1 when /config exists: the status page then shows the link (and tools/check_bin.py REQUIRE_CONFIG_PAGE
// must be switched to True in the same commit).
#ifndef GQ_HAVE_CONFIG_PAGE
#define GQ_HAVE_CONFIG_PAGE 0
#endif

namespace config {
// Loads the record from RTC (else defaults) and, unless safeMode, registers the /config routes on safety::web().
void begin(bool safeMode);
void loop();
const GqConf& get();
bool isDefault();                          // the record is the factory default (nothing was ever saved since power-on)
void statusLine(char* buf, size_t n);      // plain text; empty string = nothing to show
}  // namespace config
