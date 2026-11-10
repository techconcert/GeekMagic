"""Recovery safety checks for every GeekQuest .bin.

    python tools/check_bin.py dist/SDP_GeekQuest_test_xxx.bin

A .bin only passes if, once flashed, the clock can still be re-flashed over
WiFi (to a newer build OR back to the stock firmware):
  1. ESP8266 image header: magic 0xE9, DIO, 4MB flash (same as stock).
  2. Small enough that the ESP8266 updater can hold the running firmware and
     the next one in the 1 MB code area, in every direction we care about.
  3. Our own update page + updater code are linked in.
  4. Built with the stock flash layout (LittleFS at 0x100000), when known.
  5. File name starts with "SDP" (the stock updater rejects anything else).
"""
import os
import sys

SECTOR = 4096
CODE_AREA = 0x100000          # 4M3M layout: 1 MB for code
STOCK_SIZE = 488304           # SDPro_V1.0.6 stock image
BUDGET = 500 * 1024           # our own hard limit
FS_START_4M3M = 0x40300000

REQUIRED_STRINGS = [
    (b"GQ-OTA-PAGE-v1", "GeekQuest update page"),
    (b"/update_ota", "update route (stock-compatible path)"),
    (b"/update", "update route"),
    (b"Not Enough Space", "ESP8266 Updater library"),
    (b"GEEKQUEST-%02X%02X", "rescue hotspot"),
]


def roundup(n):
    return (n + SECTOR - 1) // SECTOR * SECTOR


def check(path, fs_start=None):
    ok = True

    def fail(msg):
        nonlocal ok
        ok = False
        print("  FAIL:", msg)

    data = open(path, "rb").read()
    size = len(data)
    name = os.path.basename(path)
    print(f"Checking {name} ({size} bytes)")

    if data[0] != 0xE9:
        fail("first byte is not 0xE9 (not an ESP8266 image)")
    if data[2] != 0x02:
        fail(f"flash mode byte is {data[2]:#x}, expected 0x02 (DIO, same as stock)")
    if data[3] >> 4 != 4:
        fail(f"flash size nibble is {data[3] >> 4}, expected 4 (4MB, same as stock)")

    if size > BUDGET:
        fail(f"image is {size} bytes, over the {BUDGET}-byte budget")
    if roundup(size) + roundup(STOCK_SIZE) > CODE_AREA:
        fail("stock firmware could not install this image (or be re-installed from it)")
    if 2 * roundup(size) > CODE_AREA:
        fail("this image could not install a same-sized update of itself")

    for needle, what in REQUIRED_STRINGS:
        if needle not in data:
            fail(f"missing {what} ({needle!r})")

    if fs_start is not None and fs_start != FS_START_4M3M:
        fail(f"_FS_start is {fs_start:#x}, expected {FS_START_4M3M:#x} (stock 4M3M layout)")
    elif fs_start is not None:
        print(f"  flash layout OK: _FS_start={fs_start:#x} (stock 4M3M)")
    else:
        print("  note: flash layout not verified (no ELF given)")

    if not name.startswith("SDP"):
        fail("file name must start with 'SDP' or the stock updater refuses it")

    room = CODE_AREA - roundup(size)
    print(f"  free OTA room while this runs: {room} bytes (stock image needs {roundup(STOCK_SIZE)})")
    print("  PASS" if ok else "  => NOT SAFE TO FLASH")
    return ok


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    results = [check(p) for p in sys.argv[1:]]
    sys.exit(0 if all(results) else 1)
