// R3 settings record logic (docs/DESIGN-R3.md 5.1, 5.2). Pure C++: no Arduino, no heap, no floating point.
#include "gqconf.h"
#include "gq_crc32.h"

static uint32_t confCrc(const GqConf* c) { return gq_crc32((const uint8_t*)c + 8, sizeof(GqConf) - 8); }

void gqc_seal(GqConf* c) {
  c->magic = GQC_MAGIC;
  c->version = GQC_VERSION;
  for (uint32_t i = 0; i < sizeof(c->reserved); i++) c->reserved[i] = 0;
  c->crc = confCrc(c);
}

void gqc_defaults(GqConf* c) {
  uint8_t* p = (uint8_t*)c;
  for (uint32_t i = 0; i < sizeof(GqConf); i++) p[i] = 0;
  c->sleep_min = GQC_DEFAULT_SLEEP_MIN;
  c->wake_min = GQC_DEFAULT_WAKE_MIN;
  gqc_seal(c);
}

int gqc_check(const GqConf* c) {
  if (c->magic != GQC_MAGIC) return 1;
  if (c->version != GQC_VERSION) return 2;
  if (c->crc != confCrc(c)) return 3;
  return 0;
}

static int placeChar(char ch) {
  return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == ',' || ch == '.' ||
         ch == '\'' || ch == '-';
}

void gqc_sanitize(GqConf* c) {
  c->flags &= (uint8_t)(GQC_F_WEATHER | GQC_F_FAHRENHEIT | GQC_F_SLEEP);
  if (c->lat_c < -9000) c->lat_c = -9000;
  if (c->lat_c > 9000) c->lat_c = 9000;
  if (c->lon_c < -18000) c->lon_c = -18000;
  if (c->lon_c > 18000) c->lon_c = 18000;
  if (c->sleep_min > 1439) c->sleep_min = (uint16_t)(c->sleep_min % 1440);
  if (c->wake_min > 1439) c->wake_min = (uint16_t)(c->wake_min % 1440);
  c->place[sizeof(c->place) - 1] = 0;
  uint32_t n = 0;
  while (n < sizeof(c->place) - 1 && c->place[n]) {
    if (!placeChar(c->place[n])) c->place[n] = ' ';
    n++;
  }
  for (uint32_t i = n; i < sizeof(c->place); i++) c->place[i] = 0;   // nothing after the NUL
  gqc_seal(c);
}

int gqc_same(const GqConf* a, const GqConf* b) {
  const uint8_t* x = (const uint8_t*)a;
  const uint8_t* y = (const uint8_t*)b;
  if (a->flags != b->flags) return 0;
  for (uint32_t i = 12; i < sizeof(GqConf); i++)    // from lat_c on; bytes 8..11 are version, flags, seq
    if (x[i] != y[i]) return 0;
  return 1;
}

int gqc_parse_coord(const char* t, int max_deg, int16_t* out) {
  if (!t) return 0;
  while (*t == ' ') t++;
  int neg = 0;
  if (*t == '-' || *t == '+') { neg = (*t == '-'); t++; }
  uint32_t whole = 0, digits = 0, frac = 0, fdig = 0;
  int third = 0;
  while (*t >= '0' && *t <= '9') {
    if (++digits > 12) return 0;                       // far beyond any sensible value
    whole = whole * 10 + (uint32_t)(*t - '0');
    if (whole > 999) return 0;                         // also keeps `whole` from overflowing
    t++;
  }
  if (*t == '.' || *t == ',') {
    t++;
    while (*t >= '0' && *t <= '9') {
      if (++fdig > 6) return 0;
      if (fdig <= 2) frac = frac * 10 + (uint32_t)(*t - '0');
      else if (fdig == 3) third = *t - '0';
      t++;
    }
  }
  while (*t == ' ') t++;
  if (*t || (digits == 0 && fdig == 0)) return 0;
  if (fdig == 1) frac *= 10;                           // "38.7" is 38.70
  uint32_t hund = whole * 100 + frac + (third >= 5 ? 1u : 0u);
  if (hund > (uint32_t)max_deg * 100u) return 0;
  *out = (int16_t)(neg ? -(int32_t)hund : (int32_t)hund);
  return 1;
}

