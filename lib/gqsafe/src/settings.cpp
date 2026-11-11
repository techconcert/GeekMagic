#include "settings.h"
#include "gq_crc32.h"

static uint32_t settingsCrc(const GqSettings* s) {
  return gq_crc32((const uint8_t*)s + 8, GQS_SETTINGS_BYTES - 8);
}

void gqs_seal(GqSettings* s) {
  s->magic = GQS_MAGIC;
  s->version = (uint8_t)GQS_VERSION;
  s->tz[sizeof(s->tz) - 1] = 0;
  s->crc = settingsCrc(s);
}

void gqs_defaults(GqSettings* s) {
  uint8_t* p = (uint8_t*)s;
  for (uint32_t i = 0; i < GQS_SETTINGS_BYTES; i++) p[i] = 0;
  s->brightness = 80;
  s->depth = 40;
  s->flags = GQS_FLAG_MAINT;
  s->card_period = 10;
  const char dflt[] = "<-03>3";
  for (uint32_t i = 0; i < sizeof(dflt); i++) s->tz[i] = dflt[i];
  gqs_seal(s);
}

int gqs_tz_valid(const char* tz) {
  uint32_t n = 0;
  for (; n <= GQS_TZ_MAX && tz[n]; n++) {
    char c = tz[n];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '<' || c == '>' || c == '+' || c == '-' || c == ':' || c == ',' || c == '.' || c == '/';
    if (!ok) return 0;
  }
  return n >= 3 && n <= GQS_TZ_MAX && tz[n] == 0;
}

int gqs_check(const GqSettings* s) {
  if (s->magic != GQS_MAGIC) return 1;
  if (s->version != GQS_VERSION) return 2;
  if (s->crc != settingsCrc(s)) return 3;
  if (s->brightness > 100 || s->depth > 100 || (s->flags & ~3u) || s->card_period > GQS_CARD_PERIOD_MAX) return 4;
  if (s->tz[sizeof(s->tz) - 1] != 0 || !gqs_tz_valid(s->tz)) return 5;
  return 0;
}

void gqs_backup_pack(const GqSettings* s, const uint8_t game[GQS_GAME_BYTES], uint8_t out[GQS_BACKUP_BYTES]) {
  const uint8_t* sp = (const uint8_t*)s;
  for (uint32_t i = 0; i < GQS_SETTINGS_BYTES; i++) out[i] = sp[i];
  for (uint32_t i = 0; i < GQS_GAME_BYTES; i++) out[GQS_SETTINGS_BYTES + i] = game[i];
}

void gqs_backup_split(const uint8_t in[GQS_BACKUP_BYTES], GqSettings* s, uint8_t game[GQS_GAME_BYTES]) {
  uint8_t* sp = (uint8_t*)s;
  for (uint32_t i = 0; i < GQS_SETTINGS_BYTES; i++) sp[i] = in[i];
  for (uint32_t i = 0; i < GQS_GAME_BYTES; i++) game[i] = in[GQS_SETTINGS_BYTES + i];
}
