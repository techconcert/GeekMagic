// Live weather reply parser (docs/DESIGN-R3.md 2.4, 2.5). Pure C++: no Arduino includes, no heap, no libc calls, no
// floating point. Fed with the raw TCP stream of an HTTP/1.x reply in pieces of any size (status line, headers, optional
// chunked framing, JSON body). The parser is a byte-at-a-time state machine of 40 bytes without pointers into the input,
// so it can sit in static RAM and be fed straight from lwIP callbacks (stack use of wxp_feed is a few words).
//
// What it accepts: status "HTTP/1.x 200"; in the body only the keys after the literal "current":{ count
// ("current_units" is ignored). Required: temperature_2m, weather_code, wind_speed_10m. Optional: is_day (absent = day,
// so a reply to a request that did not ask for it, like the owner's captured one, still parses; wx_build_request always
// asks for it). The reading is complete only once the closing brace of the "current" object has been seen, so a
// truncated reply never yields values. Numbers: sign, digits, one decimal digit; the first dropped digit rounds half
// away from zero (12.96 -> 13.0).
#pragma once
#include <stdint.h>

typedef struct {
  int16_t  temp_c10;   // tenths of a degree Celsius, -900..600
  uint8_t  wmo;        // WMO weather code 0..99
  uint16_t wind_kmh;   // 0..400
  uint8_t  is_day;     // 0 or 1
} WxReading;

enum { WXP_OK = 0, WXP_HTTP_STATUS, WXP_NO_BODY, WXP_MISSING, WXP_RANGE, WXP_TOO_BIG };

#define WXP_BODY_MAX 2048   // decoded body bytes; more is refused
#define WXP_HDR_MAX  1024   // status line plus headers; more is refused

typedef struct WxParser {
  uint16_t hdr_n, body_n;          // bytes seen in the head / in the decoded body (both saturate)
  uint16_t chunk_left;             // bytes left in the current chunk, or the chunk size being read
  uint16_t nint;                   // JSON number: integer part (saturating at 9999)
  int16_t  temp_c10;
  uint16_t wind_kmh;
  uint8_t  phase;                  // internal
  uint8_t  err;                    // WXP_* failure code that ended parsing (0 = none yet)
  uint8_t  chunked;
  uint8_t  line_len;               // bytes on the current head line (CR not counted), saturating
  uint8_t  sp;                     // status line position
  uint8_t  hm, cm;                 // header line: prefix match position, "chunked" match position
  uint8_t  csz;                    // chunk framing: hex digits seen (255 = in an extension)
  uint8_t  depth;                  // JSON nesting of { and [ (saturating)
  uint8_t  in_str, esc, in_cur, done;
  uint8_t  kmask, klen;            // JSON key under construction: candidate names, length
  uint8_t  last, pend;             // key id of the last string / of the key waiting for its value
  uint8_t  nstate, nneg, nf1, nf2, nfd;   // number parser
  uint8_t  wmo, is_day;
  uint8_t  have, bad;              // bit per key stored / bit per key whose value was refused
} WxParser;

void wxp_init(WxParser* p);
void wxp_feed(WxParser* p, const uint8_t* data, uint16_t n);   // any split of the stream
int  wxp_finish(const WxParser* p, WxReading* out);            // WXP_*; out is written only for WXP_OK
int  wxp_settled(const WxParser* p);                           // 1 once the outcome cannot change any more (success or failure)
int  wx_state(const WxReading* r);                             // 0..6, same numbers as GQ_W_* (CLEAR CLOUDY RAIN STORM FOG SNOW WINDY)
// Writes the HTTP request and a NUL; returns its length, or 0 when it does not fit (or lat/lon are out of range).
int  wx_build_request(char* buf, int cap, int16_t lat_c, int16_t lon_c);
