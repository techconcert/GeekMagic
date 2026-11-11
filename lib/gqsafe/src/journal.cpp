// See journal.h. Only built into the firmware when flash saving is compiled in,
// so that builds without it contain no journal code and no "GQJ1" string.
#if defined(ARDUINO) && !(defined(GQ_FLASH_SAVE) && GQ_FLASH_SAVE)
// flash saving is compiled out of this firmware build
#else

#include "journal.h"
#include "gq_crc32.h"

namespace {

const uint32_t JR_MAGIC  = 0x31514A47u;   // "GQJ1" little endian
const uint32_t JR_COMMIT = 0x4B4F5147u;   // "GQOK"
const uint32_t HDR_BYTES = 16;
const uint32_t SLOT_BODY = 252;           // bytes written before the commit word

inline void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
inline uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool allFF(const uint8_t* p, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) if (p[i] != 0xFF) return false;
  return true;
}

enum { SECTOR_BLANK, SECTOR_OURS, SECTOR_FOREIGN, SECTOR_IO };

// Decides what the sector is. For SECTOR_OURS fills erase_count (if given).
// Blank means the WHOLE sector is 0xFF.
int classify(const JournalHal* h, uint32_t* erase_count) {
  uint8_t hdr[HDR_BYTES];
  if (h->read(h->ctx, 0, hdr, HDR_BYTES)) return SECTOR_IO;
  if (get32(hdr) == JR_MAGIC && get32(hdr + 12) == gq_crc32(hdr, 8)) {
    if (erase_count) *erase_count = get32(hdr + 4);
    return SECTOR_OURS;
  }
  if (!allFF(hdr, HDR_BYTES)) return SECTOR_FOREIGN;
  uint8_t buf[JR_SLOT_BYTES];
  for (uint32_t off = 0; off < JR_SECTOR_BYTES; off += JR_SLOT_BYTES) {
    if (h->read(h->ctx, off, buf, JR_SLOT_BYTES)) return SECTOR_IO;
    if (!allFF(buf, JR_SLOT_BYTES)) return SECTOR_FOREIGN;
  }
  return SECTOR_BLANK;
}

int writeHeader(const JournalHal* h, uint32_t erase_count) {
  uint8_t hdr[HDR_BYTES], back[HDR_BYTES];
  put32(hdr, JR_MAGIC);
  put32(hdr + 4, erase_count);
  put32(hdr + 8, 0xFFFFFFFFu);
  put32(hdr + 12, gq_crc32(hdr, 8));
  if (h->write(h->ctx, 0, hdr, HDR_BYTES)) return JR_IO;
  if (h->read(h->ctx, 0, back, HDR_BYTES)) return JR_IO;
  for (uint32_t i = 0; i < HDR_BYTES; i++) if (back[i] != hdr[i]) return JR_IO;
  return JR_OK;
}

bool slotValid(const uint8_t* s, uint32_t* seq) {
  if (s[0] != 1 || s[1] != 1) return false;
  if ((uint32_t)s[2] + ((uint32_t)s[3] << 8) != JR_PAYLOAD) return false;
  if (get32(s + 252) != JR_COMMIT) return false;
  if (get32(s + 248) != gq_crc32(s, 248)) return false;
  *seq = get32(s + 4);
  return true;
}

int writeSlot(const JournalHal* h, uint32_t k, const uint8_t payload[JR_PAYLOAD], uint32_t seq) {
  uint8_t s[JR_SLOT_BYTES], back[JR_SLOT_BYTES];
  for (uint32_t i = 0; i < JR_SLOT_BYTES; i++) s[i] = 0xFF;
  s[0] = 1; s[1] = 1; s[2] = (uint8_t)JR_PAYLOAD; s[3] = (uint8_t)(JR_PAYLOAD >> 8);
  put32(s + 4, seq);
  for (uint32_t i = 0; i < JR_PAYLOAD; i++) s[8 + i] = payload[i];
  put32(s + 248, gq_crc32(s, 248));
  uint32_t base = k * JR_SLOT_BYTES;
  if (h->write(h->ctx, base, s, SLOT_BODY)) return JR_IO;
  if (h->read(h->ctx, base, back, SLOT_BODY)) return JR_IO;
  for (uint32_t i = 0; i < SLOT_BODY; i++) if (back[i] != s[i]) return JR_IO;
  uint8_t c[4];
  put32(c, JR_COMMIT);
  if (h->write(h->ctx, base + SLOT_BODY, c, 4)) return JR_IO;
  if (h->read(h->ctx, base + SLOT_BODY, back, 4)) return JR_IO;
  for (uint32_t i = 0; i < 4; i++) if (back[i] != c[i]) return JR_IO;
  return JR_OK;
}

}  // namespace

