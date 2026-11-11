// See wxparse.h. Keep this file free of Arduino calls, libc calls and floating point (lint L7, L9).
#include "wxparse.h"

namespace {

enum { PH_STATUS = 0, PH_HEAD, PH_BODY, PH_CSIZE, PH_CDATA, PH_CCRLF, PH_END, PH_DEAD };

enum { K_NONE = 0, K_CURRENT, K_TEMP, K_CODE, K_WIND, K_DAY, K_COUNT };   // key ids (K_NONE: any other key)
enum { HV_TEMP = 1, HV_CODE = 2, HV_WIND = 4, HV_DAY = 8 };
const uint8_t REQUIRED = HV_TEMP | HV_CODE | HV_WIND;

const char* const KEYS[K_COUNT] = {"", "current", "temperature_2m", "weather_code", "wind_speed_10m", "is_day"};
const uint8_t KEYLEN[K_COUNT]   = {0, 7, 14, 12, 14, 6};
const uint8_t KEYMASK_ALL = (1u << K_CURRENT) | (1u << K_TEMP) | (1u << K_CODE) | (1u << K_WIND) | (1u << K_DAY);

inline uint8_t lower(uint8_t c) { return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c; }
inline bool isDigit(uint8_t c) { return c >= '0' && c <= '9'; }
inline bool isSpace(uint8_t c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

void die(WxParser* p, uint8_t code) {
  if (!p->err) p->err = code;
  p->phase = PH_DEAD;
}

// ------------------------------------------------------------------ JSON body

uint8_t keyBit(uint8_t key) {
  switch (key) {
    case K_TEMP: return HV_TEMP;
    case K_CODE: return HV_CODE;
    case K_WIND: return HV_WIND;
    case K_DAY:  return HV_DAY;
    default:     return 0;
  }
}

void numberReset(WxParser* p) {
  p->nstate = 0; p->nneg = 0; p->nint = 0; p->nf1 = 0; p->nf2 = 0; p->nfd = 0;
}

// A number for `key` finished, in tenths (rounded, signed): range check and store, or mark the key bad.
void storeNumber(WxParser* p, uint8_t key, int32_t t10) {
  uint8_t bit = keyBit(key);
  bool ok = false;
  switch (key) {
    case K_TEMP:
      ok = (t10 >= -900 && t10 <= 600);
      if (ok) p->temp_c10 = (int16_t)t10;
      break;
    case K_CODE:
      ok = (t10 >= 0 && t10 <= 990 && t10 % 10 == 0);
      if (ok) p->wmo = (uint8_t)(t10 / 10);
      break;
    case K_WIND:
      ok = (t10 >= 0 && t10 <= 4000);
      if (ok) p->wind_kmh = (uint16_t)((t10 + 5) / 10);
      break;
    case K_DAY:
      ok = (t10 == 0 || t10 == 10);
      if (ok) p->is_day = (uint8_t)(t10 / 10);
      break;
    default:
      return;
  }
  if (ok) p->have |= bit; else p->bad |= bit;
}

// The number of `key` ended. nstate: 1 = sign seen, no digit yet; 2 = integer digits; 3 = after the '.'.
// `clean` is false when the byte that ended it was not whitespace, ',' or '}' (for example 1e5 or 12x).
void numberEnd(WxParser* p, uint8_t key, bool clean) {
  bool ok = clean && (p->nstate == 2 || (p->nstate == 3 && p->nfd > 0));
  if (ok) {
    int32_t t10 = (int32_t)p->nint * 10 + p->nf1 + (p->nf2 >= 5 ? 1 : 0);
    if (p->nneg) t10 = -t10;
    storeNumber(p, key, t10);
  } else {
    p->bad |= keyBit(key);
  }
  numberReset(p);
}

void keyFinish(WxParser* p) {   // a string ended: remember which key it spelled
  uint8_t id = K_NONE;
  for (uint8_t k = 1; k < K_COUNT; k++)
    if ((p->kmask & (1u << k)) && p->klen == KEYLEN[k]) id = k;
  p->last = id;
}

void jsonByte(WxParser* p, uint8_t c) {
  if (p->done) return;
  if (p->nstate) {                                   // inside a number (started only for a key we need)
    if (isDigit(c)) {
      if (p->nstate == 1) p->nstate = 2;
      if (p->nstate == 2) {
        if (p->nint < 9999) p->nint = (uint16_t)(p->nint * 10 + (c - '0'));
        if (p->nint > 9999) p->nint = 9999;
      } else {                                       // after '.': first digit kept, second rounds, the rest dropped
        if (p->nfd == 0) p->nf1 = (uint8_t)(c - '0');
        else if (p->nfd == 1) p->nf2 = (uint8_t)(c - '0');
        if (p->nfd < 200) p->nfd++;
      }
      return;
    }
    if (c == '.' && p->nstate == 2) { p->nstate = 3; return; }
    bool clean = isSpace(c) || c == ',' || c == '}';
    numberEnd(p, p->pend, clean);
    p->pend = K_NONE;
    if (!clean) return;                              // junk such as 1e5: the key is bad, keep scanning
    // clean delimiter: fall through so that ',' and '}' are processed below
  }
  if (p->in_str) {
    if (p->esc) { p->esc = 0; if (p->klen < 255) p->klen++; p->kmask = 0; return; }   // escapes never spell our keys
    if (c == '\\') { p->esc = 1; return; }
    if (c == '"') { p->in_str = 0; keyFinish(p); return; }
    uint8_t i = p->klen;
    if (i < 255) p->klen++;
    uint8_t m = p->kmask;
    for (uint8_t k = 1; k < K_COUNT; k++)
      if ((m & (1u << k)) && (i >= KEYLEN[k] || (uint8_t)KEYS[k][i] != c)) m = (uint8_t)(m & ~(1u << k));
    p->kmask = m;
    return;
  }
  switch (c) {
    case '"':
      p->in_str = 1; p->esc = 0; p->klen = 0; p->kmask = KEYMASK_ALL; p->last = K_NONE;
      return;
    case ':':
      p->pend = p->last; p->last = K_NONE;
      return;
    case '{':
    case '[':
      if (c == '{' && p->depth == 1 && p->pend == K_CURRENT && !p->in_cur) p->in_cur = 1;
      if (p->depth < 255) p->depth++;
      p->pend = K_NONE; p->last = K_NONE;
      return;
    case '}':
    case ']':
      if (p->depth > 0) p->depth--;
      if (c == '}' && p->in_cur && p->depth == 1) { p->in_cur = 0; p->done = 1; }
      p->pend = K_NONE; p->last = K_NONE;
      return;
    case ',':
      p->pend = K_NONE; p->last = K_NONE;
      return;
    default:
      break;
  }
  if (isSpace(c)) return;
  // a value starts here
  if (p->in_cur && p->depth == 2 && p->pend >= K_TEMP) {
    if (c == '-' || isDigit(c)) {
      numberReset(p);
      if (c == '-') { p->nneg = 1; p->nstate = 1; }
      else { p->nstate = 2; p->nint = (uint16_t)(c - '0'); }
      return;
    }
    p->bad |= keyBit(p->pend);                       // true, null, a bare word: not a number for a key we need
  }
  p->pend = K_NONE;
}

void bodyByte(WxParser* p, uint8_t c) {
  if (p->body_n < 0xFFFF) p->body_n++;
  if (p->body_n > WXP_BODY_MAX) { die(p, WXP_TOO_BIG); return; }
  jsonByte(p, c);
}

// ------------------------------------------------------------------ HTTP head

const char STATUS_PAT[] = "HTTP/1.";                 // then 0 or 1, space, "200", then space or end of line
const char TE_PAT[]     = "transfer-encoding:";      // 18 characters
const char CHUNKED[]    = "chunked";

void statusByte(WxParser* p, uint8_t c) {
  uint8_t i = p->sp;
  if (c == '\n') {                                   // end of the status line
    if (i < 12) { die(p, WXP_HTTP_STATUS); return; } // too short to be "HTTP/1.x 200"
    p->phase = PH_HEAD; p->line_len = 0; p->hm = 0; p->cm = 0;
    return;
  }
  bool ok;
  if (i < 7) ok = (c == (uint8_t)STATUS_PAT[i]);
  else if (i == 7) ok = (c == '0' || c == '1');
  else if (i == 8) ok = (c == ' ');
  else if (i == 9) ok = (c == '2');
  else if (i == 10 || i == 11) ok = (c == '0');
  else if (i == 12) ok = (c == ' ' || c == '\r');
  else ok = true;                                    // reason phrase
  if (!ok) { die(p, WXP_HTTP_STATUS); return; }
  if (i < 255) p->sp = (uint8_t)(i + 1);
}

void headByte(WxParser* p, uint8_t c) {
  if (c == '\n') {
    if (p->line_len == 0) {                          // blank line: the body starts
      p->phase = p->chunked ? PH_CSIZE : PH_BODY;
      p->chunk_left = 0; p->csz = 0;
      return;
    }
    p->line_len = 0; p->hm = 0; p->cm = 0;
    return;
  }
  if (c == '\r') return;
  if (p->line_len < 255) p->line_len++;
  uint8_t l = lower(c);
  if (p->hm < 18) {
    if (l == (uint8_t)TE_PAT[p->hm]) p->hm++;
    else p->hm = 255;                                // some other header: stop matching until the next line
    return;
  }
  if (p->hm == 18) {                                 // in the value of Transfer-Encoding
    if (l == (uint8_t)CHUNKED[p->cm]) {
      p->cm++;
      if (p->cm == 7) { p->chunked = 1; p->cm = 0; }
    } else {
      p->cm = (l == 'c') ? 1 : 0;
    }
  }
}

int hexVal(uint8_t c) {
  if (c >= '0' && c <= '9') return c - '0';
  uint8_t l = lower(c);
  if (l >= 'a' && l <= 'f') return 10 + (l - 'a');
  return -1;
}

// chunk framing: hex digits, optional ;extension, CRLF
void chunkSizeByte(WxParser* p, uint8_t c) {
  if (c == '\r') return;
  if (c == '\n') {
    if (p->csz == 0) { die(p, WXP_NO_BODY); return; }
    p->phase = (p->chunk_left == 0) ? PH_END : PH_CDATA;
    return;
  }
  if (p->csz == 255) return;                         // inside an extension: skip to the end of the line
  int h = hexVal(c);
  if (h >= 0 && p->csz < 254) {
    if (p->chunk_left > 0x0FFF) { die(p, WXP_TOO_BIG); return; }
    p->chunk_left = (uint16_t)(p->chunk_left * 16 + h);
    p->csz++;
  } else if (c == ';' && p->csz > 0) {
    p->csz = 255;
  } else {
    die(p, WXP_NO_BODY);
  }
}

}  // namespace

void wxp_init(WxParser* p) {
  uint8_t* b = (uint8_t*)p;
  for (unsigned i = 0; i < sizeof(WxParser); i++) b[i] = 0;
  p->phase = PH_STATUS;
}

void wxp_feed(WxParser* p, const uint8_t* data, uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    uint8_t c = data[i];
    if (p->phase <= PH_HEAD) {                       // status line and headers share one size cap
      if (p->hdr_n < 0xFFFF) p->hdr_n++;
      if (p->hdr_n > WXP_HDR_MAX) { die(p, WXP_TOO_BIG); return; }
    }
    switch (p->phase) {
      case PH_STATUS: statusByte(p, c); break;
      case PH_HEAD:   headByte(p, c); break;
      case PH_BODY:   bodyByte(p, c); break;
      case PH_CSIZE:  chunkSizeByte(p, c); break;
      case PH_CDATA:
        bodyByte(p, c);
        if (p->phase == PH_CDATA && --p->chunk_left == 0) p->phase = PH_CCRLF;
        break;
      case PH_CCRLF:
        if (c == '\n') { p->phase = PH_CSIZE; p->chunk_left = 0; p->csz = 0; }
        else if (c != '\r') die(p, WXP_NO_BODY);
        break;
      default:                                       // PH_END, PH_DEAD: nothing more to learn
        return;
    }
  }
}

