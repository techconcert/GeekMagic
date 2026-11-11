"""Recovery safety checks for every GeekQuest .bin.

    python tools/check_bin.py dist/SDP_GeekQuest_test_xxx.bin [more.bin ...] [--require-elf]

A .bin only passes if, once flashed, the clock can still be re-flashed over
WiFi (to a newer build OR back to the stock firmware):
  1. ESP8266 image header: magic 0xE9, DIO, 4MB flash (same as stock).
  2. Small enough that the ESP8266 updater can hold the running firmware and
     the next one in the 1 MB code area, in every direction we care about.
  3. Our own update page, rescue hotspot, safe mode and WiFi routes, the recovery revision tag and the
     read-only sector probe lines are linked in, and the update marker occurs exactly once.
  4. Built with the stock flash layout (_FS_start, _FS_end, _EEPROM_start), when the ELF is known.
  5. No symbol that could touch stock's files or settings (EEPROM, LittleFS, SPIFFS, HTTP updater ...).
  6. RAM (.data + .bss) within the baseline plus 8 KB (once RAM_BASELINE is filled in).
  7. No flash journal (GQJ1) and no flash settings store (GQF1): R2 has no game saving and never writes flash. In the quest build the display-test
     scene (GQ_TEST_SCENE, env:test only) must not be linked in.
  8. File name starts with "SDP" (the stock updater rejects anything else).

The ELF (same name, .elf, next to the .bin) is read with the toolchain's nm and size
when they can be found. --require-elf turns "could not read the ELF" into a failure.
"""
# RECOVERY-CHANGE (chunk1): rules 3 (more strings, marker once), 4, 5, 6, 7 and the 485 KB warning are new (design 13.3)
import glob
import os
import re
import shutil
import subprocess
import sys

SECTOR = 4096
CODE_AREA = 0x100000          # 4M3M layout: 1 MB for code
STOCK_SIZE = 488304           # SDPro_V1.0.6 stock image
BUDGET = 500 * 1024           # our own hard limit
WARN_SIZE = 440 * 1024         # R3: warning from 440 KB (the R3 plan keeps the image under 470 KB, enforced in CI)
FS_START_4M3M = 0x40300000
FS_END_4M3M = 0x405FA000
EEPROM_START_4M3M = 0x405FB000
LINKER_SYMBOLS = {
    "_FS_start": FS_START_4M3M,
    "_FS_end": FS_END_4M3M,
    "_EEPROM_start": EEPROM_START_4M3M,
}

# .data + .bss of the build before chunk 0, from the CI log of that commit (design 11.3 step 8).
# TODO(user/lead): fill in the number printed by the "Size report" step of that commit, e.g. RAM_BASELINE = 31234.
# While it is None the RAM rule only reports the value.
RAM_BASELINE = None
RAM_SLACK = 8192

MARKER = b"GQ-OTA-PAGE-v1"

REQUIRED_STRINGS = [
    (MARKER, "GeekQuest update page"),
    (b"/update_ota", "update route (stock-compatible path)"),
    (b"/update", "update route"),
    (b"Not Enough Space", "ESP8266 Updater library"),
    (b"GEEKQUEST-%02X%02X", "rescue hotspot"),
    (b"GQ-RECOVERY-REV", "recovery revision tag"),
    (b"/safemode", "safe mode route"),
    (b"Stock data sector 0x3FA000", "read-only probe of the stock data sector"),
    (b"Spare sector 0xFF000", "read-only probe of the spare sector below the file system"),
    (b"/wifi", "WiFi setup route"),
]
CONFIG_PAGE_STRING = (b"/config", "R3 settings page route")

