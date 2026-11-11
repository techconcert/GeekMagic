# GeekQuest

A self-playing pixel-art village game for the GeekMagic 240x240 ESP8266 clock
(SD Pro / "Smart weather clock Pro", ST7789 display). A small adventurer takes
breaks from questing to wander the village, chop wood, fish and chat. No input
needed. It also shows the time.

## Status

| Build | What it does |
|---|---|
| `test` | Hardware check: grass field, hero walking a loop, RGB colour bars, clock, FPS, update page. **Flash this first.** |
| `quest` | The village game: day/night, chopping, fishing, trading, chatting, questing, sleeping. |

## Getting a build

Every push runs GitHub Actions (`.github/workflows/build.yml`):

1. Game core smoke test under AddressSanitizer (1 h simulated time per scene).
2. Browser preview (`GeekQuest-preview` artifact): the same game code compiled
   to WebAssembly. Open `index.html` to see exactly what the clock will draw.
3. Firmware (`GeekQuest-firmware` artifact): `SDP_GeekQuest_<env>_<version>.bin`.

## Flashing (over WiFi, no cable)

**First time, from the stock firmware:** open the clock's web page, go to the
update page, choose `SDP_GeekQuest_test_....bin`. The stock updater only accepts
file names starting with `SDP`, which is why builds are named that way.

**Later, from GeekQuest:** open `http://<clock-ip>/update` (or
`http://geekquest.local/update`) and choose the new `.bin`. The page checks the
file before uploading and the clock verifies it again before switching over.

## Recovery: how you never lose the ability to reflash

Plain-words guide with checklists: [docs/RECOVERY.md](docs/RECOVERY.md).

Every build has these, and CI refuses to produce a `.bin` without them
(`tools/check_bin.py`):

- **The update page starts first.** WiFi and the web server start before the
  display or the game, so a game bug can't hide the update page. Pages are
  streamed from flash, so they work even when memory is low.
- **Rescue hotspot.** If the clock isn't on your WiFi (and for the first 10
  minutes after every boot, during which nothing can restart the clock by
  itself), it opens its own network `GEEKQUEST-XXXX`, password `GEEKQUEST`.
  Join it and open `http://192.168.4.1/`. Any address you type redirects there.
- **Crash-loop safe mode.** 3 crashes in a row, or 5 crashes in total without
  6 hours of uptime in between, and the clock boots with the game turned off,
  showing SAFE MODE and the hotspot details. After 6 crashes in a row it also
  skips the display. WiFi and the update page still work. You can also choose
  "Restart in safe mode" on the status page.
- **Saved game quarantine.** If the clock crashes repeatedly right after
  loading a saved game, it starts a fresh game and leaves the stored one
  alone until you choose to retry or discard it.
- **Settings and game survive restarts and updates** (kept in RTC memory; lost
  on a power cut). "Download backup" / "Restore backup" on the status page keep
  a copy on your phone. Flash saving exists in the code but is compiled out
  until approved (`GQ_FLASH_SAVE`).
- **Size limit.** The ESP8266 keeps the running firmware while it receives the
  new one, both inside a 1 MB area. CI fails any build over 500 KB, so updates
  always fit, including going back to stock.
- **Stock files are never touched.** The game uses the same flash layout as
  the stock firmware and never writes to its 3 MB file area or its settings
  sector (CI checks the linker symbols and forbids the EEPROM, LittleFS and
  SPIFFS code). Flashing `SDPro_V1.0.6_....bin` from the GeekQuest update page
  brings back the original clock with its settings.
- **Bad uploads don't install.** The image is checked (ESP8266 magic byte, 4 MB
  flash header, size) and fully verified before anything is replaced. A failed
  or interrupted upload leaves the current firmware running. The update page
  also shows the file's CRC32; the clock checks CRC, size and the presence of
  the update page itself, and enforces that when built with `GQ_STRICT_UPLOAD=1`
  (off until approved).
- **Approval switches** in `platformio.ini`, all `0`: `GQ_FLASH_SAVE`,
  `GQ_AUTO_RESTART`, `GQ_STRICT_UPLOAD`. CI also builds `quest` with
  `GQ_FLASH_SAVE=1` (gates only, never published).

CI gates for this layer: `tools/lint_safety.py` (source rules),
`tools/check_bin_selftest.py`, `tools/check_bin.py` on every `.bin`, a size
report, and a guard that requires a `RECOVERY-CHANGE:` line in the commit and a
new `GQ_RECOVERY_REV` whenever a recovery file changes.

What this cannot protect against: a power cut in the few seconds while the
bootloader copies a verified image into place (very rare), and hardware faults.

## Display facts

ST7789 240x240 on hardware SPI, mode 3, 40 MHz. MOSI 13, SCLK 14, DC 0, RST 2,
CS tied to ground, backlight on GPIO 5 (active LOW). Pin facts come from the
[GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
teardown. If colours look swapped on the test build, use the "Display test"
links on the status page (not saved) and tell us which one looked right.

## Making art

Tiles and sprites live in `assets/src/*.png` on a 16x16 grid using the
16-colour palette in `tools/palette.py` (load `assets/palette.gpl` in Aseprite
or LibreSprite; magenta = transparent). After editing:

```
python tools/png2tiles.py      # writes lib/gamecore/src/assets_gen.h
```

`tools/make_art.py` generated the starter art; running it again overwrites the
PNGs.

## Layout

```
firmware/        ESP8266 entry, ST7789 driver, recovery layer (safety.cpp)
lib/gamecore/    portable game core (no heap, no libc), used by firmware and preview
assets/src/      editable PNG sheets
tools/           converters, version stamp, .bin safety check, source lint, CI guards
docs/            design, recovery guide for the owner
preview/         browser preview (WebAssembly)
test/            host smoke test
```

## Rendering approach

No full framebuffer (115 KB would not fit in RAM). The screen is 15 strips of
240x16 pixels, one tile row each. Only strips that changed are redrawn and sent
(7.5 KB each). Art is stored as 4-bit indexed tiles (128 bytes per tile) in
flash and expanded through a 16-colour palette, which also makes day/night tints
cheap.
