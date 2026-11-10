// Tiny portability layer so the same game core builds for the ESP8266
// firmware and for the WebAssembly browser preview.
#pragma once

#if defined(ARDUINO_ARCH_ESP8266)
  #include <Arduino.h>
  #include <pgmspace.h>
  #define GC_ROM_ATTR PROGMEM
  #define GC_RD8(p)  pgm_read_byte(p)
  #define GC_RD16(p) pgm_read_word(p)
#else
  #include <stdint.h>
  #include <stddef.h>
  #define GC_ROM_ATTR
  #define GC_RD8(p)  (*(const uint8_t*)(p))
  #define GC_RD16(p) (*(const uint16_t*)(p))
#endif
