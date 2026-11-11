# GeekQuest

A self-playing pixel-art village game for the GeekMagic 240x240 ESP8266 clock. A small
adventurer takes breaks from questing to wander the village, chop wood, fish, trade and
chat. You never press anything: it is a clock with a little world in it. The bottom band
shows what the hero is doing right now, the clock's IP address and the uptime.

**Download:** every build of the `main` branch is published on the
[Releases page](https://github.com/techconcert/GeekMagic/releases) (the newest release is marked
"Latest"). Each release has the two firmware files, `preview.html` (the game in your browser)
and `SHA256SUMS.txt`.

## Hardware

- GeekMagic SD Pro / SmallTV-style clock: ESP8266 (ESP-12, 4 MB flash) with a 240x240 ST7789
  display.
- Display wiring (from the [GeekMagic-Open-Firmware](https://github.com/Times-Z/GeekMagic-Open-Firmware)
  teardown): hardware SPI mode 3 at 40 MHz, MOSI 13, SCLK 14, DC 0, RST 2, CS tied to ground,
  backlight on GPIO 5 (active LOW).
- No serial adapter is needed or assumed. Everything is installed and recovered over WiFi.

## Builds

| File | What it is |
|---|---|
| `SDP_GeekQuest_quest_<version>.bin` | The game. |
| `SDP_GeekQuest_test_<version>.bin` | Display test: grass field, walking hero, RGB colour bars, FPS. Flash this first if you are unsure about colours. |

The names start with `SDP` because the stock updater only accepts such names.

## How to flash (over WiFi, no cable)

**First time, from the stock firmware:** open the clock's own web page, go to its firmware update
page and choose `SDP_GeekQuest_test_....bin` (or the quest file). Wait for the clock to restart.

**Later, from GeekQuest:** open `http://<clock IP>/update` (the IP address is on the clock's
bottom band and on your router's list), choose the new `.bin` and press **Upload and install**.
The page checks the file before uploading and shows its CRC32; the clock verifies the image again
before switching over. A failed or interrupted upload leaves the running firmware untouched.

Read [docs/RECOVERY.md](docs/RECOVERY.md) once, before the first install.

## Recovery and the rescue hotspot

Every build contains, and CI refuses to produce a `.bin` without (`tools/check_bin.py`):

- **The update page starts first.** WiFi and the web server start before the display or the game,
  so a game bug cannot hide the update page.
- **Rescue hotspot** `GEEKQUEST-XXXX`, password `GEEKQUEST`, at `http://192.168.4.1/`. It is on for
  the first 10 minutes after every power-on and whenever the clock cannot find your WiFi. The
  WiFi page (`/wifi`) lets you enter your network again.
- **Crash-loop safe mode.** After 3 crashes in a row (or 5 in total without 6 hours of uptime in
  between) the clock boots without the game and shows SAFE MODE; after 6 it also skips the
  display. The update page still works.
- **Automatic restarts** (approved in R2): after hours without WiFi, with very low memory, and once
  a week at night. They never happen while a firmware upload is running, are rate limited, and
  count towards safe mode like any restart.
- **Size limit.** The ESP8266 holds the running firmware while receiving the next one, both inside
  a 1 MB area. CI fails any build over 500 KB, so updates always fit, including going back to
  stock.
- **Stock files are never touched.** Same flash layout as the stock firmware; the code never writes
  flash by itself (only the update page's Updater does), and CI forbids EEPROM, LittleFS and SPIFFS
  code. Flashing `SDPro_V1.0.6_....bin` from the GeekQuest update page brings the original clock
  back.
- **The game is not saved.** After any restart or power cut the game starts on day 1. Only the
  settings (time zone, brightness, colour depth, card period) are kept in RTC memory across
  restarts and updates. The status page reports, read only, what flash sectors 0x3FA000 (stock data) and 0xFF000 (spare) contain:
  a later release may use it.

Gates: `tools/lint_safety.py`, `tools/check_bin_selftest.py`, `tools/check_bin.py` on every `.bin`,
a size report, and `tools/recovery_guard.py` (a `RECOVERY-CHANGE:` commit line and a new
`GQ_RECOVERY_REV` whenever a recovery file changes).

## Build and test

Firmware (PlatformIO, ESP8266 Arduino core):

```
pio run -e quest        # the game  -> dist/SDP_GeekQuest_quest_<version>.bin
pio run -e test         # display test build (the only one with the test scene)
```

Host tests (no ESP8266 needed; run every `test/*.cpp`, `test_world` and `test_soak` take minutes):

```
for t in test/*.cpp; do
  g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror \
    -o /tmp/$(basename $t .cpp) $t lib/gamecore/src/*.cpp lib/gqsafe/src/*.cpp && /tmp/$(basename $t .cpp)
done
python3 tools/lint_safety.py && python3 tools/lint_safety.py --selftest
python3 tools/check_bin_selftest.py
```

Browser preview (clang with wasm32 and lld): `tools/build_preview.sh` writes `preview/dist/index.html`.

Art: tiles and sprites are `assets/src/*.png` on a 16x16 grid with the 16-colour palette in
`tools/palette.py`. After editing run `python tools/png2tiles.py`.

## Repository layout

```
firmware/        ESP8266 entry (main.cpp), ST7789 driver, recovery layer (safety.cpp, netwatch.cpp), prefs.cpp
lib/gamecore/    portable game core (no heap, no libc), used by the firmware and the preview
lib/gqsafe/      pure decision code of the recovery layer (boot counters, WiFi ladder, upload checks)
assets/src/      editable PNG sheets
tools/           asset converter, version stamp, .bin safety checks, source lint, CI guards
docs/            design notes, the owner's recovery guide
preview/         browser preview (WebAssembly)
test/            host tests
```

## How it draws

No full framebuffer (115 KB would not fit). The screen is 15 strips of 240x16 pixels; only strips
that changed are redrawn and sent. Art is stored as 4-bit indexed tiles in flash and expanded
through a 16-colour palette, which also makes the day/night tints cheap.

## License

Public domain: [CC0 1.0 Universal](LICENSE). No rights reserved.
