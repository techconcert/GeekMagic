// See persist.h and DESIGN.md sections 8.3, 8.8 and 9.
//
// RTC user memory map (blocks of 4 bytes; blocks 0..31 belong to the boot loader and are never touched):
//   32..79   game save (GqSave, 192 bytes)       80..91  GqSettings (48 bytes)
//   92..95   meta {magic, sequence, flags, crc}   96..127 owned by safety.cpp and netwatch.cpp
//
// Flash is touched ONLY in the GQ_FLASH_SAVE section at the end of this file, and only through the HAL there.
#include "persist.h"
#include "safety.h"
#include "st7789.h"
#include "gamecore.h"
#include "gq_types.h"       // GQ_Q_* keys of gc_query (design 10.3)
#include "gq_crc32.h"
#if GQ_FLASH_SAVE
#include "journal.h"
#endif

namespace persist {

static const uint32_t RTC_GAME = 32;       // 48 blocks
static const uint32_t RTC_SET = 80;        // 12 blocks
static const uint32_t RTC_META = 92;       // 4 blocks
static const uint32_t META_MAGIC = 0x4D525147;   // "GQRM"
static const uint32_t SAVE_EVERY_MS = 60UL * 1000;

struct Meta { uint32_t magic, seq, flags, crc; };

static GqSettings s_set;
static bool s_safe = false;
static bool s_display = false;
static uint32_t s_seq = 0;               // sequence of the newest save (RTC and flash share it)
static uint32_t s_lastRtcMs = 0;
static uint32_t s_lastSerial = 0;
static bool s_serialInit = false;
static bool s_needRtc = false;           // no valid RTC copy yet: save at the first opportunity
static uint8_t s_source = 0;
static bool s_flashSettingsDirty = false;
static uint32_t s_flashSettingsAt = 0;

// ------------------------------------------------------------ RTC
static uint32_t metaCrc(const Meta& m) { return gq_crc32((const uint8_t*)&m, 12); }

static uint32_t rtcSeq() {
  Meta m;
  ESP.rtcUserMemoryRead(RTC_META, (uint32_t*)&m, sizeof(m));
  return (m.magic == META_MAGIC && m.crc == metaCrc(m)) ? m.seq : 0;
}
static void rtcSeqWrite(uint32_t seq) {
  Meta m;
  m.magic = META_MAGIC;
  m.seq = seq;
  m.flags = 0;
  m.crc = metaCrc(m);
  ESP.rtcUserMemoryWrite(RTC_META, (uint32_t*)&m, sizeof(m));
}
static bool rtcSettingsRead(GqSettings& s) {
  ESP.rtcUserMemoryRead(RTC_SET, (uint32_t*)&s, sizeof(s));
  return gqs_check(&s) == 0;
}
static void rtcSettingsWrite() {
  gqs_seal(&s_set);
  ESP.rtcUserMemoryWrite(RTC_SET, (uint32_t*)&s_set, sizeof(s_set));
}
static bool rtcGameRead(uint8_t out[GQS_GAME_BYTES]) {
  ESP.rtcUserMemoryRead(RTC_GAME, (uint32_t*)out, GQS_GAME_BYTES);
  return gc_save_check(out, GQS_GAME_BYTES) == 0;
}
static void rtcGameWrite(const uint8_t in[GQS_GAME_BYTES]) {
  ESP.rtcUserMemoryWrite(RTC_GAME, (uint32_t*)in, GQS_GAME_BYTES);
  rtcSeqWrite(++s_seq);
}
static void rtcGameWipe() {
  uint32_t z[GQS_GAME_BYTES / 4] = {0};
  ESP.rtcUserMemoryWrite(RTC_GAME, z, sizeof(z));
  rtcSeqWrite(++s_seq);
}

#if GQ_FLASH_SAVE
// forward declarations of the flash section
static void flashInit();
static bool flashLoad(uint8_t payload[GQS_BACKUP_BYTES], uint32_t* seq);
static bool flashSaveInternal(bool force);
static void flashPolicy(uint32_t now);
static bool flashSaveDue6h();
#endif

// ------------------------------------------------------------ settings
void begin(bool safeMode) {
  s_safe = safeMode;
  s_seq = rtcSeq();
  GqSettings r;
  bool have = false;
  if (rtcSettingsRead(r)) {
    s_set = r;
    have = true;
  }
#if GQ_FLASH_SAVE
  flashInit();
  if (!have) {
    uint8_t p[GQS_BACKUP_BYTES];
    uint32_t q;
    if (flashLoad(p, &q)) {
      GqSettings f;
      memcpy(&f, p, sizeof(f));
      if (gqs_check(&f) == 0) { s_set = f; have = true; }
    }
  }
  {
    uint8_t p[GQS_BACKUP_BYTES];
    uint32_t q;
    if (flashLoad(p, &q) && q > s_seq) s_seq = q;
  }
#endif
  if (!have) gqs_defaults(&s_set);
  safety::setTimeZone(s_set.tz);
  safety::setDepth(s_set.depth);
  s_lastRtcMs = millis();
}

const GqSettings& settings() { return s_set; }

void apply(bool display) {
  s_display = display;
  if (display) lcd::setBrightness(s_set.brightness);
  if (!s_safe) {
    gc_set_option(GC_OPT_CARD_PERIOD, s_set.card_period);
    gc_set_option(GC_OPT_NIGHT_QUIET, (s_set.flags & GQS_FLAG_NIGHT) ? 1 : 0);
  }
}

bool setSettings(const GqSettings& s) {
  GqSettings t = s;
  gqs_seal(&t);
  if (gqs_check(&t) != 0) return false;
  s_set = t;
  safety::setTimeZone(s_set.tz);
  safety::setDepth(s_set.depth);
  apply(s_display);
  rtcSettingsWrite();
  s_flashSettingsDirty = true;
  s_flashSettingsAt = millis();
  return true;
}

void resetSettings() {
  GqSettings d;
  gqs_defaults(&d);
  setSettings(d);
}

// ------------------------------------------------------------ game
bool loadGame(uint8_t out[GQS_GAME_BYTES]) {
  if (s_safe || safety::saveQuarantined()) return false;
  bool haveR = rtcGameRead(out);
  uint32_t rseq = haveR ? rtcSeq() : 0;
  s_needRtc = !haveR;
#if GQ_FLASH_SAVE
  uint8_t p[GQS_BACKUP_BYTES];
  uint32_t fseq = 0;
  if (flashLoad(p, &fseq) && gc_save_check(p + GQS_SETTINGS_BYTES, GQS_GAME_BYTES) == 0 && (!haveR || fseq > rseq)) {
    memcpy(out, p + GQS_SETTINGS_BYTES, GQS_GAME_BYTES);
    haveR = true;
    s_needRtc = true;                 // the RTC copy was older or missing: refresh it
  }
#else
  (void)rseq;
#endif
  if (haveR) safety::noteGameRestored();
  s_source = haveR ? (s_needRtc ? 2 : 1) : 0;
  return haveR;
}

uint8_t loadSource() { return s_source; }

void saveRtc() {
  rtcSettingsWrite();
  if (s_safe || safety::saveQuarantined()) return;      // never overwrite what is stored with a game we did not load
  uint8_t b[GQS_GAME_BYTES];
  if (gc_save_export(b, sizeof(b)) == (int)sizeof(b)) rtcGameWrite(b);
}

void loop() {
  uint32_t now = millis();
  if (!s_serialInit) {
    s_serialInit = true;
    s_lastSerial = gc_save_serial();
  }
  if (now - s_lastRtcMs >= SAVE_EVERY_MS) {
    s_lastRtcMs = now;
    if (!s_safe && !safety::saveQuarantined()) {
      uint32_t ser = gc_save_serial();
      if (ser != s_lastSerial || s_needRtc) {
        saveRtc();
        s_lastSerial = ser;
        s_needRtc = false;
      }
    }
  }
#if GQ_FLASH_SAVE
  flashPolicy(now);
#endif
}

void saveNow() {
  saveRtc();
#if GQ_FLASH_SAVE
  flashSaveNow();
#endif
}

void beforeRestart() {
  saveRtc();
#if GQ_FLASH_SAVE
  if (flashSaveDue6h()) flashSaveInternal(false);
#endif
}

void factoryResetGame() {
  if (s_safe) {
    rtcGameWipe();                    // game is not running: the next normal boot starts on day 1
  } else {
    gc_reset_game();
    saveRtc();
  }
  safety::clearQuarantine();
#if GQ_FLASH_SAVE
  flashSaveInternal(true);
#endif
}

void discardSavedGame() {
  safety::clearQuarantine();
  saveRtc();                          // the fresh game replaces the quarantined copy
#if GQ_FLASH_SAVE
  flashSaveInternal(true);
#endif
}

// ------------------------------------------------------------ backup file
bool backupExport(uint8_t out[GQS_BACKUP_BYTES]) {
  uint8_t g[GQS_GAME_BYTES];
  if (s_safe || safety::saveQuarantined()) {
    if (!rtcGameRead(g)) return false;
  } else if (gc_save_export(g, sizeof(g)) != (int)sizeof(g)) {
    return false;
  }
  GqSettings t = s_set;
  gqs_seal(&t);
  gqs_backup_pack(&t, g, out);
  return true;
}

bool backupImport(const uint8_t* in, size_t n) {
  if (!in || n != GQS_BACKUP_BYTES) return false;
  GqSettings s;
  uint8_t g[GQS_GAME_BYTES];
  gqs_backup_split(in, &s, g);
  if (gqs_check(&s) != 0) return false;
  if (gc_save_check(g, GQS_GAME_BYTES) != 0) return false;
  if (!s_safe && gc_save_import(g, GQS_GAME_BYTES) != 1) return false;   // nothing changed so far
  setSettings(s);
  safety::clearQuarantine();
  if (s_safe) rtcGameWrite(g);                                          // applied at the next normal boot
  else saveRtc();
#if GQ_FLASH_SAVE
  flashSaveInternal(true);
#endif
  return true;
}

// ------------------------------------------------------------ status text
void gameSummary(char* buf, size_t n) {
  static const char* const SEASON[4] = {"spring", "summer", "autumn", "winter"};
  int season = (int)gc_query(GQ_Q_SEASON) & 3;
  snprintf(buf, n, "day %d, %s, %d coins, %d stars, goal %d (%d coins)", (int)gc_query(GQ_Q_DAY) + 1, SEASON[season],
           (int)gc_query(GQ_Q_COINS), (int)gc_query(GQ_Q_STARS), (int)gc_query(GQ_Q_GOAL), (int)gc_query(GQ_Q_GOAL_PRICE));
}

#if GQ_FLASH_SAVE
static char s_statusBuf[96];
static const char* flashStatus();
#endif

const char* statusText() {
  if (s_safe) return "safe mode: settings only, in RTC memory";
  if (safety::saveQuarantined()) return "saved game quarantined; the stored copy is untouched";
#if GQ_FLASH_SAVE
  return flashStatus();
#else
  return "RTC memory (survives restarts and updates; lost on a power cut). Use Download backup to keep a copy.";
#endif
}

// ============================================================ flash saving (GQ_FLASH_SAVE=1 only)
#if GQ_FLASH_SAVE

// The linker script of the stock 4M3M layout defines these (design 9.2). Only their addresses are used.
extern "C" uint32_t _FS_end;
extern "C" uint32_t _EEPROM_start;

// ONE constant, no function takes a sector number. 0x3FA = the 4 KB gap between stock's file system and its EEPROM sector.
static const uint32_t GQ_SAVE_SECTOR = 0x3FA;
static const uint32_t SAVE_ADDR = GQ_SAVE_SECTOR * 4096UL;
static const uint32_t STOCK_SETTINGS_ADDR = 0x3FB000UL;      // read only, for the status page
static const uint32_t MIN_GAP_MS = 60UL * 60 * 1000;         // automatic saves are at least an hour apart
static const uint32_t SETTINGS_DELAY_MS = 5UL * 60 * 1000;
static const uint32_t BEFORE_RESTART_MS = 6UL * 3600 * 1000;
static const uint32_t NOW_GAP_MS = 10UL * 1000;
static const uint32_t QUIET_AFTER_REQUEST_MS = 2000;

static bool s_layoutOk = false;      // boot-time symbol checks passed
static bool s_jReady = false;        // header valid: saving is enabled
static bool s_allowOnce = false;     // set only while enabling or erasing on the user's request
static uint8_t s_fails = 0;          // consecutive failed writes; 3 turns saving off for this boot
static bool s_everSaved = false;
static uint32_t s_lastFlashMs = 0, s_lastNowMs = 0;
static bool s_nowEver = false;
static int s_lastDay = -1;
static bool s_dayPending = false;

alignas(4) static uint8_t s_sc[256];

static bool writeAllowed() {
  return s_layoutOk && !safety::safeMode() && !safety::saveQuarantined() && !safety::golden() && !safety::updating() && (s_jReady || s_allowOnce);
}

static int halRead(void*, uint32_t off, uint8_t* buf, uint32_t n) {
  if ((off & 3) || (n & 3) || n > sizeof(s_sc) || off + n > 4096) return -1;
  if (!ESP.flashRead(SAVE_ADDR + off, (uint32_t*)s_sc, n)) return -1;
  memcpy(buf, s_sc, n);
  return 0;
}
static int halWrite(void*, uint32_t off, const uint8_t* buf, uint32_t n) {
  if (!writeAllowed()) return -1;
  if ((off & 3) || (n & 3) || n > sizeof(s_sc) || off + n > 4096) return -1;
  memcpy(s_sc, buf, n);
  return ESP.flashWrite(SAVE_ADDR + off, (uint32_t*)s_sc, n) ? 0 : -1;
}
static int halErase(void*) {
  if (!writeAllowed()) return -1;
  return ESP.flashEraseSector(GQ_SAVE_SECTOR) ? 0 : -1;
}
static const JournalHal HAL = {halRead, halWrite, halErase, nullptr};

static bool layoutCheck() {
  return ((uint32_t)(uintptr_t)&_FS_end - 0x40200000UL) / 4096 == 0x3FA && ((uint32_t)(uintptr_t)&_EEPROM_start - 0x40200000UL) / 4096 == 0x3FB &&
         ESP.getFlashChipRealSize() == 4UL * 1024 * 1024;
}

static void flashInit() {
  s_layoutOk = layoutCheck();
  s_jReady = false;
  if (!s_layoutOk) return;
  uint32_t used, ec;
  s_jReady = journal_probe(&HAL, &used, &ec) == JR_OK;     // read only
}

static bool flashLoad(uint8_t payload[GQS_BACKUP_BYTES], uint32_t* seq) {
  if (!s_layoutOk || !s_jReady) return false;
  uint8_t p[JR_PAYLOAD];
  uint32_t q = 0;
  if (journal_load(&HAL, p, &q) != JR_OK) return false;
  memcpy(payload, p, JR_PAYLOAD);
  *seq = q;
  return true;
}

static bool flashSaveInternal(bool force) {
  (void)force;
  if (!s_jReady || !writeAllowed()) return false;
  saveRtc();                                               // RTC copy first; its sequence number is the journal's
  uint8_t payload[GQS_BACKUP_BYTES];
  if (!backupExport(payload)) return false;
  int r = journal_append(&HAL, payload, s_seq);
  if (r == JR_OK) {
    s_fails = 0;
    s_everSaved = true;
    s_lastFlashMs = millis();
    s_flashSettingsDirty = false;
    return true;
  }
  if (++s_fails >= 3) s_jReady = false;                    // saving turns itself off for this boot
  return false;
}

static bool flashSaveDue6h() {
  return s_jReady && (!s_everSaved || millis() - s_lastFlashMs >= BEFORE_RESTART_MS);
}

static void flashPolicy(uint32_t now) {
  if (!s_jReady || !writeAllowed() || safety::msSinceRequest() < QUIET_AFTER_REQUEST_MS) return;
  int day = (int)gc_query(GQ_Q_DAY);
  if (s_lastDay < 0) s_lastDay = day;
  else if (day != s_lastDay) { s_lastDay = day; s_dayPending = true; }
  bool setDue = s_flashSettingsDirty && now - s_flashSettingsAt >= SETTINGS_DELAY_MS;
  bool gapOk = !s_everSaved || now - s_lastFlashMs >= MIN_GAP_MS;
  if ((s_dayPending || setDue) && gapOk) {
    flashSaveInternal(false);
    s_dayPending = false;
  }
}

Probe probe() {
  if (!s_layoutOk) return PROBE_LAYOUT_MISMATCH;
  int r = journal_probe(&HAL, nullptr, nullptr);
  if (r == JR_BLANK) return PROBE_BLANK;
  if (r == JR_OK) return PROBE_JOURNAL;
  return PROBE_FOREIGN;
}

bool flashEnable() {
  if (!s_layoutOk || safety::safeMode() || safety::golden() || safety::updating()) return false;
  Probe p = probe();
  if (p == PROBE_JOURNAL) { s_jReady = true; return true; }
  if (p != PROBE_BLANK) return false;                      // foreign content: no override
  s_allowOnce = true;
  bool ok = journal_format(&HAL) == JR_OK;
  s_allowOnce = false;
  if (!ok) return false;
  s_jReady = true;
  s_fails = 0;
  flashSaveInternal(true);
  return true;
}

bool flashDisableAndErase() {
  if (!s_layoutOk || safety::safeMode() || safety::golden() || safety::updating()) return false;
  Probe p = probe();
  if (p != PROBE_JOURNAL && p != PROBE_BLANK) return false;   // never touch foreign content
  s_allowOnce = true;
  bool ok = journal_erase(&HAL) == JR_OK;
  s_allowOnce = false;
  s_jReady = false;
  s_everSaved = false;
  return ok;
}

bool flashSaveNow() {
  uint32_t now = millis();
  if (s_nowEver && now - s_lastNowMs < NOW_GAP_MS) return false;
  if (!s_jReady) return false;
  s_nowEver = true;
  s_lastNowMs = now;
  return flashSaveInternal(true);
}

void probeText(char* buf, size_t n) {
  Probe p = probe();
  if (p == PROBE_LAYOUT_MISMATCH) {
    snprintf(buf, n, "Save sector: the flash layout does not match the expected one, flash saving is disabled for this boot.");
    return;
  }
  size_t k;
  if (p == PROBE_BLANK) {
    k = (size_t)snprintf(buf, n, "Save sector 0x3FA000: blank.");
  } else if (p == PROBE_JOURNAL) {
    uint32_t used = 0, ec = 0;
    journal_probe(&HAL, &used, &ec);
    k = (size_t)snprintf(buf, n, "GeekQuest journal GQJ1 in save sector 0x3FA000, %u slots used, erased %u times%s.", (unsigned)used, (unsigned)ec,
                         (!s_jReady) ? " (save sector failing: saving is off for this boot)" : "");
  } else {
    uint8_t h[16];
    char hex[40];
    if (halRead(nullptr, 0, h, 16) != 0) memset(h, 0, sizeof(h));
    for (int i = 0; i < 16; i++) snprintf(hex + i * 2, 3, "%02X", h[i]);
    k = (size_t)snprintf(buf, n, "Save sector 0x3FA000: IN USE BY SOMETHING ELSE (first 16 bytes %s). Nothing will ever be written there.", hex);
  }
  // CRC of stock's settings sector, read only, so the user can see that it never changes
  uint32_t c = GQ_CRC32_INIT;
  for (uint32_t off = 0; off < 4096; off += sizeof(s_sc)) {
    if (!ESP.flashRead(STOCK_SETTINGS_ADDR + off, (uint32_t*)s_sc, sizeof(s_sc))) { c = 0; break; }
    c = gq_crc32_step(c, s_sc, sizeof(s_sc));
  }
  if (k < n) snprintf(buf + k, n - k, " Stock settings sector 0x3FB000 CRC32 %08x.", (unsigned)gq_crc32_done(c));
}

static const char* flashStatus() {
  if (!s_layoutOk) return "RTC memory only (flash layout check failed)";
  if (s_jReady) {
    snprintf(s_statusBuf, sizeof(s_statusBuf), "RTC memory and flash%s", s_everSaved ? " (flash saved this boot)" : "");
    return s_statusBuf;
  }
  return "RTC memory only (flash saving not enabled)";
}

#endif  // GQ_FLASH_SAVE

}  // namespace persist
