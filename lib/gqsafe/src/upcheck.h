// Upload checks (design 8.2 threat 1, 11.3 step 5). Pure C++: CRC32 of the
// received bytes plus a marker scan that works across chunk borders, and the
// verdict rule. firmware/safety.cpp feeds every received chunk to
// upchk_update() and asks upchk_verdict() before Update.end(true).
#pragma once
#include <stdint.h>

#define UPCHK_MARKER_MAX 24

typedef struct {
  uint32_t crc_state;
  uint32_t bytes;
  uint8_t  seen;                       // the marker was found
  uint8_t  state;                      // KMP state: length of the marker prefix matched so far
  uint8_t  len;                        // marker length
  uint8_t  marker[UPCHK_MARKER_MAX];
  uint8_t  fail[UPCHK_MARKER_MAX];
} UpChk;

// marker: NUL-terminated, 1..UPCHK_MARKER_MAX-1 characters.
void     upchk_init(UpChk* u, const char* marker);
void     upchk_update(UpChk* u, const uint8_t* p, uint32_t n);
uint32_t upchk_crc(const UpChk* u);      // CRC32 of everything fed so far

enum { UV_OK = 0, UV_SIZE, UV_CRC, UV_NO_MARKER };

typedef struct {
  uint32_t want_size;      // 0 = not given
  uint8_t  have_crc;       // want_crc is valid
  uint32_t want_crc;
  uint8_t  force;          // "install anyway" ticked: skips the marker rule only
  uint32_t stock_size;     // exact size of the stock image (488,304)
  uint32_t stock_crc;      // 0 = unknown: the stock rule is then size only
} UpRule;

// Order: size given and different -> UV_SIZE; crc given and different -> UV_CRC;
// then the marker rule: seen, or force, or the image is the stock image
// (size == stock_size and, when stock_crc != 0, crc == stock_crc), else UV_NO_MARKER.
int upchk_verdict(const UpChk* u, const UpRule* r);
