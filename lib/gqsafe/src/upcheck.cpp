#include "upcheck.h"
#include "gq_crc32.h"

void upchk_init(UpChk* u, const char* marker) {
  u->crc_state = GQ_CRC32_INIT;
  u->bytes = 0;
  u->seen = 0;
  u->state = 0;
  uint8_t n = 0;
  while (n < UPCHK_MARKER_MAX - 1 && marker[n]) {
    u->marker[n] = (uint8_t)marker[n];
    n++;
  }
  u->len = n;
  // KMP failure table (bounded by UPCHK_MARKER_MAX)
  for (uint8_t i = 0; i < UPCHK_MARKER_MAX; i++) u->fail[i] = 0;
  uint8_t k = 0;
  for (uint8_t i = 1; i < n; i++) {
    while (k > 0 && u->marker[i] != u->marker[k]) k = u->fail[k - 1];
    if (u->marker[i] == u->marker[k]) k++;
    u->fail[i] = k;
  }
}

void upchk_update(UpChk* u, const uint8_t* p, uint32_t n) {
  u->crc_state = gq_crc32_step(u->crc_state, p, n);
  u->bytes += n;
  if (u->seen || u->len == 0) return;
  uint8_t s = u->state;
  for (uint32_t i = 0; i < n; i++) {
    uint8_t c = p[i];
    // at most len iterations in total per byte (amortised), bounded by the table size
    for (uint8_t guard = 0; s > 0 && c != u->marker[s] && guard < UPCHK_MARKER_MAX; guard++) s = u->fail[s - 1];
    if (c == u->marker[s]) s++;
    if (s == u->len) {
      u->seen = 1;
      break;
    }
  }
  u->state = s;
}

uint32_t upchk_crc(const UpChk* u) { return gq_crc32_done(u->crc_state); }

int upchk_verdict(const UpChk* u, const UpRule* r) {
  if (r->want_size != 0 && r->want_size != u->bytes) return UV_SIZE;
  uint32_t crc = upchk_crc(u);
  if (r->have_crc && r->want_crc != crc) return UV_CRC;
  if (u->seen || r->force) return UV_OK;
  if (r->stock_size != 0 && u->bytes == r->stock_size && (r->stock_crc == 0 || crc == r->stock_crc)) return UV_OK;
  return UV_NO_MARKER;
}
