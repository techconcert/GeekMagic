# GeekQuest release R3 design: live weather, sleep schedule, dialogue

Status: DESIGN ONLY. Nothing in this document is implemented. No code was changed to write it.
Audience: the owner of the clock, the lead, and the engineers (Sonnet coders and writers) who implement the chunks in section 8.
This document adds to `docs/DESIGN.md`. Where the two disagree about R3 features, this document wins. The recovery contract (`DESIGN.md` 8.10) stays binding.

Words used in a fixed sense:

- **Base**: `main` after the size-reduction work that is running now (game saves, backup and display-test code removed, image under 400 KB). R3 starts from that commit, not from today's `main`.
- **Recovery layer**, **golden window**, **core**: as in `DESIGN.md`.
- **Live weather**: temperature and condition fetched from the internet. **Simulated weather**: the game's own weather machine in `calendar.cpp`.
- **Screen off**: backlight off and the ST7789 in its sleep state. The processor and the WiFi keep running.
- **NEEDS OWNER APPROVAL**: must not be enabled on the assumption that the answer is yes. The list is in section 10.

## 0. Decisions at a glance

| Topic | Decision |
|---|---|
| Weather source | Open-Meteo forecast API over **plain HTTP**, no key, every 30 minutes. One request, about 1 KB of reply. |
| TLS | **Not used.** BearSSL costs 60 to 90 KB of flash and 13 to 27 KB of heap; the clock has about 26 KB of heap and the image limit is 500 KB. CI keeps refusing any image that links it. |
| City name to coordinates | Done **in the owner's browser** by the settings page (the phone calls Open-Meteo's geocoding API over HTTPS). The clock only stores latitude, longitude and a label. Latitude and longitude can also be typed by hand. |
| HTTP client | Own 200-line client on the raw lwIP callback API. DNS, connect, send and receive never block; a pure parser consumes the reply as it arrives. Peak heap about 3 KB for under a second. |
| Weather in the game | Live condition replaces the simulated state for visuals, HUD icon and hero behaviour. Simulated weather keeps running underneath and takes over when live data is older than 125 minutes or not configured. Seasons stay on the 28-day game calendar. |
| HUD | Weather icon, temperature (up to 3 characters), a degree mark and `C` or `F`, between the item counter and the clock. |
| Settings storage | New 64-byte record in RTC memory (survives restarts and updates). Flash storage in sector 0x3FA000 is compiled out by default (`GQ_CONFIG_FLASH=0`) and **NEEDS OWNER APPROVAL**. Without it, the phone's browser remembers the settings and offers a one-tap restore after a power cut. |
| Sleep | At sleep time the hero walks to bed, then the screen switches off, the processor drops to 80 MHz and rendering stops. **WiFi, the web server, the rescue hotspot rules and the update page run exactly as when awake.** No deep sleep, no light sleep, no WiFi off. |
| Reaching the clock while it sleeps | The status page and `/update` answer as always. "Wake now" button on the page. Unplugging and replugging always gives 10 minutes of lit screen. |
| Dialogue | Conversations of 2 to 4 turns chosen by context (who, time, weather and temperature, season, festival, quest, friendship, what the hero just did). 178 scripts, about 510 lines, dictionary-compressed to about 9.5 KB. The generic hero replies are deleted. |
| Size | R3 adds at most 26 KB of flash (expected 21 KB), 0.7 KB of static RAM, 3 KB of transient heap. |
| Work split | Two small serial plumbing chunks, then five parallel chunks with disjoint files, then four writers, then integration. |

## 1. Baseline (read from the code; re-check before starting)

1. `firmware/main.cpp`: `loop()` calls `safety::loop()` first, ticks the core, renders dirty strips, then `delay()` to about 30 fps. `updateStatus()` runs once per second and pushes clock, date and network state into the core.
2. `firmware/safety.cpp` owns the `ESP8266WebServer` (a file-static object), all pages, the restart machinery and RTC blocks 96..111. Pages are streamed from flash through the small `Out` writer. `GQ_RECOVERY_REV` is 2. Blocks 112..127 belong to `netwatch.cpp`; blocks 80..91 hold `GqSettings` (48 bytes: brightness, depth, flags, card period, time zone); blocks 32..79 held the game save.
3. `firmware/st7789.cpp`: backlight on GPIO 5, active low, PWM through `analogWrite`. There is no sleep function yet. `lcd::begin()` does a hardware reset and the full init sequence (about 290 ms of delays).
4. `platformio.ini`: Arduino core 3.1.2 (`espressif8266@4.2.1`), 160 MHz, lwIP 2 "low memory" variant (MSS 536, IPv4 only). Modem sleep is the SDK default; `GQ_NET_NO_SLEEP` (approval A3) is not set.
5. `tools/check_bin.py` refuses images that link `HTTPClient`; `tools/lint_safety.py` rule L3 forbids `WiFiClientSecure`, `HTTPClient` and blocking WiFi waits; rule L2 allows flash calls only in `persist.cpp`.
6. `calendar.cpp`: 7 weather states `GQ_W_*`, season odds, a state lasts 2 to 8 game hours, festival days are forced clear, snow is redrawn outside winter. `weather.cpp` draws particles as a pure function of a phase. `ui.cpp` `draw_hud()` already draws a weather icon (`GC_ITEMS(40 + w)`) at x=100 and a season block at x=120; the clock starts at x=178.
7. `hero.cpp` `chat_step()`: the villager says one line (`quest_talk`, else `quest_ambient_line`: 26 lines per villager chosen by season, rain and friendship), the hero answers with one of 8 context-free replies (`quest_reply_line`; the fallbacks in `hero.cpp` are `"TELL ME MORE!"`, `"I'LL KEEP WATCH."` and three more). That reply is the reported problem: it never relates to what was said.
8. `hero.cpp` `GQ_ACT_SLEEP`: chosen when `cal_is_night()` (22:00 to 05:59); the hero travels to `SP_BED`, lies down with a Zzz, and wakes when the night ends.
9. Font: 64 glyphs, ASCII 32..95 (upper case, digits, punctuation). The dialogue box shows a name and 2 lines of 19 characters; `ui_say` copies 40 bytes; `|` is the line break.
10. Measured for this design: the existing text blob is 10,445 bytes for 444 strings; ambient lines average 27.4 characters. A greedy 160-entry substring dictionary shrinks that text by 33 to 35 % (dictionary included).

Preconditions the lead confirms before chunk R3-0F starts:

| Id | Precondition | If it does not hold |
|---|---|---|
| P-1 | The size-reduction branch is merged; `quest` image is under 400 KB. | R3 waits. |
| P-2 | RTC blocks 32..79 are no longer used by a game save. | Move the R3 record (section 5.2) to wherever 16 free blocks exist and update the block table. |
| P-3 | `GqSettings` (blocks 80..91) and `persist::settings()` / `setSettings()` still exist. | Chunk P1 keeps the old settings inside its own module; add 1 KB to its budget. |
| P-4 | The owner opens the test address of section 2.3 in a browser and it shows weather text over `http://`. | Use plan B of section 2.3. |

Facts this design could **not** verify (the build container has no outside network and no ESP8266 toolchain): that Open-Meteo answers plain HTTP without redirect (P-4), the exact reply bytes (the sample in 2.4 is from the public documentation), heap and code sizes (estimates), and how the stock firmware fetched its weather. From the stock manual and the open-firmware project it most likely used OpenWeatherMap over plain HTTP with a city code and a shared or user-supplied key; that is plan B.

## 2. Feature 1: live weather

### 2.1 Transport analysis

