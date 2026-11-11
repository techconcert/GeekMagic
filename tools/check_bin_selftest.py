"""Self-test of tools/check_bin.py: synthetic images for every rule must give the expected verdict.

    python tools/check_bin_selftest.py
"""
import contextlib
import io
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_bin  # noqa: E402

KB = 1024


def make(size=300 * KB, magic=0xE9, mode=0x02, nibble=4, drop=None, marker_copies=1, extra=b"", skip_marker=False):
    """A fake image: valid header, filler, and every required string (except `drop`)."""
    body = bytearray(b"\x00" * size)
    body[0] = magic
    body[2] = mode
    body[3] = nibble << 4
    pos = 64
    for needle, _ in check_bin.REQUIRED_STRINGS:
        if drop is not None and needle == drop:
            continue
        if needle == check_bin.MARKER:
            if skip_marker:
                continue
            for _ in range(marker_copies):
                body[pos:pos + len(needle)] = needle
                pos += len(needle) + 8
            continue
        body[pos:pos + len(needle)] = needle
        pos += len(needle) + 8
    if extra:
        body[pos:pos + len(extra)] = extra
    return bytes(body)


GOOD_ELF = dict(addrs=dict(check_bin.LINKER_SYMBOLS), symbols=["setup", "loop", "_FS_start", "_FS_end", "_EEPROM_start"], ram=30000)


def elf(**kw):
    d = dict(GOOD_ELF)
    d["addrs"] = dict(GOOD_ELF["addrs"])
    d["symbols"] = list(GOOD_ELF["symbols"])
    d.update(kw)
    return check_bin.ElfInfo(**d)


def verdict(data, name="SDP_GeekQuest_test_x.bin", **kw):
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, name)
        with open(p, "wb") as f:
            f.write(data)
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            ok = check_bin.check(p, **kw)
        return ok, buf.getvalue()


failures = []


def expect(label, want, got):
    if want != got[0]:
        failures.append(f"{label}: expected {'PASS' if want else 'FAIL'}, got {'PASS' if got[0] else 'FAIL'}\n{got[1]}")
    else:
        print(f"ok   {label}")


# ---- today's rules
expect("good image", True, verdict(make()))
expect("good image with a complete ELF", True, verdict(make(), elf=elf(), require_elf=True))
expect("501 KB is over the budget", False, verdict(make(size=501 * KB)))
expect("500 KB exactly is allowed", True, verdict(make(size=500 * KB)))
ok, text = verdict(make(size=490 * KB))
expect("490 KB passes", True, (ok, text))
if "WARNING" not in text:
    failures.append("490 KB should print a size warning")
expect("wrong magic byte", False, verdict(make(magic=0xE8)))
expect("flash mode not DIO", False, verdict(make(mode=0x00)))
expect("flash size nibble 1 (512 KB)", False, verdict(make(nibble=0)))
expect("flash size nibble 2 (1 MB)", False, verdict(make(nibble=2)))
expect("name without SDP", False, verdict(make(), name="GeekQuest_test.bin"))

# ---- every required string, one at a time
for needle, what in check_bin.REQUIRED_STRINGS:
    if needle == b"/update":
        continue          # a substring of /update_ota: dropping it alone cannot be told apart (and /update_ota is checked)
    expect(f"missing {what}", False, verdict(make(drop=needle)))
expect("marker twice", False, verdict(make(marker_copies=2)))

# ---- flash journal rule (R2: no flash saving anywhere)
expect("GQJ1 in any build", False, verdict(make(extra=b"GQJ1")))
expect("GQJ1 and Save sector in any build", False, verdict(make(extra=b"GQJ1 Save sector")))

# ---- display-test scene must not be in the quest build
expect("test scene symbol in the quest build", False,
       verdict(make(), name="SDP_GeekQuest_quest_x.bin", elf=elf(symbols=GOOD_ELF["symbols"] + ["_ZZL11test_renderiPtE5cycle"])))
expect("test scene symbol in the test build", True,
       verdict(make(), name="SDP_GeekQuest_test_x.bin", elf=elf(symbols=GOOD_ELF["symbols"] + ["_ZZL11test_renderiPtE5cycle"])))
expect("quest build without test scene symbols", True,
       verdict(make(), name="SDP_GeekQuest_quest_x.bin", elf=elf(symbols=GOOD_ELF["symbols"] + ["_ZL10quest_stepP5Actorj", "village_render"])))

# ---- ELF rules
expect("no ELF, not required", True, verdict(make()))
expect("no ELF, required", False, verdict(make(), require_elf=True))
for sym, bad in (("_FS_start", 0x40300000 + 0x1000), ("_FS_end", 0x405FB000), ("_EEPROM_start", 0x405FA000)):
    a = dict(check_bin.LINKER_SYMBOLS)
    a[sym] = bad
    expect(f"{sym} moved", False, verdict(make(), elf=elf(addrs=a)))
a = dict(check_bin.LINKER_SYMBOLS)
del a["_FS_end"]
expect("linker symbol missing and ELF required", False, verdict(make(), elf=elf(addrs=a), require_elf=True))
expect("legacy fs_start argument still works", True, verdict(make(), fs_start=check_bin.FS_START_4M3M))
expect("legacy fs_start wrong", False, verdict(make(), fs_start=0x40400000))
for sym in ("_ZN11EEPROMClass5beginEj", "lfs_format", "LittleFS", "spiffs_mount", "SPIFFS", "ESP8266HTTPUpdate", "ArduinoOTA",
            "_ZN10HTTPClient5beginEv"):
    expect(f"forbidden symbol {sym}", False, verdict(make(), elf=elf(symbols=GOOD_ELF["symbols"] + [sym])))
expect("harmless symbol with lfs inside a word", True, verdict(make(), elf=elf(symbols=GOOD_ELF["symbols"] + ["selfs_x", "golfs_y"])))

# ---- RAM rule (only once the baseline is known)
old = check_bin.RAM_BASELINE
try:
    check_bin.RAM_BASELINE = 30000
    expect("RAM at baseline + 8192", True, verdict(make(), elf=elf(ram=30000 + 8192)))
    expect("RAM above baseline + 8192", False, verdict(make(), elf=elf(ram=30000 + 8193)))
    check_bin.RAM_BASELINE = None
    expect("RAM not enforced without a baseline", True, verdict(make(), elf=elf(ram=10 ** 6)))
finally:
    check_bin.RAM_BASELINE = old

# ---- sizes that the updater could not handle
expect("room for the stock image", True, verdict(make(size=480 * KB)))

if failures:
    print()
    for f in failures:
        print("FAILED:", f)
    print(f"{len(failures)} check(s) failed")
    sys.exit(1)
print("check_bin_selftest: all passed")
