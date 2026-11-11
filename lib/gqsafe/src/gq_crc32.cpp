#include "gq_crc32.h"

uint32_t gq_crc32_step(uint32_t c, const uint8_t* p, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c;
}

uint32_t gq_crc32(const uint8_t* p, uint32_t n) {
  return gq_crc32_done(gq_crc32_step(GQ_CRC32_INIT, p, n));
}