| Option | Flash | Heap while fetching | Blocks the loop | Verdict |
|---|---|---|---|---|
| HTTPS, BearSSL, certificate check | +80 to 90 KB | 22 to 27 KB (16 KB receive buffer, 6 KB second stack, context) plus a trust anchor | 1 to 3 s handshake | Impossible: more heap than the clock has. |
| HTTPS, BearSSL, `setInsecure()`, 512-byte buffers | +60 to 70 KB | 12 to 14 KB, and only if the server honours the fragment-length extension; otherwise as above | 1 to 3 s | Rejected: leaves under 12 KB free (the heap watchdog's warning line), spends a quarter of the remaining image budget, and gives no real security without the certificate check. |
| Plain HTTP, `HTTPClient` or `WiFiClient` | +6 to 9 KB | 2 to 4 KB plus `String`s | DNS up to 10 s, connect up to 5 s when the internet is down | Rejected: stalls the game and the web server. |
| Plain HTTP, `ESPAsyncTCP` library | +10 KB, new dependency | 3 KB | no | Rejected: a third-party library in the image for something 200 lines can do. |
| **Plain HTTP, own client on raw lwIP** | **+1.6 KB** (lwIP TCP and DNS are already linked for the web server and NTP) | **about 1.5 KB typical, 3 KB worst** | **no** | **Chosen.** |

Consequences of plain HTTP, stated openly: the request (latitude and longitude rounded to 0.01 degree, about 1 km) is readable by the network path, and a forged reply could show a wrong temperature. A forged reply cannot do more than that: the parser accepts only four numbers inside fixed ranges and nothing from the reply is stored in flash or executed.

### 2.2 City name to coordinates: in the browser

The settings page (served by the clock over HTTP) contains a search box. Its JavaScript runs on the owner's phone or PC, which has HTTPS, and calls `https://geocoding-api.open-meteo.com/v1/search?name=<text>&count=5&language=en&format=json`. The page lists the matches ("Lisbon, Portugal 38.72, -9.14"), and a tap fills the latitude, longitude and label fields of the form. Foreign names, accents and ambiguous names are handled by the phone; the clock needs no geocoding code, no UTF-8 handling and no second host.

When the phone has no internet (for example while it is joined to the rescue hotspot) the search shows "No internet on this device. Type latitude and longitude, or open this page over your home WiFi." The two number fields always work.

Rejected: geocoding on the clock over plain HTTP. It would work, but costs about 1.5 KB, a second reply parser with free text in it, and percent-encoding of UTF-8 names, for no benefit to the owner.

### 2.3 Provider

| Provider | Key | Plain HTTP | Reply | Verdict |
|---|---|---|---|---|
| **Open-Meteo** `api.open-meteo.com` | none | yes according to its documentation (P-4 confirms) | about 450 bytes of JSON with a numeric WMO code | **Chosen.** Free for non-commercial use, 10,000 calls a day allowed, we make 48. Attribution "Weather data by Open-Meteo.com" is shown on the settings page (licence CC BY 4.0). |
| OpenWeatherMap `api.openweathermap.org` | owner's own free key | yes | about 500 bytes of JSON with a numeric condition id | **Plan B.** Probably what the stock firmware used. Needs the owner to register. |
| wttr.in | none | yes | free text, often overloaded or slow | Rejected. |

Test address for P-4 (the owner opens it in a browser, with their own coordinates): `http://api.open-meteo.com/v1/forecast?latitude=38.72&longitude=-9.14&current=temperature_2m,weather_code,wind_speed_10m,is_day`. Expected: a line of text starting with `{"latitude"` while the address bar still says `http://`. The owner saves that text and sends it; it becomes the first real test fixture.

The provider is described by one small table (host, request format, four key names, code mapper) so plan B is a change of about 40 lines in `wxparse.cpp` plus a key field on the page. Plan B is not implemented unless P-4 fails.

### 2.4 Request, reply and mapping

Request (built once per fetch into a static 200-byte buffer, HTTP/1.0 so the server does not use chunked encoding):

```
GET /v1/forecast?latitude=38.72&longitude=-9.14&current=temperature_2m,weather_code,wind_speed_10m,is_day HTTP/1.0\r\n
Host: api.open-meteo.com\r\nUser-Agent: GeekQuest\r\nConnection: close\r\n\r\n
```

Temperature is always requested in Celsius; the core converts to Fahrenheit for display (`F = C * 9 / 5 + 32` on tenths, integer arithmetic), so changing the unit needs no new fetch.

Reply body, as documented (to be replaced by the owner's capture):

```
{"latitude":38.72,"longitude":-9.14,"generationtime_ms":0.06,"utc_offset_seconds":0,"timezone":"GMT",
"timezone_abbreviation":"GMT","elevation":38.0,"current_units":{"time":"iso8601","interval":"seconds",
"temperature_2m":"°C","weather_code":"wmo code","wind_speed_10m":"km/h","is_day":""},
"current":{"time":"2026-11-11T10:00","interval":900,"temperature_2m":7.3,"weather_code":61,"wind_speed_10m":12.2,"is_day":1}}
```

Mapping to the game's seven states (the owner's list: sunny, cloudy, rain, storm, snow, fog, windy):

| WMO `weather_code` | State |
|---|---|
| 0, 1 | `GQ_W_CLEAR` (drawn with the moon icon, cell 47, when `is_day` is 0) |
| 2, 3 | `GQ_W_CLOUDY` |
| 45, 48 | `GQ_W_FOG` |
| 51..67, 80..82 | `GQ_W_RAIN` |
| 71..77, 85, 86 | `GQ_W_SNOW` |
| 95..99 | `GQ_W_STORM` |
| any code up to 3 with `wind_speed_10m` of 29 km/h or more | `GQ_W_WINDY` |
| anything else | `GQ_W_CLOUDY` |

### 2.5 Pure code (`lib/gqsafe/src`, host-tested, no Arduino includes)

```c
// wxparse.h
typedef struct { int16_t temp_c10; uint8_t wmo; uint16_t wind_kmh; uint8_t is_day; } WxReading;
typedef struct WxParser WxParser;                       // at most 64 bytes, no pointers into the input
enum { WXP_OK = 0, WXP_HTTP_STATUS, WXP_NO_BODY, WXP_MISSING, WXP_RANGE, WXP_TOO_BIG };
void wxp_init(WxParser* p);
void wxp_feed(WxParser* p, const uint8_t* data, uint16_t n);   // any split of the stream; stack use under 96 bytes
int  wxp_finish(const WxParser* p, WxReading* out);            // WXP_*; out is written only for WXP_OK
int  wx_state(const WxReading* r);                             // 0..6, same numbers as GQ_W_*
int  wx_build_request(char* buf, int cap, int16_t lat_c, int16_t lon_c);   // returns length, 0 if it does not fit

// wxfsm.h: when to fetch. Stepped once per second.
typedef struct { uint32_t now_ms, uptime_ms, free_heap, max_block;
                 uint8_t sta_up_s;      // seconds the station link has been up, saturating at 255
                 uint8_t configured, updating, safe_mode, screen_off, wake_soon, http_recent, strikes; } WxIn;
enum { WXA_NONE = 0, WXA_START = 1, WXA_ABORT = 2 };
typedef struct WxFsm WxFsm;
void    wxfsm_init(WxFsm* f, uint32_t now_ms, uint32_t seed);
uint8_t wxfsm_step(WxFsm* f, const WxIn* in);
void    wxfsm_result(WxFsm* f, uint32_t now_ms, int ok);       // called when a fetch ends
void    wxfsm_kick(WxFsm* f);                                  // settings changed: fetch in 2 s
uint8_t wxfsm_age_class(const WxFsm* f, uint32_t now_ms);      // 0 no data, 1 fresh, 2 stale (grey), 3 expired
```

Parser rules: status line must be `HTTP/1.x 200`; headers are skipped up to the blank line, noting `Transfer-Encoding: chunked` (case-insensitive) and then removing the chunk framing; in the body only keys that follow the literal `"current":{` count (so `"current_units"` is ignored); keys may come in any order with any whitespace; numbers are read as sign, digits, one decimal digit (more are dropped, the first dropped digit rounds); accepted ranges are temperature -90.0..60.0, code 0..99, wind 0..400, is_day 0..1; the body is capped at 2048 bytes; all four keys must be present.

Schedule rules (`wxfsm`):

| Rule | Value |
|---|---|
| First fetch | uptime at least 40 s, station link up for at least 20 s |
| Period after success | 30 min plus 0 to 60 s of jitter from the seeded generator |
| Retry after failure | 1, 2, 5, 15 min, then every 30 min |
| Deadline for one fetch (DNS + connect + reply) | 8 s, then `WXA_ABORT` |
| Never start when | not configured, safe mode, upload in progress, link down, a web request was served in the last 2 s, free heap under 14 KB, largest block under 6 KB, `strikes` at least 2 |
| Screen off (sleep) | no fetches, except one when `wake_soon` (5 minutes before wake) |
| Data age | fresh up to 65 min; stale (temperature drawn grey) up to 125 min; then expired: the game returns to simulated weather and the temperature disappears |
| All time comparisons | `now - since >= period` (lint L5) |

### 2.6 Firmware glue (`firmware/wxnet.cpp`, about 200 lines)

`wxnet::loop()` is called from `main.cpp` on every iteration and does its work once per second. The sequence:

1. `WXA_START`: `safety::wxMark(true)` (section 2.8), `wxp_init`, build the request, `dns_gethostbyname("api.open-meteo.com", &ip, onDns, generation)`. `ERR_OK` means cached; `ERR_INPROGRESS` means the callback will come.
2. Address known: `tcp_new()`, `tcp_arg/tcp_err/tcp_recv`, `tcp_connect(pcb, &ip, 80, onConnected)`.
3. `onConnected`: `tcp_write(pcb, request, len, 0)` (the buffer is static and unchanged during the fetch) and `tcp_output`.
4. `onRecv` with data: for each pbuf in the chain `wxp_feed`, then `tcp_recved` and `pbuf_free`. With `p == NULL` (peer closed): set the "end" flag.
5. `loop()` sees the end flag: close the connection, `wxp_finish`, hand the reading to the core through `main.cpp`, `wxfsm_result`, `safety::wxMark(false)`.

Binding rules for this file:

- Callbacks only feed the parser and set flags. They never call `tcp_abort`, never allocate, never print, and use under 128 bytes of stack (they run on the system stack).
- `onErr` means lwIP has already freed the connection: set the pointer to null and the error flag, nothing else.
- Closing happens only in `loop()`: clear the four callbacks, `tcp_close`, and `tcp_abort` if close fails or on `WXA_ABORT`.
- DNS cannot be cancelled: every fetch has a generation number, and a DNS callback with an old number is ignored.
- No `delay`, no `while` that waits for the network, no `String`, no `WiFiClient`.
- When `safety::updating()` becomes true the connection is aborted at once.

Heap: one connection block (about 200 bytes), the incoming pbufs (the reply fits two segments; the receive window caps it at 2,144 bytes), a DNS query packet. Typical 1.5 KB for under a second, worst case 3 KB. The lowest free heap seen during a fetch is kept and shown on the status page.

### 2.7 Weather in the core

New public call (`gamecore.h`), invoked by `main.cpp` once per second with the current reading:

```c
#define GC_WX_NONE  (-1)
#define GC_WXF_FAHRENHEIT 1     // display unit
#define GC_WXF_NIGHT      2     // is_day == 0
#define GC_WXF_STALE      4     // age class 2
void gc_set_live_weather(int wx, int temp_c10, int flags);   // wx 0..6 or GC_WX_NONE; values are clamped
```

New functions between modules (`gq_mod.h`), implemented in `calendar.cpp`:

```c
int cal_live(void);                 // 1 while live data is in use
int cal_weather_shown(void);        // what the screen shows: live state if any, else simulated
int cal_temp(int* out, int* is_f);  // 1 and whole degrees in the display unit (round half away from zero); 0 if none
int cal_temp_c(int* out);           // 1 and whole degrees Celsius, for dialogue conditions
```

Rules:

1. The simulated machine (`G.s.weather`, `weather_hours`) keeps running untouched, so the fallback is seamless.
2. `cal_weather_shown()` drives particles (`weather.cpp`), the palette look (`cal_look`), the HUD icon and the clock card. It is the live state whenever live data is present.
3. `cal_weather()` keeps its meaning for game rules (hero scheduler, quests, critters). It returns the live state too, with one cap: a live STORM blocks outdoor work for at most 120 minutes per game day; after that `cal_weather()` reports RAIN while the screen keeps showing the storm. A week of real thunderstorms must not stop the story.
4. While live, the two simulated-only rules are skipped: festival days are not forced clear, and snow may appear in any game season. When the live state is SNOW, `cal_look` uses the winter palette remap for the picture (the game season, its festivals and its forage tables do not change).
5. `GQ_EV_WEATHER` is emitted when the value of `cal_weather()` changes, whatever the cause.
6. Seasons stay on the 28-day game calendar (approved earlier as A8). Driving seasons from the real date would turn a one-week season into three months and break festival and story pacing.
7. Nothing of this is saved; after a restart the first fetch comes about 45 s after boot.

HUD (strip 0). When there is no live data the layout is exactly today's. With live data:

```
x=0   coin icon, coins            (unchanged)
x=56  item icon, count            (unchanged)
x=100 weather icon 16 px          (cell 40 + state; cell 47 moon for CLEAR at night)
x=117 temperature, scale 2, right-aligned in 3 character cells (117..152): "7", "23", "-5", "104"
x=154 degree mark: 3x3 hollow square at y=1..3; unit letter C or F at scale 1, y=8
x=163 season as 2 letters (SP SU AU WI) and "D3" below, scale 1; replaced by the red "!" while WiFi is down
x=178 clock                       (unchanged)
```

The temperature is white when fresh and grey when stale. Values are clamped to -99..199 for drawing. The HUD signature includes temperature, unit and age class so the strip redraws when they change. The clock card's weather line gets the temperature appended.

### 2.8 Failure modes

| Failure | Effect | Handling |
|---|---|---|
| No location set | No request is ever made | Simulated weather, old HUD layout |
| WiFi down, DNS fails, server down, timeout | Fetch fails within 8 s; nothing blocks | Backoff of 2.5; data ages to stale, then expired, then simulated weather |
| Reply is an error page, truncated, or not JSON | Parser returns a `WXP_*` code; no value is taken | Shown as "last error" on the settings page |
| Forged or absurd values | Rejected by ranges; a plausible forged value shows wrong weather only | Accepted risk of plain HTTP |
| Heap low | Fetch not started | Gate in `wxfsm`; status page shows the lowest heap seen during fetches |
| The fetch code crashes the clock | Would repeat every boot | **Weather strikes**: `safety::wxMark(true)` sets a flag in RTC before a fetch and clears it after. A boot that follows an unplanned reset with the flag set adds one strike. At 2 strikes weather is off until the owner presses "Try weather again" or power-cycles. Independently the crash counters lead to safe mode, where weather never runs. |
| Fetch during the golden window | New network code runs in the first 10 minutes | The first fetch waits for 40 s of uptime; it only opens one outgoing connection and cannot change WiFi mode, hotspot or restart state (lint L10 keeps WiFi control out of `wxnet.cpp`). |
| Upload in progress | Heap needed by the updater | Fetches are aborted and not started while `safety::updating()` |
| Provider ends plain HTTP or changes the format | Weather expires | Simulated weather continues; plan B or a parser fix by update |

## 3. Feature 2: sleep schedule

### 3.1 What "sleep" is, and what it is not

The owner has no serial adapter, so the sleep state must not be able to hide or stop the recovery layer. The design therefore makes sleep a **display and speed state only**:

| Part | Awake | Asleep |
|---|---|---|
| Backlight | on (PWM) | off, PWM stopped |
| ST7789 | on | display off (0x28), sleep in (0x10) |
| Rendering and SPI | about 30 fps | none |
| Processor | 160 MHz | 80 MHz |
| Main loop | 33 ms period | 50 ms period; `safety::loop()` on every pass as always |
| Game core | ticked every frame | ticked 4 times a second (the calendar must see midnight; costs almost nothing) |
| WiFi station, web server, mDNS, DNS, NTP | running | **running, unchanged** |
| Rescue hotspot rules (golden window, link lost) | as designed | **unchanged** |
| Weather fetches | every 30 min | paused; one fetch 5 minutes before wake |

Power, estimated (not measured): awake about 0.6 to 0.7 W; asleep with the station link up about 0.15 W, because the SDK's default modem sleep switches the radio off between router beacons once the hotspot is off; asleep with the hotspot on about 0.4 W.

Options that were examined and rejected:

| Option | Why not |
|---|---|
| Deep sleep (`ESP.deepSleep`) | Waking needs GPIO16 wired to reset. If this board lacks the wire the clock never wakes until it is unplugged, and it is unreachable the whole night. Also erases RAM, so the game would restart every morning. **Forbidden by lint.** |
| Timed light sleep (`wifi_fpm_*`) | Radio off, unreachable, wake-up paths are fragile on this SDK, time is kept by the imprecise RTC oscillator. **Forbidden by lint.** |
| Automatic light sleep (`WIFI_LIGHT_SLEEP`) | Saves about 0.05 W more than modem sleep, but is known to be unreliable with timers, PWM and a soft AP that may have to start at any moment. Not worth any risk to the web page. **Forbidden by lint.** |
| WiFi off at night (`WIFI_OFF`, `forceSleepBegin`) | Makes the update page unreachable for hours and NTP stops. **Forbidden by lint.** |
| Modem sleep | Already the default behaviour of this build; nothing to switch. If approval A3 (modem sleep off) is ever granted, sleep mode would need to switch it back on for the night; out of scope for R3. |

Timekeeping: the processor never halts, so the system clock keeps running from the main crystal and the SDK's hourly NTP sync continues through the night. With WiFi down all night the drift is under 2 seconds.

### 3.2 Schedule logic (`lib/gqsafe/src/sleepfsm.cpp`, pure, host-tested)

Inputs once per second: `now_ms`, clock valid (NTP has synced since boot), local minute of day, schedule enabled, `sleep_min`, `wake_min`, boot kind, uptime, updating, safe mode, hero in bed, and a command from the page (wake now, sleep now). Outputs: state, hero mode for the core, screen on or off, "wake soon".

States: `AWAKE`, `BEDTIME` (the hero has been sent to bed, screen still on), `ASLEEP` (screen off).

| Rule | Definition |
|---|---|
| Window test | With `s = sleep_min`, `w = wake_min`: if `s < w` the window is `s <= m < w`; if `s > w` (crosses midnight) it is `m >= s or m < w`. `s == w` is refused by the form. |
| Level, not edge | The window is evaluated from the current local time every second. A time-zone change, a daylight-saving jump or a late NTP sync therefore corrects itself within a second and cannot skip a night or a morning. |
| Bedtime | 5 minutes before `s` (same wrap-around arithmetic) the state becomes `BEDTIME`: hero mode 2 (go to bed). |
| Screen off | In `BEDTIME`, once the window has begun and the hero has been in bed for 10 s; at the latest 120 s after `s` whatever the hero is doing. |
| Wake | When the window ends: screen on, hero mode 1. "Wake soon" is true in the last 5 minutes of the window. |
| Clock not valid | Never go to sleep. A clock without NTP after a power cut stays lit until the time is known. Once asleep, the clock stays valid (it is latched), so sleep is not left by accident. |
| Boot grace | After every boot the screen stays on for 10 minutes (the golden window) even inside the sleep window, so a power-cycle at night always shows the IP address and the hotspot name. Exception: after an *automatic* restart (maintenance, heap, WiFi; only exist if approval A2 is ever granted) the screen stays off for up to 60 s until the time is known and then follows the schedule, so a 04:00 maintenance restart does not light the room. After a crash the grace applies: a lit screen at night is then a visible sign that something happened. |
| Upload | No transition to `ASLEEP` while an upload runs. |
| Safe mode | Sleep never runs; the SAFE MODE screen stays lit. |
| "Wake now" | Awake until the window is next left (overrides one night). |
| "Sleep now" | Asleep until the next wake time, at most 12 h, or "Wake now". Works outside the window too. |
| Schedule disabled | Always `AWAKE` with hero mode 0: the hero keeps his built-in night (22:00 to 06:00) and the screen stays on, as today. |

### 3.3 The hero and the core

New option and query (`gamecore.h`, `gq_types.h`):

```c
enum { GC_OPT_SLEEP_MODE = 2 };   // 0 = built-in night (default), 1 = schedule in use: stay up, 2 = go to bed and sleep
// gc_query(GQ_Q_HERO_ABED): 1 while the hero lies in bed (sleep phases 2 and 4)
```

`hero.cpp` (chunk R3-0C): one helper `sleep_due()` replaces the three uses of `cal_is_night()` that decide the hero's own sleep (`pick_activity`, and the two wake tests in `GQ_ACT_SLEEP`): mode 0 gives `cal_is_night()`, mode 1 gives false, mode 2 gives true. When `sleep_due()` becomes true while another activity runs and no dialogue or curtain is on screen, the hero drops it through the existing `go_home()` path and the scheduler then picks `GQ_ACT_SLEEP`. Before lying down he says a bedtime line (`talk_solo`, section 4.4), and a morning line on waking, which uses the live temperature when there is one. `cal_is_night()` itself is unchanged: villagers, critters and the sky still follow the real clock.

### 3.4 Firmware glue

`firmware/nightmode.cpp`: steps `sleepfsm` once per second; calls `gc_set_option(GC_OPT_SLEEP_MODE, ...)`; on entering `ASLEEP` calls `lcd::sleep()` and `system_update_cpu_freq(80)`; on leaving calls `system_update_cpu_freq(160)`, `lcd::wake()`, `gc_mark_all_dirty()`. It exposes `screenOff()`, `wakeSoon()` and a status line.

`firmware/st7789.cpp`: `lcd::sleep()` sets the backlight pin high through `digitalWrite` (which also stops the PWM), sends 0x28 and 0x10. `lcd::wake()` calls the existing `lcd::begin()`: the hardware reset and init sequence that already runs at every boot on this exact panel, 290 ms once a day. It is preferred over a bare "sleep out" because it is proven code and also recovers a panel that lost its settings. Brightness, colour order and inversion are kept in the driver's statics and are applied again by `begin()`.

`firmware/main.cpp` (chunk R3-0F): the game is ticked whenever no upload runs; strips are rendered only when the display is on and `!nightmode::screenOff()`; the loop delay is 50 ms while the screen is off.

### 3.5 Access while asleep

- Home WiFi: the status page, the settings page and `/update` answer as always (replies may be 0.1 to 0.3 s slower because of modem sleep).
- The status page's first line reads "Asleep (screen off) until 07:00" with the buttons **Wake now** and, when awake, **Sleep now**.
- Rescue hotspot: the rules do not know about sleep. On for 10 minutes after every boot, on again 60 s after the home WiFi is lost. Its name and password are fixed (`GEEKQUEST-XXXX`, `GEEKQUEST`) and are written in `docs/RECOVERY.md`, so the dark screen does not hide anything the owner needs.
- No button is needed: unplug and replug gives a lit screen and the hotspot for 10 minutes.
- `docs/RECOVERY.md` gets one new row: "The screen is black at night: that is the sleep schedule. Open the page and press Wake now, or unplug and replug."

### 3.6 Cases

| Case | Behaviour |
|---|---|
| Sleep 23:30, wake 07:00 | Bedtime 23:25, screen off about 23:30, weather fetched 06:55, screen on 07:00. The game day changes at 00:00 while asleep. |
| Sleep 01:00, wake 09:00 | Same; the hero stays up in the night village until 00:55 (sitting, wandering, the cat). |
| NTP never synced | Stays awake. |
| Power cut and return at 03:00, settings kept (flash enabled or restored) | Screen on for 10 minutes, then bedtime sequence and off, as soon as NTP has synced. |
| Power cut, settings lost (no flash approval) | Schedule is off until restored (section 5.4). The clock stays lit. |
| Daylight saving change at night | The window follows local time; the night is one hour shorter or longer. |
| WiFi lost during the night | Hotspot comes on after 60 s as always; screen stays off; the clock keeps time by itself. |
| Update at night | Works. After the restart the screen is lit for 10 minutes. |

## 4. Feature 3: dialogue

### 4.1 Why it is dull today, and the fix

Today a chat is one villager line plus one hero reply drawn from 8 context-free strings, so every exchange ends in "I SEE!" or "TELL ME MORE!". The villager line knows only season, rain and friendship.

R3 replaces line picking by **scripts**: a script is a complete small conversation of 2 to 4 turns written as a unit, so each answer fits what was said, and either side can open. A script carries one condition; the engine picks among the scripts whose condition is true, prefers specific ones, and never repeats recent ones.

### 4.2 Data model (`talk_data.h`, generated)

```c
typedef struct {            // 4 bytes
  uint8_t  cond;            // TC_* below
  uint8_t  arg;             // meaning depends on cond
  uint16_t pack;            // bits 0..9 first line id, bits 10..11 turns - 2, bit 12 hero opens, bits 13..15 spare
} TalkScript;
// TALK_SCRIPTS[], grouped by speaker: TALK_FIRST[11] gives the first script of npc 1..9, of the shared pool, and the end.
// Lines of a script are consecutive ids; speakers alternate from the opener.
```

| Condition `cond` | `arg` | True when |
|---|---|---|
| `TC_ANY` | | always (personality lines) |
| `TC_TIME` | 0 morning (06..10), 1 midday (10..15), 2 evening (15..22), 3 late (22..06) | the game hour is in the block |
| `TC_WX` | `GQ_W_*` | `cal_weather_shown()` equals it |
| `TC_TEMP` | 0 cold (5 °C or less), 1 hot (28 °C or more) | live temperature exists and is in the band |
| `TC_SEASON` | 0..3 | game season |
| `TC_FEST` | 0 today is a festival day, 1 a festival is running | calendar |
| `TC_DID` | `TD_*`: chopped, fished, foraged, mined, fought, fainted, sold, bought a tool, built a project, ate, gifted, slept | the hero did it within the last 30 game minutes (`talk_on_event` remembers the last two) |
| `TC_QUEST` | 0 a quest of this villager is active, 1 a story quest is active, 2 no quest is active | quest state |
| `TC_CHAPTER` | 0..5 | story chapter reached |
| `TC_FRIEND` | 1..3 | friendship tier of this villager (25, 50, 75) |
| `TC_GOAL` | 0 saving for a tool, 1 saving for a project | current goal kind |
| `TC_EVENT` | 1..12 | today's daily event |

Selection (`talk_begin`): collect matching scripts of the villager and of the shared pool (bounded loop over at most 40 entries); weight 1 for `TC_ANY`, 2 for time and season, 4 for everything else; drop the last 8 scripts used (ring buffer) and any script already used with this villager today; draw by weight; if nothing is left, allow the ring again. Quest lines (`quest_talk`) keep priority over scripts as today, but are now followed by a quest-aware hero answer from the shared pool instead of a random reply.

Slots (expanded while decoding; the checker proves the worst case fits 19 characters):

| Slot | Text | Longest |
|---|---|---|
| `{T}` | live temperature with unit, for example `23C`, `-5C`, `104F` (scripts using it must have `TC_TEMP` or be in the "live" group) | 4 |
| `{W}` | weather word: SUN, CLOUDS, RAIN, STORM, FOG, SNOW, WIND | 6 |
| `{S}` | season word: SPRING, SUMMER, AUTUMN, WINTER | 6 |
| `{G}` | name of the current goal (`econ_goal_name`) | 9 |
| `{I}` | name of the focus item | 8 |
| `{N}` | name of the villager spoken to | 4 |

### 4.3 Compact encoding

Each line is stored as bytes: 0x00 ends the line; 0x01 is the line break; 0x02..0x07 are the six slots; 0x20..0x5F are the 64 font glyphs; 0x60..0xFF index a shared dictionary of 160 substrings ("THE ", "YOU", "ING", " IS ", ...). The generator builds the dictionary greedily (pick the substring of 2 to 8 characters with the largest saving, replace, repeat 160 times; ties broken alphabetically so the output is stable). Dictionary entries hold plain glyphs only (no nesting), so the decoder is one bounded loop of about 120 bytes of code. Measured on the existing game text this saves 33 to 35 %.

Size estimate:

| Part | Bytes |
|---|---|
| About 510 lines, average 26 characters plus terminator: 13.8 KB raw, at 33 % saving | 9,300 (hard limit 10,240 by `static_assert`) |
| Dictionary, 160 entries plus offsets | 850 |
| Line offsets, 510 x 2 | 1,020 |
| Script table, 178 x 4, plus index | 730 |
| Engine and decoder code | 2,400 |
| Removed: 234 ambient lines, 8 replies, their offsets, the fallback tables in `hero.cpp` | -7,300 |
| **Net change** | **about +7.0 KB** (budget 8.0 KB) |

Size lever if the image gets tight: the same encoding applied to the remaining quest strings saves about 1.2 KB. Not planned for R3.

### 4.4 Interface (`gq_mod.h`, declared by chunk R3-0C with stubs)

```c
void talk_init(void);
void talk_on_event(int ev, int a, int b);        // remembers what the hero just did; called from quest_on_event
int  talk_begin(int npc);                        // picks a script for npc 1..9; returns the number of turns, 0 = none
int  talk_turn(int i, char* buf, int cap);       // text of turn i with slots filled; returns 1 if the hero speaks, 0 the villager
enum { TALK_SOLO_BED, TALK_SOLO_WAKE, TALK_SOLO_DID };
int  talk_solo(int kind, char* buf, int cap);    // hero thinking aloud; 1 if there is a line
```

`chat_step()` phases 4 to 6 become a loop: show turn `i` through `ui_say` (hold 2,200 ms), wait for `ui_dialogue_done()`, next turn. A 4-turn exchange lasts about 14 s. Friendship gain and `GQ_EV_TALKED` are unchanged.

### 4.5 Content plan (for the writers)

| Speaker | Scripts | Lines | Voice |
|---|---|---|---|
| Mira (shop) | 20 | 55 | brisk, counts everything, dry jokes about prices |
| Bram (elder) | 20 | 55 | slow, proverbs, bell lore, his knees know the weather |
| Pip (child) | 20 | 55 | excited, asks questions, frogs, wants to be a hero |
| Odda (tavern) | 20 | 55 | warm, feeds everyone, hears all the gossip |
| Finn (fisher) | 20 | 55 | few words, sea and tide comparisons |
| Tess (smith, builder) | 20 | 55 | practical, measures, proud of every project |
| Luma (herbalist) | 20 | 55 | calm, mysterious, the forest speaks to her |
| Wren (merchant) | 20 | 55 | showy, far countries, always selling |
| Miso (cat) | 6 | 12 | MEOW, PRRR, the hero does the talking |
| Shared pool (any villager) | 12 | 30 | weather and temperature with `{T}` and `{W}`, festival, quest answers |
| Hero alone | | 30 | 6 bedtime, 6 waking (3 with `{T}`), 18 after-action remarks |
| **Total** | **178** | **about 510** | |

Each villager's 20 scripts, by condition: 4 `TC_ANY`, 2 `TC_TIME`, 3 weather or temperature (at least one `TC_TEMP`), 2 `TC_SEASON`, 1 `TC_FEST`, 3 `TC_DID` (things that villager cares about: Mira sold and bought, Finn fished, Tess built and mined, Luma foraged and fought, Odda ate, Bram fought and fainted, Pip fought and slept, Wren bought and gifted), 2 `TC_QUEST` or `TC_CHAPTER`, 3 `TC_FRIEND` (one per tier), 1 `TC_GOAL` or `TC_EVENT`. By length: 8 scripts of 2 turns, 9 of 3 turns, 3 of 4 turns. The hero opens at least 6 of the 20.

The hero's voice: cheerful, hungry, a little vain about his sword, never a filler. Every hero turn must answer the line before it.

Source format, one file per speaker in `content/talk/` (`mira.txt` ... `wren.txt`, `miso.txt`, `_shared.txt`, `_hero.txt`):

```
@ WX RAIN
N: RAIN KEEPS THE|SHOPPERS HOME.
H: THEN I AM YOUR|BEST CUSTOMER!
N: MY ONLY ONE.|BUY SOMETHING.

@ TEMP COLD
H: {T} OUT THERE!|MY NOSE IS ICE.
N: SCARVES, 9 COINS.|NOSES ARE FREE.
```

### 4.6 Validation (`tools/gen_talk.py --check`, and again in `test/test_talk.cpp`)

1. Every character is one of the 64 font glyphs, `|`, or a known slot; upper case only.
2. At most 2 lines per turn; every line is 1 to 19 characters **after the longest possible slot text is inserted**; no leading, trailing or double spaces; the decoded turn fits 40 bytes.
3. Every script has 2 to 4 turns; `cond` and `arg` are valid; `{T}` appears only in scripts that require live temperature.
4. Per villager: exactly the category and length counts of 4.5 (the checker prints a table and fails on any difference).
5. No line occurs twice anywhere. No hero turn is in the banned list (`TELL ME MORE!`, `I'LL KEEP WATCH.`, `SOUNDS GOOD!`, `SURE THING.`, `I SEE!`, `GOOD TO KNOW.`, `REALLY? NICE!`) or shorter than 8 characters.
6. Encoding round trip: every line decodes to its source for every slot value; blob at most 10,240 bytes; generated header is fresh in CI.
7. Runtime, 30 simulated days, 3 seeds, under ASan: every chat has at least 2 turns; no script repeats within 8 chats; at least 120 different scripts and 100 different hero lines are seen; no decoded text exceeds 39 characters; every condition kind fires at least once with the debug hooks.

## 5. Settings: page, record, persistence

### 5.1 The settings page

New page `GET /config` ("Clock settings"), linked from the status page; `POST /config` saves; `GET /config.json` returns the current values and state. All handlers live in `firmware/config.cpp`; the page is streamed from flash in pieces like every other page (no `String` of the page).

| Box | Fields |
|---|---|
| Weather | "Use live weather" tick; place search (browser-side, 2.2) with result buttons; latitude; longitude; place label; unit Celsius (default) or Fahrenheit; current reading, age and last error; "Try weather again" when strikes disabled it; attribution line |
| Sleep | "Use sleep schedule" tick; sleep time and wake time (`<input type="time">`); buttons Wake now and Sleep now |
| Storage | One line: "Kept in the clock's memory: survives restarts and updates, not unplugging" or, with flash enabled, "Stored permanently"; restore banner (5.4) |

Validation (pure, in `gqconf.cpp`, the whole form first, an invalid value changes nothing): latitude -90..90 and longitude -180..180, read from text with `.` or `,` and up to 6 decimals into hundredths of a degree without floating point; times `HH:MM`; sleep and wake must differ; label at most 23 characters from `A-Z a-z 0-9 space , . ' -` (the page's JavaScript strips accents; the clock HTML-escapes on output). Live weather cannot be ticked without coordinates.

### 5.2 The record

```c
// lib/gqsafe/src/gqconf.h   64 bytes, little-endian, no padding (static_assert on size and offsets)
typedef struct {
  uint32_t magic;        //  0  "GQC1"
  uint32_t crc;          //  4  CRC32 of bytes 8..63
  uint8_t  version;      //  8  1
  uint8_t  flags;        //  9  bit 0 live weather on, bit 1 Fahrenheit, bit 2 sleep schedule on
  uint16_t seq;          // 10  +1 per change
  int16_t  lat_c;        // 12  hundredths of a degree
  int16_t  lon_c;        // 14
  uint16_t sleep_min;    // 16  0..1439
  uint16_t wake_min;     // 18
  char     place[24];    // 20  NUL-terminated label
  uint8_t  reserved[20]; // 44  zero
} GqConf;
```

Defaults: everything off, coordinates 0, sleep 23:00, wake 07:00. Every load goes through `gqc_check` (magic, version, CRC) and `gqc_sanitize` (ranges), like the existing settings.

### 5.3 Where it is kept

Stage C0, always on, no approval needed for safety: RTC user memory blocks 32..47 (64 bytes, in the range freed by the removal of the game save; P-2). RTC memory is RAM: no wear, cannot harm flash, survives restarts, crashes and updates, lost when unplugged. Updated RTC block map:

| Blocks | Content | Owner |
|---|---|---|
| 0..31 | boot loader (update command): never touched | |
| 32..47 | `GqConf` (R3) | `config.cpp` |
| 48..79 | free | |
| 80..91 | `GqSettings` | `persist.cpp` |
| 92..95 | as in the base | |
| 96..111 | `RtcState`, `RtcState2` (two spare words become weather busy flag and weather strikes) | `safety.cpp` |
| 112..127 | network event log | `netwatch.cpp` |

Stage C1, **NEEDS OWNER APPROVAL**, compiled out by default (`-DGQ_CONFIG_FLASH=0`): one flash sector.

Which sector, and why:

| Candidate | Verdict |
|---|---|
| 0x100000..0x3F9FFF, the stock firmware's files | **Never.** |
| 0x3FB000, EEPROM sector (stock firmware's settings) | **Never.** |
| 0x3FC000..0x3FFFFF, radio calibration and SDK WiFi data | Never (only the SDK writes there). |
| A sector in the 1 MB code area above the image | Rejected: the updater overwrites it, and a wrong address there erases running code. |
| **0x3FA000**, the 4 KB gap between the end of the file system (`_FS_end`) and the EEPROM sector | **Chosen.** It is unused in every standard 4 MB Arduino layout. It is the sector `DESIGN.md` 9.4 proposed for game saves (approval A1); with game saves gone, C1 is the smaller successor of A1: settings only. |

Format (pure logic in `lib/gqsafe/src/cfgstore.cpp`, host-tested against a simulated flash): a 16-byte header (`"GQF1"`, erase count, CRC) and 31 slots of 128 bytes: `GqSettings` (48), `GqConf` (64), sequence number, CRC32, and a commit word written last in a separate write. Writing appends to the first blank slot and reads back. The newest valid slot wins. The sector is erased only when all 31 slots are used, or by "Disable and erase". A torn write leaves an invalid slot and the previous one is used.

The three gates of `DESIGN.md` 9.4 apply unchanged: (1) build flag; (2) a read-only probe line on the page ("Settings sector 0x3FA000: blank", "GeekQuest settings, N slots used", or "IN USE BY SOMETHING ELSE" with the first 16 bytes, in which case every write refuses and there is no override) plus the CRC of the stock settings sector so the owner can see it never changes; (3) nothing is written until the owner presses "Store settings permanently". The address is one constant; at boot the feature disables itself unless `_FS_end` is sector 0x3FA, `_EEPROM_start` is sector 0x3FB and the real chip size is 4 MB.

Write policy: 10 s after the last settings change, at least 60 s after the previous write, at most 20 writes a day, never in the golden window (a change made in the first 10 minutes is written when the window ends; the page says so), never during an upload, never in safe mode.

Risks of C1, for the owner's decision:

| Risk | Size | Answer |
|---|---|---|
| The stock firmware uses that sector after all | Low; would damage stock data, not the boot path or the update page | Probe before any write; refuses on foreign content |
| A bug writes a wrong address | Very low; the neighbours are stock's last file block and stock's settings | One constant, boot-time symbol checks, flash calls in one file (lint), host tests with bounds |
| Power cut while writing | One slot lost | Previous slot is used |
| Power cut during the erase (once per 31 changes) | Settings back to defaults | Restore from the phone (5.4) |
| Wear | None in practice | One slot per change |
| Going back to stock | Leftover data in the gap | "Disable and erase" first; harmless if forgotten and stock does not use the sector |

### 5.4 Fallback without flash: the phone remembers

After a successful save the page stores the form values in the browser's `localStorage`. When the page is opened and `/config.json` reports `"defaults":true` while the browser holds saved values, a banner appears: "The clock lost its settings (it was unplugged). Restore from this device?" with one button that posts them. Cost about 350 bytes of JavaScript, no flash writes, no approval needed. Limits: it needs the owner to open the page once, and it works per browser.

## 6. Recovery impact

| Item | Change | Why it is safe |
|---|---|---|
| `safety.cpp` | (1) `ESP8266WebServer& web()` so `config.cpp` can register its routes, skipped in safe mode; (2) the `Out` writer and `beginPage/endPage` move unchanged to `firmware/webout.h`; (3) one link and one hook line on the status page and in `/info`; (4) `wxMark(bool)`, `wxStrikes()`, `wxClearStrikes()` using two spare words of `RtcState2`; (5) `bootKind()` for the boot grace. `GQ_RECOVERY_REV` becomes 3. | No existing route, timer or restart rule changes. Safe mode registers no new route and runs neither weather nor sleep. |
| `main.cpp` | Calls `config::begin`, `wxnet::loop`, `nightmode::loop`; render is skipped while the screen is off. | `safety::begin()` stays first in `setup()`, `safety::loop()` first in `loop()` (lint L4). |
| Update page | Not touched. Reachable awake and asleep, over home WiFi and hotspot. | |
| Golden window | Unchanged. Weather starts no earlier than 40 s after boot; flash config writes never happen in it. | |
| CI: `check_bin.py` | Forbidden symbols gain `br_ssl_`, `BearSSL`, `WiFiClientSecure`, `AsyncClient`. Required strings gain `/config`. Builds without `GQ_CONFIG_FLASH` must not contain `GQF1`. Warning above 440 KB; failure above 500 KB as today. | |
| CI: `lint_safety.py` | L2: flash calls only in `firmware/cfgflash.cpp` (and `persist.cpp` if it still has any). L9: `deepSleep`, `forceSleep`, `wifi_fpm_`, `LIGHT_SLEEP`, `WIFI_OFF` nowhere in `firmware/`. L10: `tcp_`, `dns_gethostbyname`, `pbuf_` only in `wxnet.cpp`; `wxnet.cpp` contains no `WiFi.` call. L11: `system_update_cpu_freq`, `lcd::sleep`, `lcd::wake` only in `nightmode.cpp`; `safety::web()` only in `config.cpp`. L8: `GQ_CONFIG_FLASH` listed with the approvals, default 0. Each new rule gets a fixture. | |

## 7. Budgets

Flash in KB, static RAM in bytes, heap as peak transient use. The cap for R3 is **26 KB of flash** (expected 21.3), **800 bytes of static RAM**, and free heap never under 14 KB because of R3 code.

| Chunk | Flash expected | Flash cap | Static RAM | Heap |
|---|---|---|---|---|
| R3-0F firmware plumbing and gates | 0.6 | 1.0 | 40 | 0 |
| R3-0C core plumbing | 0.8 | 1.2 | 40 | 0 |
| W1 weather client | 3.6 | 4.5 | 320 (request 200, parser 64, state) | 3 KB for under 1 s, every 30 min |
| W2 weather in the core and HUD | 1.2 | 1.6 | 16 | 0 |
| P1 settings page and record | 5.5 (page 3.0) | 6.5 | 200 | none beyond the web server's own; 480-byte stack buffer |
| P1 with `GQ_CONFIG_FLASH=1` | +1.6 | +2.0 | +140 | 0 |
| S1 sleep | 1.6 | 2.2 | 40 | 0 |
| D1 + D2 dialogue | 7.0 net | 8.0 | 24 | 0 |
| **Sum (default build)** | **20.3** | **25.0** | **680** | **3 KB peak** |

Every chunk states its measured delta (from the CI size report) in its hand-off note. A chunk over its cap does not merge until the lead agrees. Stack: core rule of 256 bytes per function stays; lwIP callbacks stay under 128 bytes.

## 8. Work breakdown

### 8.1 Order

```
base (P-1..P-4)
  -> R3-0F  firmware plumbing + gates      (serial, one engineer, reviewed by the lead line by line)
  -> R3-0C  core plumbing                  (serial; may run beside 0F, no shared files)
  -> W1, W2, P1, S1, D1                    (parallel, disjoint files)
  -> D2a..D2d content writers              (start when D1 has committed the checker and the Mira model file)
  -> integration                           (merge, soak, size, documents, owner checklist)
```

After 0F and 0C the image must build, pass every gate and behave exactly like the base (all new modules are stubs that do nothing). That build is a safe point: it can be flashed.

### 8.2 File ownership (each file has exactly one owner in R3)

| Chunk | Files |
|---|---|
| R3-0F | `firmware/safety.cpp`, `safety.h`, `main.cpp`, new `webout.h`, new stubs with final headers `config.h/.cpp`, `wxnet.h/.cpp`, `nightmode.h/.cpp`; `platformio.ini`; `tools/check_bin.py`, `check_bin_selftest.py`, `lint_safety.py`, `tools/lint_fixtures/*`; `.github/workflows/build.yml` |
| R3-0C | `lib/gamecore/src/gamecore.h`, `gq_types.h`, `gq_mod.h`, `game.cpp`, `hero.cpp`, new `talk.cpp` (stub that returns today's lines), `preview/*`, `tools/build_preview.sh`, new `test/test_r3_api.cpp` |
| W1 | new `lib/gqsafe/src/wxparse.h/.cpp`, `wxfsm.h/.cpp`; `firmware/wxnet.cpp` (body); new `test/test_wx.cpp`, `test/fixtures/wx/*` |
| W2 | `lib/gamecore/src/calendar.cpp`, `weather.cpp`, `ui.cpp`; `test/test_calendar.cpp`, `test/test_ui.cpp`; `assets/src/items.png` and the regenerated `assets_gen.h` only if cells 40..47 need repainting |
| P1 | new `lib/gqsafe/src/gqconf.h/.cpp`, `cfgstore.h/.cpp`; `firmware/config.cpp` (body), new `firmware/cfgflash.h/.cpp`; new `test/test_config.cpp`, `test/config_page_server.py` |
| S1 | new `lib/gqsafe/src/sleepfsm.h/.cpp`; `firmware/nightmode.cpp` (body), `firmware/st7789.cpp`, `st7789.h`; new `test/test_sleep.cpp` |
| D1 | `lib/gamecore/src/talk.cpp` (body), new `talk_data.h` (generated), `quest.cpp`, `text_data.h` (regenerated), `tools/gen_quest_data.py`, new `tools/gen_talk.py`, `content/talk/mira.txt`, `_shared.txt`, `_hero.txt`, `miso.txt`; new `test/test_talk.cpp`, `test/test_quest.cpp` |
| D2a..D2d | `content/talk/bram.txt` + `pip.txt`; `odda.txt` + `finn.txt`; `tess.txt` + `luma.txt`; `wren.txt` |
| Integration | `test/test_soak.cpp`, `docs/RECOVERY.md`, `README.md`, `docs/DESIGN-R3.md`, anything that needs a cross-chunk fix |

`hero.cpp` is changed only by R3-0C (the chat loop and the sleep hook are both written there, against the stub `talk_*` functions), so D1 and S1 never touch it.

### 8.3 Chunks

**R3-0F: firmware plumbing and gates.** Steps: move `Out` to `webout.h` with no behaviour change; add the five additions of section 6; add the calls in `main.cpp`; create the stub modules with the final headers below; add the lint rules with fixtures and the `check_bin` rules; add the `quest` build flag `-DGQ_CONFIG_FLASH=0`. Commit message carries `RECOVERY-CHANGE:`.

```cpp
namespace config    { void begin(bool safeMode); void loop(); const GqConf& get(); bool isDefault(); void statusLine(char* buf, size_t n); }
namespace wxnet     { void begin(bool safeMode); void loop(); bool reading(int* wx, int* temp_c10, int* flags); void kick(); void statusLine(char* buf, size_t n); }
namespace nightmode { void begin(bool safeMode, bool display); void loop(); bool screenOff(); bool wakeSoon(); void command(uint8_t cmd); void statusLine(char* buf, size_t n); }
```

Acceptance: every CI gate green; `tools/check_bin_selftest.py` and the lint self-test cover each new rule; the status page and `/info` are byte-identical to the base except the new link and line (checked by diffing the page sources); image grows by at most 1.0 KB; the owner's five-step smoke checklist passes on hardware before any other R3 firmware chunk is flashed.

**R3-0C: core plumbing.** Steps: add `gc_set_live_weather`, `GC_OPT_SLEEP_MODE`, `GQ_Q_HERO_ABED`, the `cal_*` and `talk_*` declarations with stubs (`cal_live()` returns 0; `talk_begin` returns 2 turns built from `quest_ambient_line` and `quest_reply_line`); rewrite the chat loop of `chat_step()`; add `sleep_due()` and the bedtime interruption; add preview controls (live weather state and temperature, unit, sleep mode, force a chat with villager N). Acceptance: all existing host tests pass unchanged; `test_r3_api.cpp` shows that mode 2 puts the hero in bed within 5 simulated minutes from any activity on 20 seeds, that mode 1 keeps him up through a whole night, that mode 0 behaves as before, and that presence and liveness invariants hold in all three; a 4-turn stub script plays in order.

**W1: weather client.** Steps: `wxparse`, `wxfsm` with tests first; then `wxnet.cpp` following section 2.6. Acceptance: the tests of 9.2; `wxnet.cpp` passes lint L10 and contains no `delay`, `String` or `while` on network state; a review checklist (every rule of 2.6 ticked with the line number) is in the hand-off note, because this file cannot be run in the container.

**W2: weather in the core and HUD.** Steps: live override in `calendar.cpp` (rules of 2.7), particles and look on `cal_weather_shown()`, HUD and clock card. Acceptance: tests of 9.3; preview screenshots of the HUD for `7C`, `23C`, `-12C`, `104F`, stale, and no live data, each looked at; the icon cells 40..47 are recognisable at 16 px (repaint if they are placeholders).

**P1: settings page and record.** Steps: `gqconf` and `cfgstore` with tests; `config.cpp` (RTC load and store, routes, page, `localStorage` restore, "Try weather again", wake and sleep buttons that call `nightmode::command`); `cfgflash.cpp` behind the flag. Acceptance: tests of 9.4; the page works in a desktop browser against a 60-line Python stand-in for the three routes (`test/config_page_server.py`), including search, hand entry, restore banner and every validation message; default build contains neither `GQF1` nor any flash call.

**S1: sleep.** Steps: `sleepfsm` with tests; `lcd::sleep` and `lcd::wake`; `nightmode.cpp`. Acceptance: tests of 9.5; lint L9 and L11; review checklist in the hand-off note.

**D1: dialogue engine.** Steps, in this order: (1) `gen_talk.py` with `--check`, the file format, and the complete model file `mira.txt`, committed first so the writers can start; (2) encoder, `talk_data.h`, decoder and selection in `talk.cpp`; (3) remove the ambient and reply strings from `gen_quest_data.py` and `quest.cpp`; (4) `test_talk.cpp`; (5) `_shared.txt`, `_hero.txt`, `miso.txt`. Acceptance: validation of 4.6; preview screenshots of one 2-turn, one 3-turn and one 4-turn exchange and of a `{T}` line.

**D2a..D2d: writers.** Each writes the files of 8.2 to the plan of 4.5 in the voices given, runs `gen_talk.py --check` until it prints no error and the category table matches, and reads every script aloud in order as a last check that each answer follows from the line before. No code.

**Integration.** Merge order W2, D1, D2, P1, S1, W1 (the piece with the most new firmware last); extend `test_soak.cpp` (30 days with live weather changing every 30 minutes and a nightly sleep window); CI size report against the caps; update `docs/RECOVERY.md`, `README.md`; produce the owner checklist of 9.7.

### 8.4 Merge risks

| Risk | Answer |
|---|---|
| The size-reduction work changes `safety.cpp`, `main.cpp`, `persist.*`, `st7789.*` while R3 is planned | R3 starts only from the merged base; R3-0F re-reads those files and adapts names. No R3 branch is cut earlier. |
| Two chunks need `hero.cpp` (sleep, dialogue) | Both changes are made by R3-0C against stubs. |
| `ui.cpp` is needed for HUD (W2) and might be wanted by dialogue | The dialogue box is not changed; D1 has no reason to open `ui.cpp`. |
| `quest.cpp` and the text generator (D1) against the quest tests | D1 owns `test_quest.cpp` and updates the string-count checks in the same commit. |
| Stub headers turn out to be insufficient | The engineer stops and asks the lead; the header owner (0F or 0C) makes the change; nobody edits another chunk's file. |
| Content arrives over the 10,240-byte limit | Writers shorten lines; the checker prints the encoded size after every run. Cut order if still over: 4-turn scripts become 3-turn, then the 18 after-action remarks go. |
| Firmware glue cannot be compiled locally | Thin glue, logic in host-tested pure code, CI build on every push, review checklists, and flashing in the order of 9.7. |

## 9. What the engineers must test

### 9.1 Rules for every chunk

1. Pure code first, with its host test, compiled with `-fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror`. No firmware glue is written before the pure tests pass.
2. Every test file contains negative cases (bad input is refused and changes nothing), boundary cases (the limits of every range, one below and one above), and time cases that step `millis()` across the 49.7-day wrap.
3. Anything visible is checked in the preview with a Playwright screenshot at scale 1, and the engineer looks at the picture before claiming it works.
4. The hand-off note states: tests added and passing, the CI size and RAM delta against the cap, screenshots looked at, lint result, deviations from this document. "Should work" without a test or a checklist line is not accepted.
5. No chunk edits a file it does not own, and none touches the approvals: `GQ_CONFIG_FLASH`, `GQ_AUTO_RESTART`, `GQ_STRICT_UPLOAD` stay 0.

### 9.2 `test_wx.cpp` (W1)

- The documented reply and the owner's captured reply parse to the right four values.
- The same reply fed in every possible split into two pieces, and in pieces of 1, 2, 7 and 536 bytes, gives the same result.
- Every prefix of the reply (truncation at each byte) gives a failure code or the right values, never wrong values.
- Chunked transfer encoding parses; a chunk boundary inside a number parses.
- Refused: status 301, 400, 404, 500; missing key; key present only in `current_units`; values out of range; a body over 2,048 bytes; empty body.
- Key order shuffled, extra keys, extra whitespace, negative and zero temperatures, `-0.4`, `12`, `12.96` (rounds to 13.0).
- 100,000 random and mutated inputs under ASan: no crash, no out-of-bounds read, and `WXP_OK` only with all values inside the ranges.
- Mapping: every WMO code 0..99 with wind 0 and 40 gives a state in 0..6 and matches the table of 2.4.
- Request builder: latitudes and longitudes at both extremes, negative values under 1 degree (`-0.50`), buffer too small returns 0.
- `wxfsm`, simulated for 7 days: 48 fetches a day give or take 2 when all succeed; the retry ladder when all fail; no start during upload, safe mode, link down, low heap, the first 40 s, screen off (except `wake_soon`), or with 2 strikes; `WXA_ABORT` exactly at 8 s; age classes at 65 and 125 minutes; `wxfsm_kick` leads to a fetch within 3 s; correct across the `millis()` wrap.

### 9.3 `test_calendar.cpp` and `test_ui.cpp` (W2)

- Live state replaces the simulated one for `cal_weather_shown()`; `GC_WX_NONE` restores the simulated state on the next tick; the simulated machine's own sequence is identical with and without live data (same seed, same states underneath).
- Storm cap: after 120 minutes of live STORM in one game day `cal_weather()` is RAIN while `cal_weather_shown()` is STORM; the allowance resets at the day change.
- `GQ_EV_WEATHER` fires once per change of `cal_weather()`.
- Conversion: -40 C is -40 F, 0 C is 32 F, 37.8 C is 100 F, rounding at .5 in both signs; clamping of absurd inputs.
- HUD: for every temperature from -99 to 199 in both units, strip 0 is rendered and no pixel at x of 178 or more differs from the render without temperature (the clock is never overlapped), and no pixel left of x=100 differs; the stale colour is used when flagged; without live data the strip is identical to the base render.
- Particle and dirty-strip budgets of `DESIGN.md` 6.1 still hold with the live state changing every simulated 30 minutes.

### 9.4 `test_config.cpp` (P1)

- Record: size and offsets; round trip; a flipped bit anywhere is refused; unknown version refused; sanitize clamps every field.
- Coordinate text: `38.72`, `-9.14`, `38,72`, `+5`, `-0.004` (rounds to 0), `90`, `90.01` (refused), `abc`, empty, 40 digits (refused, no overflow).
- Time text: `00:00`, `23:59`, `24:00` (refused), `7:5` (refused or read as 07:05; choose one and test it).
- Label: allowed characters pass, `<script>` is refused.
- `cfgstore` against a simulated flash that can only clear bits: append and read back; newest slot wins; a write cut after every possible byte count leaves the previous settings readable; a cut erase gives defaults, not garbage; a sector with foreign content is never written or erased; 1,000 saves cause at most 33 erases; no access outside the 4,096 bytes (the simulated flash has guard areas checked by the test).

### 9.5 `test_sleep.cpp` (S1)

- Window table: for (sleep, wake) in (23:30, 07:00), (01:00, 09:00), (13:00, 15:00), (00:00, 23:59), (23:59, 00:00), every minute of the day is classified correctly, including the 5-minute bedtime lead across midnight.
- A full simulated week at one-second steps: exactly one sleep and one wake per day, screen-off no later than 120 s after sleep time even if the hero never reports being in bed.
- Clock invalid: never asleep. Clock becomes valid inside the window: bedtime, then asleep.
- Time jumps of -60, +60 and +600 minutes at arbitrary moments settle within 2 steps and never produce two sleeps in one window.
- Boot grace: 10 minutes lit after power-on, crash, user restart and update; the 60 s rule after an automatic restart.
- Commands: wake now inside the window stays awake until the window ends, then the next night sleeps normally; sleep now outside the window sleeps until the next wake time and at most 12 h.
- No transition to asleep while updating; none in safe mode; schedule disabled gives hero mode 0.
- Correct across the `millis()` wrap.

### 9.6 Dialogue (D1, D2)

The checks of 4.6. In addition the lead reads 30 randomly drawn scripts in the preview before integration.

### 9.7 On hardware (the owner, in this order; one flash each)

| Flash | Contains | Check |
|---|---|---|
| R3-a | plumbing only (0F, 0C) | The standard five-step smoke checklist. The clock behaves as before. Free heap, FPS and longest loop noted. |
| R3-b | dialogue, weather in the core, settings page, sleep | Smoke checklist. Set a sleep time 10 minutes ahead and a wake time 5 minutes later: the hero goes to bed, the screen goes dark, the status page and `/update` still open, "Wake now" works, the screen returns at wake time. Unplug and replug inside the window: 10 minutes of lit screen and the hotspot. Chats have several turns. |
| R3-c | weather client | Smoke checklist. Enter a place: within a minute the HUD shows icon and temperature and the page shows the reading. Note free heap and "lowest heap during fetch". Switch the router's internet off for 3 hours: the temperature turns grey, then disappears; the clock, the page and the frame rate are unaffected. After 24 hours: crash counter still 0, lowest heap above 14 KB. |
| R3-d (only if C1 is approved) | flash settings, probe only at first | Report the "Settings sector" line. After a few days press "Store settings permanently", unplug, replug: settings are still there. |

## 10. Decisions that need the owner's approval

Nothing in this list may be enabled on the assumption that the answer is yes.

| Id | Decision | Why it needs approval | Recommendation | If declined |
|---|---|---|---|---|
| **B1** | **Changes to the recovery layer's files**: `safety.cpp` (web server accessor, page link, weather strike words in RTC, revision 3), `main.cpp`, the CI gate and lint rules (section 6) | Proven recovery code is edited | Approve; flash R3-a alone first and run the checklist | R3 cannot have a settings page; no feature ships |
| **B2** | **Outgoing plain-HTTP request to `api.open-meteo.com` every 30 minutes** once a place is set | First code that talks to the internet besides NTP; location (to about 1 km) travels unencrypted; no TLS | Approve; it is inert until a place is entered, and guarded by strikes, heap gates and safe mode | No live weather; simulated weather stays |
| **B3** | **Sleep state**: screen off, 80 MHz, no rendering; WiFi and web server unchanged; 10 minutes of lit screen after every power-on | The clock is dark for hours; the owner must know the page and the replug rule | Approve; it is inert until a schedule is entered | No sleep schedule |
| **B4** | **Settings in flash sector 0x3FA000** (`GQ_CONFIG_FLASH`), in the three steps of 5.3 | Flash is erased and written outside the updater; unverified that stock leaves the sector alone | Approve step 1 (probe build) after R3-c has run for some days; decide on enabling after reading the probe line | Settings survive restarts and updates only; after unplugging, one tap restores them from the phone (5.4) |
| B5 | City search runs in the owner's browser and contacts Open-Meteo's geocoding service from the phone | The phone, not the clock, sends the typed place name to a third party | Approve | Latitude and longitude are typed by hand |
| B6 | RTC blocks 32..47 hold the R3 settings; two spare RTC words hold the weather strike state | Transparency (RAM only, no flash) | Approve | |
| B7 | Seasons stay on the 28-day game calendar; real weather may show snow in a game summer, drawn with the winter colours | Design choice | Approve | Alternative: real seasons by date and hemisphere; needs new pacing for festivals and story |
| B8 | A real storm stops the hero's outdoor work for at most 2 hours per day | Design choice | Approve | Real storms stop outdoor work for as long as they last |
| B9 | With a schedule set, the hero's bedtime follows the owner's times instead of 22:00 to 06:00 | Design choice | Approve | |
| B10 | Open-Meteo's terms: free for non-commercial use, attribution on the settings page | Third-party terms | Approve | Plan B (own OpenWeatherMap key) |
| B11 | Information needed (P-4): open the test address of 2.3 and send the text shown | Confirms plain HTTP works from the owner's network and gives the real test fixture | Do before W1 starts | W1 is built on the documented format and checked at R3-c |

Unchanged and still off: A1 game saves to flash (superseded by B4 for settings), A2 automatic restarts, A3 modem sleep off, A4 strict upload checks.

## 11. Risks

| # | Risk | Likelihood | Impact | Answer |
|---|---|---|---|---|
| 1 | Open-Meteo does not serve plain HTTP, or stops | Low to medium | Feature dark | P-4 before work; fallback to simulated weather; plan B |
| 2 | A bug in `wxnet.cpp` (use after free in a callback) crashes the clock | Medium (cannot be run before CI and hardware) | Low: crash counters, strikes, safe mode; update page unaffected | Thin glue, binding rules of 2.6, review checklist, flashed last (R3-c) |
| 3 | Heap pressure during a fetch while a page is served | Low | Page slow or fetch fails | Heap gates, 2 s rule after a request, 1 KB reply |
| 4 | The panel does not come back from sleep | Low | Screen dark until replug; page unaffected | Wake uses the full boot init; replug always re-inits |
| 5 | The owner forgets the schedule and thinks the clock is dead | Medium | Confusion | Status page line, RECOVERY.md row, replug gives 10 lit minutes |
| 6 | Image over budget | Low after the size reduction | CI blocks | Caps per chunk, warning at 440 KB, cut list in 8.4 and 4.3 |
| 7 | Parallel chunks drift at the interfaces | Medium | Schedule | Headers fixed by 0F and 0C with working stubs |
| 8 | Dialogue text quality | Medium | Cosmetic | Voices and counts fixed, checker, lead reads a sample |
| 9 | Unverified facts (lwIP callback behaviour in this core version, sizes, power figures) | Medium | Rework in W1 or S1 | Stated in section 1; CI build; staged flashing |