int wxp_settled(const WxParser* p) {
  if (p->err) return 1;
  return (p->done && (p->have & REQUIRED) == REQUIRED) ? 1 : 0;
}

int wxp_finish(const WxParser* p, WxReading* out) {
  if (p->err) return p->err;
  if (p->phase < PH_BODY || p->body_n == 0) return WXP_NO_BODY;
  if (p->bad) return WXP_RANGE;
  if (!p->done || (p->have & REQUIRED) != REQUIRED) return WXP_MISSING;
  if (out) {
    out->temp_c10 = p->temp_c10;
    out->wmo = p->wmo;
    out->wind_kmh = p->wind_kmh;
    out->is_day = (p->have & HV_DAY) ? p->is_day : 1;
  }
  return WXP_OK;
}

// WMO code to the game's states: 0 CLEAR, 1 CLOUDY, 2 RAIN, 3 STORM, 4 FOG, 5 SNOW, 6 WINDY (GQ_W_*)
int wx_state(const WxReading* r) {
  uint8_t c = r->wmo;
  if (c <= 3) {
    if (r->wind_kmh >= 29) return 6;
    return c <= 1 ? 0 : 1;
  }
  if (c == 45 || c == 48) return 4;
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return 2;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return 5;
  if (c >= 95 && c <= 99) return 3;
  return 1;
}