# R3 adds: no TLS (BearSSL costs 60 to 90 KB of flash and heap the clock does not have), no second HTTP client stack, and no
# sleep modes that would switch the radio off (forced light sleep and the core's deepSleep wrapper; the update page must stay reachable).
# The SDK's own system_deep_sleep*/wifi_fpm_do_sleep symbols are always linked from the closed libraries, so they cannot be
# forbidden here: lint L10 forbids calling them from the sources.
FORBIDDEN_SYMBOLS = re.compile(
    r"EEPROMClass|LittleFS|SPIFFS|spiffs_|ESP8266HTTPUpdate|ArduinoOTA|HTTPClient|(?<![a-z])lfs_"
    r"|(?<![a-z])br_ssl_|WiFiClientSecure|AsyncClient|forceSleepBegin|EspClass\w*deepSleep"
)
# R3 REQUIRED string for the settings page. Chunk P1 switches this to True in the commit that adds /config (together with
# GQ_HAVE_CONFIG_PAGE in firmware/config.h); until then the stub build has no such page.
REQUIRE_CONFIG_PAGE = True
# Code of the display-test scene (lib/gamecore/src/gamecore.cpp, only compiled with -DGQ_TEST_SCENE=1 in env:test).
# The quest build must not contain it. Static C++ names appear mangled (_ZL<len><name>..., _ZZL<len><name>... for
# local statics); the compiler often inlines the functions, so their tables are what remains.
TEST_SCENE_SYMBOLS = re.compile(r"^_ZZ?L\d+(?:test_render|test_tick|test_init|build_test_map|draw_test_top|draw_test_bottom|WAYPOINTS)")


def roundup(n):
    return (n + SECTOR - 1) // SECTOR * SECTOR


class ElfInfo:
    """What the checks need from the ELF. Tests build these directly."""

    def __init__(self, symbols=None, addrs=None, ram=None):
        self.symbols = list(symbols or [])   # all symbol names
        self.addrs = dict(addrs or {})       # name -> address, at least the LINKER_SYMBOLS
        self.ram = ram                       # .data + .bss in bytes, or None


def find_tool(name, env=None):
    p = shutil.which(name, path=(env or {}).get("PATH")) if env else None
    p = p or shutil.which(name)
    if p:
        return p
    for pat in ("~/.platformio/packages/toolchain-xtensa*/bin/" + name,):
        hits = glob.glob(os.path.expanduser(pat))
        if hits:
            return hits[0]
    return None


def read_elf_info(elf, env=None):
    """Runs nm and size on the ELF. Returns ElfInfo, or None when that is not possible."""
    nm = find_tool("xtensa-lx106-elf-nm", env)
    size = find_tool("xtensa-lx106-elf-size", env)
    if not nm or not os.path.exists(elf):
        return None
    try:
        out = subprocess.check_output([nm, elf], env=env).decode(errors="replace")
    except Exception as e:  # noqa: BLE001
        print("  note: could not run nm:", e)
        return None
    info = ElfInfo()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            info.symbols.append(parts[-1])
            if parts[-1] in LINKER_SYMBOLS:
                try:
                    info.addrs[parts[-1]] = int(parts[0], 16)
                except ValueError:
                    pass
    if size:
        try:
            out = subprocess.check_output([size, "-A", elf], env=env).decode(errors="replace")
            total = 0
            seen = False
            for line in out.splitlines():
                parts = line.split()
                if len(parts) >= 2 and parts[0] in (".data", ".bss"):
                    total += int(parts[1])
                    seen = True
            info.ram = total if seen else None
        except Exception as e:  # noqa: BLE001
            print("  note: could not run size:", e)
    return info


def count(data, needle):
    return data.count(needle)


