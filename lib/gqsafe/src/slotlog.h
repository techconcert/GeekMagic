// R5 flash record log (pure C++: no Arduino, no heap, no libc, no floating point). Decides where records go; the firmware
// (firmware/store.cpp) does the flash access.
//
// Slot i is at byte (i / 12) * 4096 + (i % 12) * 320 of the area (the last 256 bytes of every sector stay unused).
// Three 4 KB sectors hold a ring of 36 slots of 320 bytes. A record is one slot:
//   u32 magic "GQSV" | u32 crc32 over bytes 8..319 | u16 fmt | u16 payload_len | u32 seq | payload (304 bytes: settings 48,
//   weather/sleep record 64, game 192) | 0xFF padding.
// Writing: the next blank slot after the newest record. Only when no slot is blank, the sector after the newest record's sector
// is erased first, so the newest record is never inside a sector that is being erased. A write or an erase cut short by a power
// loss leaves a slot that fails its CRC (never loaded, never reused until its sector is erased) and the newest good record is
// still found. Loading picks the valid record with the highest seq.
#pragma once
#include <stdint.h>

#define SL_SECTORS          3u
#define SL_SECTOR_BYTES     4096u
#define SL_SLOT_BYTES       320u
#define SL_SLOTS_PER_SECTOR 12u
#define SL_SLOTS            36u
#define SL_PAYLOAD_BYTES    304u
#define SL_MAGIC            0x56534751u    // "GQSV"
#define SL_FMT              1u

enum { SL_BLANK = 0, SL_VALID = 1, SL_JUNK = 2 };

// Classifies one slot image of SL_SLOT_BYTES bytes. *seq is set for VALID slots.
int sl_classify(const uint8_t* slot, uint32_t* seq);
// Fills in the header and the CRC of a slot image whose payload (bytes 16..319) is already in place.
void sl_seal(uint8_t* slot, uint32_t seq);
// Builds a slot image (header, payload of SL_PAYLOAD_BYTES, padding, CRC) for sequence number seq.
void sl_build(uint8_t* slot, uint32_t seq, const uint8_t* payload);

typedef struct {
  uint8_t  state[SL_SLOTS];     // SL_BLANK / SL_VALID / SL_JUNK
  uint32_t seq[SL_SLOTS];       // valid slots only
} SlScan;

typedef struct {
  int load;           // slot to load, or -1 (nothing valid)
  int write_slot;     // slot the next record goes to
  int erase_sector;   // sector (0..2) to erase before writing, or -1
  uint32_t next_seq;  // sequence number for the next record
} SlPlan;

void sl_plan(const SlScan* scan, SlPlan* plan);
// After carrying out a plan: record the new state (erase first, then the written slot).
void sl_applied(SlScan* scan, const SlPlan* plan, int ok);