int gqc_parse_time(const char* t) {
  if (!t) return -1;
  for (int i = 0; i < 5; i++)                          // stop at a NUL before reading further
    if (!t[i]) return -1;
  if (t[0] < '0' || t[0] > '9' || t[1] < '0' || t[1] > '9' || t[2] != ':' || t[3] < '0' || t[3] > '9' || t[4] < '0' || t[4] > '9' || t[5])
    return -1;
  int h = (t[0] - '0') * 10 + (t[1] - '0');
  int m = (t[3] - '0') * 10 + (t[4] - '0');
  if (h > 23 || m > 59) return -1;
  return h * 60 + m;
}

int gqc_place_ok(const char* t) {
  if (!t) return 0;
  for (uint32_t n = 0; t[n]; n++)
    if (n >= GQC_PLACE_MAX || !placeChar(t[n])) return 0;
  return 1;
}

int gqc_format_coord(int16_t c, char* buf) {
  int32_t v = c;
  int n = 0;
  if (v < 0) { buf[n++] = '-'; v = -v; }
  if (v >= 10000) buf[n++] = (char)('0' + v / 10000);
  if (v >= 1000) buf[n++] = (char)('0' + v / 1000 % 10);
  buf[n++] = (char)('0' + v / 100 % 10);               // the units digit is always printed
  buf[n++] = '.';
  buf[n++] = (char)('0' + v / 10 % 10);
  buf[n++] = (char)('0' + v % 10);
  buf[n] = 0;
  return n;
}

int gqc_format_time(uint16_t m, char* buf) {
  m = (uint16_t)(m % 1440);
  buf[0] = (char)('0' + m / 600);
  buf[1] = (char)('0' + m / 60 % 10);
  buf[2] = ':';
  buf[3] = (char)('0' + m % 60 / 10);
  buf[4] = (char)('0' + m % 10);
  buf[5] = 0;
  return 5;
}

static int blank(const char* t) {
  while (*t == ' ') t++;
  return *t == 0;
}

static void setFlag(uint8_t* f, uint8_t bit, int8_t v) {
  if (v < 0) return;
  if (v) *f = (uint8_t)(*f | bit); else *f = (uint8_t)(*f & ~bit);
}

int gqc_validate(const GqConf* cur, const GqcInput* in, GqConf* out) {
  GqConf n = *cur;
  gqc_sanitize(&n);
  int16_t v;
  if (in->lat && in->lon) {
    const int bl = blank(in->lat), bo = blank(in->lon);
    if (bl && bo) { n.lat_c = 0; n.lon_c = 0; }                // both empty: coordinates cleared
    else if (bl != bo) return GQC_E_COORD_PAIR;
    else {
      if (!gqc_parse_coord(in->lat, 90, &v)) return GQC_E_LAT;
      n.lat_c = v;
      if (!gqc_parse_coord(in->lon, 180, &v)) return GQC_E_LON;
      n.lon_c = v;
    }
  } else if (in->lat) {
    if (!gqc_parse_coord(in->lat, 90, &v)) return GQC_E_LAT;
    n.lat_c = v;
  } else if (in->lon) {
    if (!gqc_parse_coord(in->lon, 180, &v)) return GQC_E_LON;
    n.lon_c = v;
  }
  if (in->place) {
    if (!gqc_place_ok(in->place)) return GQC_E_PLACE;
    uint32_t i = 0;
    for (; in->place[i]; i++) n.place[i] = in->place[i];
    for (; i < sizeof(n.place); i++) n.place[i] = 0;
  }
  if (in->sleep_time) { int m = gqc_parse_time(in->sleep_time); if (m < 0) return GQC_E_SLEEP_TIME; n.sleep_min = (uint16_t)m; }
  if (in->wake_time) { int m = gqc_parse_time(in->wake_time); if (m < 0) return GQC_E_WAKE_TIME; n.wake_min = (uint16_t)m; }
  if (n.sleep_min == n.wake_min) return GQC_E_SAME_TIME;
  setFlag(&n.flags, GQC_F_WEATHER, in->weather);
  setFlag(&n.flags, GQC_F_FAHRENHEIT, in->fahrenheit);
  setFlag(&n.flags, GQC_F_SLEEP, in->sleep_on);
  if ((n.flags & GQC_F_WEATHER) && n.lat_c == 0 && n.lon_c == 0) return GQC_E_NO_COORDS;
  gqc_sanitize(&n);
  *out = n;
  return GQC_OK;
}
