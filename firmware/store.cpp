// See store.h.
#include "store.h"
#include "slotlog.h"
#include "prefs.h"
#include "config.h"
#include "safety.h"
#include "gamecore.h"
#include "gq_types.h"

namespace store {

static const uint32_t BASE = 0x80000UL;          // first of three 4 KB sectors
static const uint32_t MAX_IMAGE = 0x7D000UL;     // a larger image could reach the store
static const uint32_t OFF_SET = 16, OFF_CONF = 16 + GQS_SETTINGS_BYTES, OFF_GAME = 16 + GQS_SETTINGS_BYTES + 64;
static const uint32_t GAME_EVERY_MS = 20UL * 60 * 1000;
static const uint32_t SETTINGS_WAIT_MS = 5000;
static const uint32_t DAY_WAIT_MS = 60UL * 1000;

static_assert(OFF_GAME + GC_SAVE_BYTES == SL_SLOT_BYTES, "record layout");
static_assert(sizeof(GqConf) == 64, "GqConf is 64 bytes");

alignas(4) static uint8_t s_slot[SL_SLOT_BYTES];  // the record loaded at boot, then the buffer for the next one
static SlScan s_scan;
static bool s_on = false, s_have = false, s_gameQ = false;
static uint8_t s_fails = 0;
static uint32_t s_t = 0, s_lastWrite = 0, s_lastOk = 0, s_seqSaved = 0, s_serial = 0;
static uint16_t s_day = 65535;
static GqSettings s_lastSet;
static GqConf s_lastConf;
static uint8_t s_burst = 0;
static uint32_t s_burstT = 0;
static bool s_wrote = false;

static uint32_t slotAddr(uint32_t i) { return BASE + (i / SL_SLOTS_PER_SECTOR) * SL_SECTOR_BYTES + (i % SL_SLOTS_PER_SECTOR) * SL_SLOT_BYTES; }

void begin(bool safeMode) {
  s_on = !safeMode && ESP.getSketchSize() <= MAX_IMAGE && ESP.getFlashChipRealSize() >= 4UL * 1024 * 1024;
  if (!s_on) return;
  for (uint32_t i = 0; i < SL_SLOTS; i++) {
    uint32_t q = 0;
    if (!ESP.flashRead(slotAddr(i), (uint32_t*)s_slot, SL_SLOT_BYTES)) { s_on = false; return; }
    s_scan.state[i] = (uint8_t)sl_classify(s_slot, &q);
    s_scan.seq[i] = q;
  }
  SlPlan p;
  sl_plan(&s_scan, &p);
  if (p.load >= 0 && ESP.flashRead(slotAddr((uint32_t)p.load), (uint32_t*)s_slot, SL_SLOT_BYTES)) {
    s_have = true;
    s_seqSaved = p.next_seq - 1;
  }
  s_gameQ = safety::crashes() >= 2;          // two crashes in a row: do not trust the saved game
  s_lastWrite = millis() - (GAME_EVERY_MS - 5UL * 60 * 1000);   // the first game save comes 5 minutes after boot
  s_burstT = millis();
}

const GqSettings* savedSettings() { return s_have ? (const GqSettings*)(s_slot + OFF_SET) : nullptr; }
const GqConf* savedConf() { return s_have ? (const GqConf*)(s_slot + OFF_CONF) : nullptr; }
const uint8_t* savedGame() { return (s_have && !s_gameQ) ? s_slot + OFF_GAME : nullptr; }

static bool verify(uint32_t addr) {
  alignas(4) uint8_t b[64];
  for (uint32_t off = 0; off < SL_SLOT_BYTES; off += sizeof(b)) {
    uint32_t n = SL_SLOT_BYTES - off < sizeof(b) ? SL_SLOT_BYTES - off : sizeof(b);
    n = (n + 3) & ~3u;
    if (!ESP.flashRead(addr + off, (uint32_t*)b, n)) return false;
    if (memcmp(b, s_slot + off, SL_SLOT_BYTES - off < sizeof(b) ? SL_SLOT_BYTES - off : sizeof(b)) != 0) return false;
  }
  return true;
}

static void save(bool withGame) {
  SlPlan p;
  sl_plan(&s_scan, &p);
  // The record is assembled in place. A quarantined game keeps the bytes that were loaded from flash.
  const GqSettings& st = prefs::settings();
  const GqConf& cf = config::get();
  memcpy(s_slot + OFF_SET, &st, sizeof(st));
  memcpy(s_slot + OFF_CONF, &cf, sizeof(cf));
  if (!s_gameQ) {
    if (gc_save_export(s_slot + OFF_GAME, GC_SAVE_BYTES) == 0) memset(s_slot + OFF_GAME, 0, GC_SAVE_BYTES);   // not the village: nothing to keep
  } else if (!s_have) {
    memset(s_slot + OFF_GAME, 0, GC_SAVE_BYTES);
  }
  (void)withGame;
  sl_seal(s_slot, p.next_seq);
  bool ok = true;
  if (p.erase_sector >= 0) ok = ESP.flashEraseSector(BASE / SL_SECTOR_BYTES + (uint32_t)p.erase_sector);
  const uint32_t addr = slotAddr((uint32_t)p.write_slot);
  if (ok) ok = ESP.flashWrite(addr, (uint32_t*)s_slot, SL_SLOT_BYTES);
  if (ok) ok = verify(addr);
  sl_applied(&s_scan, &p, ok ? 1 : 0);
  const uint32_t now = millis();
  s_lastWrite = now;
  if (ok) {
    s_have = true; s_wrote = true;
    s_seqSaved = p.next_seq; s_lastOk = now; s_fails = 0;
    s_lastSet = st; s_lastConf = cf;
    s_serial = gc_save_serial();
    s_day = (uint16_t)gc_query(GQ_Q_DAY);
  } else if (++s_fails >= 3) {
    s_on = false;                              // flash that does not verify: stop using it until the next restart
  }
}

void loop() {
  if (!s_on) return;
  const uint32_t now = millis();
  if (now - s_t < 1000) return;
  s_t = now;
  if (safety::updating()) return;
  static bool init = false;
  if (!init) {                                 // baseline: what is in RTC now equals what the record holds (or defaults)
    init = true;
    s_lastSet = prefs::settings();
    s_lastConf = config::get();
    s_serial = gc_save_serial();
    s_day = (uint16_t)gc_query(GQ_Q_DAY);
    if (!s_have) { s_lastSet.crc ^= 1; }       // nothing saved yet: the first loop saves the settings
  }
  if (now - s_burstT >= 10UL * 60 * 1000) { s_burstT = now; s_burst = 0; }
  if (s_burst >= 6) return;                    // never more than 6 writes in 10 minutes, whatever happens
  const uint32_t since = now - s_lastWrite;
  const bool setDirty = memcmp(&prefs::settings(), &s_lastSet, sizeof(GqSettings)) != 0 || memcmp(&config::get(), &s_lastConf, sizeof(GqConf)) != 0;
  const bool gameDirty = !s_gameQ && gc_save_serial() != s_serial;
  const bool newDay = !s_gameQ && (uint16_t)gc_query(GQ_Q_DAY) != s_day;
  if ((setDirty && since >= SETTINGS_WAIT_MS) || (newDay && since >= DAY_WAIT_MS) || (gameDirty && since >= GAME_EVERY_MS)) {
    s_burst++;
    save(true);
  }
}

bool active() { return s_on; }

void statusLine(char* buf, size_t n) {
  if (!n) return;
  buf[0] = 0;
  if (!s_on) { snprintf_P(buf, n, PSTR("Saving to flash is off (safe mode, or the flash did not verify).")); return; }
  if (s_wrote) snprintf_P(buf, n, PSTR("Saved to flash %u min ago (record %u)%s."), (unsigned)((millis() - s_lastOk) / 60000), (unsigned)s_seqSaved, s_gameQ ? ", saved game kept aside after crashes" : "");
  else if (s_have) snprintf_P(buf, n, PSTR("Loaded from flash (record %u)%s."), (unsigned)s_seqSaved, s_gameQ ? ", saved game kept aside after crashes" : "");
  else snprintf_P(buf, n, PSTR("Nothing saved to flash yet."));
}

}  // namespace store
