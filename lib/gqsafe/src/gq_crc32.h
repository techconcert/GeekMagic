// CRC-32 (zlib/PNG polynomial, reflected). Pure C++, no tables, no heap.
// The update page computes the same value in JavaScript.
#pragma once
#include <stdint.h>

#define GQ_CRC32_INIT 0xFFFFFFFFu
// Feed bytes into a running state (start from GQ_CRC32_INIT).
uint32_t gq_crc32_step(uint32_t state, const uint8_t* p, uint32_t n);
// Final value for a running state.
static inline uint32_t gq_crc32_done(uint32_t state) { return state ^ 0xFFFFFFFFu; }
// One shot.
uint32_t gq_crc32(const uint8_t* p, uint32_t n);
