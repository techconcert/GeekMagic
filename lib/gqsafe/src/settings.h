// Settings record (time zone, brightness, colour depth, card period, flags), kept in RTC memory. Pure C++.
//
// GqSettings, 48 bytes, owned by the firmware:
//   u32 magic "GQST" | u32 crc32 over bytes 8..47 | u8 version | u8 brightness | u8 depth |
//   u8 flags (bit 0 maintenance restart, bit 1 night dimming) | u8 card_period | char tz[35]
#pragma once
#include <stdint.h>

#define GQS_SETTINGS_BYTES 48u
#define GQS_MAGIC          0x54535147u     // "GQST"
#define GQS_VERSION        1u
#define GQS_FLAG_MAINT     1u
#define GQS_FLAG_NIGHT     2u
#define GQS_TZ_MAX         34u             // characters; tz[35] holds the NUL
#define GQS_CARD_PERIOD_MAX 240u

typedef struct {
  uint32_t magic;
  uint32_t crc;
  uint8_t  version;
  uint8_t  brightness;      // 0..100
  uint8_t  depth;           // 0..100
  uint8_t  flags;
  uint8_t  card_period;     // minutes, 0 = off
  char     tz[35];
} GqSettings;

static_assert(sizeof(GqSettings) == GQS_SETTINGS_BYTES, "GqSettings must be 48 bytes");

void gqs_defaults(GqSettings* s);            // sealed (magic and crc set)
void gqs_seal(GqSettings* s);                // sets magic, version and crc
// 0 = acceptable. Otherwise 1 magic, 2 version, 3 crc, 4 range, 5 time zone.
int  gqs_check(const GqSettings* s);
// Time zone text rule of design 7.3: length 3..34, characters A-Z a-z 0-9 < > + - : , . /
int  gqs_tz_valid(const char* tz);