def check(path, fs_start=None, elf=None, require_elf=False):
    """True when the image is safe to flash.

    fs_start    legacy: the _FS_start value (kept so older callers keep working)
    elf         ElfInfo or None
    """
    ok = True

    def fail(msg):
        nonlocal ok
        ok = False
        print("  FAIL:", msg)

    data = open(path, "rb").read()
    size = len(data)
    name = os.path.basename(path)
    is_quest = "_quest" in name
    print(f"Checking {name} ({size} bytes)")

    if size < 16 or data[0] != 0xE9:
        fail("first byte is not 0xE9 (not an ESP8266 image)")
    if size >= 4 and data[2] != 0x02:
        fail(f"flash mode byte is {data[2]:#x}, expected 0x02 (DIO, same as stock)")
    if size >= 4 and data[3] >> 4 != 4:
        fail(f"flash size nibble is {data[3] >> 4}, expected 4 (4MB, same as stock)")

    if size > BUDGET:
        fail(f"image is {size} bytes, over the {BUDGET}-byte budget")
    elif size > WARN_SIZE:
        print(f"  WARNING: image is {size} bytes, above {WARN_SIZE} (limit {BUDGET})")
    if roundup(size) + roundup(STOCK_SIZE) > CODE_AREA:
        fail("stock firmware could not install this image (or be re-installed from it)")
    if 2 * roundup(size) > CODE_AREA:
        fail("this image could not install a same-sized update of itself")

    for needle, what in REQUIRED_STRINGS + ([CONFIG_PAGE_STRING] if REQUIRE_CONFIG_PAGE else []):
        if needle not in data:
            fail(f"missing {what} ({needle!r})")
    n = count(data, MARKER)
    if n > 1:
        fail(f"{MARKER.decode()} occurs {n} times, it must occur exactly once")

    # R2 has no game saving: the flash journal must be gone everywhere (it was the only flash writer)
    if b"GQJ1" in data:
        fail("GQJ1 (flash journal) found: R2 must not contain any flash saving code")
    # R3 has no flash backend either (RTC memory only): the settings-sector header of the unapproved flash store must not appear
    if b"GQF1" in data:
        fail("GQF1 (flash settings store) found: R3 stores settings in RTC memory only")

    # ELF based rules
    addrs = dict(elf.addrs) if elf else {}
    if fs_start is not None:
        addrs.setdefault("_FS_start", fs_start)
    if elf is None and fs_start is None:
        msg = "ELF not available: symbols, linker layout and RAM were not verified"
        if require_elf:
            fail(msg)
        else:
            print("  note:", msg)
    else:
        for sym, want in LINKER_SYMBOLS.items():
            have = addrs.get(sym)
            if have is None:
                if elf is not None or sym == "_FS_start":
                    msg = f"{sym} not found in the ELF"
                    if require_elf:
                        fail(msg)
                    else:
                        print("  note:", msg)
            elif have != want:
                fail(f"{sym} is {have:#x}, expected {want:#x} (stock 4M3M layout)")
            else:
                print(f"  flash layout OK: {sym}={have:#x}")
        if elf is not None and elf.symbols:
            bad = sorted({s for s in elf.symbols if FORBIDDEN_SYMBOLS.search(s)})
            if bad:
                fail("forbidden symbols linked in: " + ", ".join(bad[:8]) + (" ..." if len(bad) > 8 else ""))
        if elf is not None and is_quest and elf.symbols:
            ts = sorted({s for s in elf.symbols if TEST_SCENE_SYMBOLS.match(s)})
            if ts:
                fail("display-test scene code linked into the quest build: " + ", ".join(ts[:6]))
        if elf is not None and elf.ram is not None:
            if RAM_BASELINE is None:
                print(f"  RAM .data+.bss: {elf.ram} bytes (RAM_BASELINE not set yet: not enforced)")
            elif elf.ram > RAM_BASELINE + RAM_SLACK:
                fail(f".data+.bss is {elf.ram} bytes, over baseline {RAM_BASELINE} + {RAM_SLACK}")
            else:
                print(f"  RAM .data+.bss: {elf.ram} bytes (baseline {RAM_BASELINE} + {RAM_SLACK})")
        elif elf is not None:
            msg = "could not read .data/.bss sizes"
            if require_elf:
                fail(msg)
            else:
                print("  note:", msg)

    if not name.startswith("SDP"):
        fail("file name must start with 'SDP' or the stock updater refuses it")

    room = CODE_AREA - roundup(size)
    print(f"  free OTA room while this runs: {room} bytes (stock image needs {roundup(STOCK_SIZE)})")
    print("  PASS" if ok else "  => NOT SAFE TO FLASH")
    return ok


def main(argv):
    paths = []
    require_elf = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--require-elf":
            require_elf = True
        else:
            paths.append(a)
        i += 1
    if not paths:
        sys.exit(__doc__)
    results = []
    for p in paths:
        elf_path = os.path.splitext(p)[0] + ".elf"
        elf = read_elf_info(elf_path) if os.path.exists(elf_path) else None
        results.append(check(p, elf=elf, require_elf=require_elf))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
