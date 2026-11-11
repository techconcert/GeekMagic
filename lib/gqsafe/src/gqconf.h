// R3 settings record (docs/DESIGN-R3.md 5.2). 64 bytes, little-endian, no padding. Kept in RTC memory blocks 32..47 only:
// R3 has no flash backend (R3 addendum item 3), so there is no flash copy of this record.
// R3-0F created this header with the final layout so firmware/config.h could be final; chunk P1 owns it from here on and adds
// the functions (gqc_check, gqc_sanitize, validation) below the record.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define GQC_MAGIC       0x31435147u   // "GQC1" little-endian
#define GQC_VERSION     1
#define GQC_F_WEATHER   1u            // live weather on
#define GQC_F_FAHRENHEIT 2u
#define GQC_F_SLEEP     4u            // sleep schedule on
#define GQC_DEFAULT_SLEEP_MIN 1380    // 23:00
#define GQC_DEFAULT_WAKE_MIN  420     // 07:00

typedef struct {
  uint32_t magic;        //  0  GQC_MAGIC
  uint32_t crc;          //  4  CRC32 of bytes 8..63
  uint8_t  version;      //  8  1
  uint8_t  flags;        //  9  GQC_F_*
  uint16_t seq;          // 10  +1 per change
  int16_t  lat_c;        // 12  hundredths of a degree
  int16_t  lon_c;        // 14
  uint16_t sleep_min;    // 16  0..1439
  uint16_t wake_min;     // 18
  char     place[24];    // 20  NUL-terminated label
  uint8_t  reserved[20]; // 44  zero
} GqConf;

#ifdef __cplusplus
static_assert(sizeof(GqConf) == 64, "GqConf must be 64 bytes (RTC blocks 32..47)");
static_assert(offsetof(GqConf, crc) == 4 && offsetof(GqConf, version) == 8 && offsetof(GqConf, lat_c) == 12 &&
              offsetof(GqConf, sleep_min) == 16 && offsetof(GqConf, place) == 20 && offsetof(GqConf, reserved) == 44,
              "GqConf layout");
#else
_Static_assert(sizeof(GqConf) == 64, "GqConf must be 64 bytes (RTC blocks 32..47)");
#endif