namespace {

// Appends text and numbers into buf; once something does not fit, fail stays set.
struct Out {
  char* b; int cap, n; bool fail;
  void ch(char c) { if (n + 1 < cap) b[n++] = c; else fail = true; }
  void str(const char* s) { for (; *s; s++) ch(*s); }
  void hundredths(int v) {                           // v in hundredths of a degree, e.g. -50 -> "-0.50"
    if (v < 0) { ch('-'); v = -v; }
    int w = v / 100, f = v % 100;
    char t[4]; int k = 0;
    if (w == 0) t[k++] = '0';
    for (int x = w; x > 0 && k < 4; x /= 10) t[k++] = (char)('0' + x % 10);
    while (k > 0) ch(t[--k]);
    ch('.'); ch((char)('0' + f / 10)); ch((char)('0' + f % 10));
  }
};

}  // namespace

int wx_build_request(char* buf, int cap, int16_t lat_c, int16_t lon_c) {
  if (!buf || cap < 2 || lat_c < -9000 || lat_c > 9000 || lon_c < -18000 || lon_c > 18000) return 0;
  Out o = {buf, cap, 0, false};
  o.str("GET /v1/forecast?latitude=");
  o.hundredths(lat_c);
  o.str("&longitude=");
  o.hundredths(lon_c);
  o.str("&current=temperature_2m,weather_code,wind_speed_10m,is_day HTTP/1.0\r\n"
        "Host: api.open-meteo.com\r\nUser-Agent: GeekQuest\r\nConnection: close\r\n\r\n");
  if (o.fail) { buf[0] = 0; return 0; }
  buf[o.n] = 0;
  return o.n;
}
