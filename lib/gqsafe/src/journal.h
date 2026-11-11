// Append-only journal for ONE 4 KB flash sector (design 9.4). Pure C++: no
// Arduino, no heap. Compiled into the firmware only with GQ_FLASH_SAVE=1;
// always compiled for the host tests.
//
// Sector layout (4096 bytes):
//   0      header, 16 bytes: "GQJ1", u32 erase_count, u32 reserved (0xFFFFFFFF), u32 crc32(first 8 bytes)
//   256*k  slot k, k = 1..15, 256 bytes each:
//            u8 type (1) | u8 format (1) | u16 payload_len (240) | u32 seq |
//            u8 payload[240] | u32 crc32 (first 248 bytes) | u32 commit "GQOK" (written last, separately)
//
// Every read and write is 4-byte aligned and a multiple of 4 bytes long (the
// SPI flash API needs that). The HAL may only clear bits in write().
#pragma once
#include <stdint.h>

#define JR_SECTOR_BYTES 4096u
#define JR_SLOT_BYTES   256u
#define JR_SLOTS        15u      // slot indexes 1..15
#define JR_PAYLOAD      240u

typedef struct {
  int (*read)(void* ctx, uint32_t off, uint8_t* buf, uint32_t n);          // off + n <= 4096; returns 0 on success
  int (*write)(void* ctx, uint32_t off, const uint8_t* buf, uint32_t n);   // may only clear bits; returns 0 on success
  int (*erase)(void* ctx);                                                 // whole sector to 0xFF; returns 0 on success
  void* ctx;
} JournalHal;

enum { JR_OK, JR_BLANK, JR_FOREIGN, JR_IO, JR_NO_VALID };

// JR_OK: valid header (slots_used = slots that are not blank, erase_count from the header).
// JR_BLANK: every byte is 0xFF. JR_FOREIGN: anything else. JR_IO: the HAL failed.
// Either out pointer may be NULL.
int journal_probe(const JournalHal* h, uint32_t* slots_used, uint32_t* erase_count);
// Writes the header. Only when the sector is blank (JR_BLANK -> JR_OK); an already
// formatted sector returns JR_OK untouched, a foreign one JR_FOREIGN.
int journal_format(const JournalHal* h);
// Newest valid snapshot. JR_OK, or JR_BLANK / JR_FOREIGN / JR_NO_VALID (header fine, no valid slot) / JR_IO.
int journal_load(const JournalHal* h, uint8_t payload[JR_PAYLOAD], uint32_t* seq);
// Appends one snapshot; erases and restarts at slot 1 when the sector is full.
// Refuses (JR_BLANK, JR_FOREIGN) unless the sector holds a valid header.
int journal_append(const JournalHal* h, const uint8_t payload[JR_PAYLOAD], uint32_t seq);
// Erases the sector back to all 0xFF (the state it was found in).
int journal_erase(const JournalHal* h);