int journal_probe(const JournalHal* h, uint32_t* slots_used, uint32_t* erase_count) {
  uint32_t ec = 0;
  int c = classify(h, &ec);
  if (c == SECTOR_IO) return JR_IO;
  if (c == SECTOR_BLANK) return JR_BLANK;
  if (c == SECTOR_FOREIGN) return JR_FOREIGN;
  uint32_t used = 0;
  uint8_t s[JR_SLOT_BYTES];
  for (uint32_t k = 1; k <= JR_SLOTS; k++) {
    if (h->read(h->ctx, k * JR_SLOT_BYTES, s, JR_SLOT_BYTES)) return JR_IO;
    if (!allFF(s, JR_SLOT_BYTES)) used++;
  }
  if (slots_used) *slots_used = used;
  if (erase_count) *erase_count = ec;
  return JR_OK;
}

int journal_format(const JournalHal* h) {
  int c = classify(h, 0);
  if (c == SECTOR_IO) return JR_IO;
  if (c == SECTOR_FOREIGN) return JR_FOREIGN;
  if (c == SECTOR_OURS) return JR_OK;     // already formatted: nothing to do
  return writeHeader(h, 0);
}

int journal_load(const JournalHal* h, uint8_t payload[JR_PAYLOAD], uint32_t* seq) {
  int c = classify(h, 0);
  if (c == SECTOR_IO) return JR_IO;
  if (c == SECTOR_BLANK) return JR_BLANK;
  if (c == SECTOR_FOREIGN) return JR_FOREIGN;
  bool found = false;
  uint32_t best = 0;
  uint8_t s[JR_SLOT_BYTES];
  for (uint32_t k = 1; k <= JR_SLOTS; k++) {
    if (h->read(h->ctx, k * JR_SLOT_BYTES, s, JR_SLOT_BYTES)) return JR_IO;
    uint32_t q;
    if (!slotValid(s, &q)) continue;
    if (!found || q >= best) {
      found = true;
      best = q;
      for (uint32_t i = 0; i < JR_PAYLOAD; i++) payload[i] = s[8 + i];
    }
  }
  if (!found) return JR_NO_VALID;
  if (seq) *seq = best;
  return JR_OK;
}

int journal_append(const JournalHal* h, const uint8_t payload[JR_PAYLOAD], uint32_t seq) {
  uint32_t ec = 0;
  int c = classify(h, &ec);
  if (c == SECTOR_IO) return JR_IO;
  if (c == SECTOR_BLANK) return JR_BLANK;
  if (c == SECTOR_FOREIGN) return JR_FOREIGN;
  uint8_t s[JR_SLOT_BYTES];
  for (uint32_t k = 1; k <= JR_SLOTS; k++) {
    if (h->read(h->ctx, k * JR_SLOT_BYTES, s, JR_SLOT_BYTES)) return JR_IO;
    if (allFF(s, JR_SLOT_BYTES)) return writeSlot(h, k, payload, seq);
  }
  // no blank slot left: the only erase
  if (h->erase(h->ctx)) return JR_IO;
  int r = writeHeader(h, ec + 1);
  if (r != JR_OK) return r;
  return writeSlot(h, 1, payload, seq);
}

int journal_erase(const JournalHal* h) {
  return h->erase(h->ctx) ? JR_IO : JR_OK;
}

#endif  // flash saving compiled in
