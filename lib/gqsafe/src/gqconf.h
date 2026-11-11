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

// ---------------------------------------------------------------------------------------------------------------------
// Chunk P1: pure logic (no Arduino, no heap, no floating point). Host-tested in test/test_config.cpp.
// ---------------------------------------------------------------------------------------------------------------------
#define GQC_PLACE_MAX 23              // label characters (place[24] holds the NUL)

// Result codes of gqc_validate (0 = ok). The firmware keeps one message per code in flash (config.cpp, same order).
enum {
  GQC_OK = 0,
  GQC_E_LAT,          // latitude is not a number in -90..90
  GQC_E_LON,          // longitude is not a number in -180..180
  GQC_E_COORD_PAIR,   // only one of latitude / longitude was given
  GQC_E_PLACE,        // label too long or has characters outside A-Z a-z 0-9 space , . ' -
  GQC_E_SLEEP_TIME,   // sleep time is not HH:MM
  GQC_E_WAKE_TIME,    // wake time is not HH:MM
  GQC_E_SAME_TIME,    // sleep time equals wake time
  GQC_E_NO_COORDS,    // live weather needs coordinates
  GQC_E_COUNT
};

// Form input, all fields optional: a NULL text keeps the current value, a flag of -1 keeps the current value.
// Texts are NUL-terminated and not copied; any length is safe.
typedef struct {
  int8_t      weather;      // 1 on, 0 off, -1 keep
  int8_t      fahrenheit;   // 1 Fahrenheit, 0 Celsius, -1 keep
  int8_t      sleep_on;     // 1 on, 0 off, -1 keep
  const char* lat;          // "38.72", "-9,14", ...; "" together with an empty lon clears both
  const char* lon;
  const char* place;
  const char* sleep_time;   // "HH:MM"
  const char* wake_time;
} GqcInput;

#ifdef __cplusplus
void gqc_defaults(GqConf* c);                 // factory record, sealed
void gqc_seal(GqConf* c);                     // magic, version, reserved = 0, crc
int  gqc_check(const GqConf* c);              // 0 = valid record. 1 magic, 2 version, 3 crc
// Clamps every field into its range (lat/lon, minutes modulo a day, flags, label characters), zeroes reserved, seals.
void gqc_sanitize(GqConf* c);
// Same-content test that ignores seq and crc (did the owner change anything?). 1 = same.
int  gqc_same(const GqConf* a, const GqConf* b);

// Text to hundredths of a degree without floating point. Accepts optional spaces, an optional sign, digits, one '.' or ','
// and up to 6 decimals (rounded to hundredths, half away from zero, judged on the third decimal). Returns 1 and writes
// *out when the value is within +-max_deg degrees, else 0 and *out is untouched. 40 digits, empty text, "abc" are refused.
int  gqc_parse_coord(const char* text, int max_deg, int16_t* out);
// "HH:MM" (exactly two digits each, 00:00..23:59) to minutes of the day, or -1. "7:5" is refused.
int  gqc_parse_time(const char* text);
// Label rule: at most GQC_PLACE_MAX characters from A-Z a-z 0-9 space , . ' - (empty is allowed).
int  gqc_place_ok(const char* text);
// Formatting for the page: "-9.14" (buf >= 8), "07:05" (buf >= 6). Return the length.
int  gqc_format_coord(int16_t c, char* buf);
int  gqc_format_time(uint16_t minutes, char* buf);

// Validates the whole form first; `out` is written only when the result is GQC_OK (then it is sanitized and sealed, seq
// unchanged: the caller bumps it when gqc_same says something changed). An invalid value changes nothing.
int  gqc_validate(const GqConf* cur, const GqcInput* in, GqConf* out);
#endif
