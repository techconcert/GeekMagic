# Recovery guide (for the owner of the clock)

You do not need to know how to program to use this page. It tells you what to do when something goes wrong, what to check before you install a new version, and what to look at after you install one.

Two addresses matter:

- Your clock on the home WiFi: `http://<IP address>/`. The IP address is shown on the bottom band of the clock screen (the band cycles every 4 seconds between what the hero is doing, the IP address and the uptime) and is in your router's list. (`geekquest.local` is no longer answered in R2: it needed 19 KB of firmware.)
- The clock's own rescue network: join the WiFi called `GEEKQUEST-XXXX` (the password is `GEEKQUEST`), then open `http://192.168.4.1/`. This network is switched on for the first 10 minutes after every power-on, and whenever the clock cannot find your home WiFi.

The page `/update` (for example `http://192.168.4.1/update`) installs a new version of the firmware. It is the most important page: every version of GeekQuest has it, and the clock starts it before it starts the game, so a game problem cannot hide it.

## What can go wrong, and what to do

| What you see | What to do |
|---|---|
| The clock works, but something in the game looks wrong | Open the update page and install the last version you trusted (the file in `firmware/known-good/`, or an older file from `firmware/obsolete/`). |
| The screen shows SAFE MODE and the name of the rescue network | The game crashed several times in a row, so the clock stopped running it. The update page still works. Install a fixed version, or press **Restart** on the status page to try the game again. |
| The game crashes every few minutes | After 5 crashes the clock goes into safe mode by itself. Then install a fixed or older version. |
| The game state looks like nonsense | Press **Restart** on the status page: the game always starts again on day 1 (R2 does not save the game anywhere). |
| The screen is black or garbled | The web page does not depend on the screen. Open it over WiFi or through the rescue network. |
| The clock is no longer on your home WiFi | Unplug it, plug it in, and within 10 minutes join `GEEKQUEST-XXXX` and open `http://192.168.4.1/wifi` to enter the WiFi name and password again. |
| The clock lost the WiFi because the router was off | Nothing to do. The clock keeps trying with longer and longer pauses and connects again by itself. The bottom band shows the rescue network name and password meanwhile. |
| The time shows `--:--` | The clock has not reached a time server yet (or the power was cut). It fixes itself once the WiFi is back. |
| You want the original clock firmware back | On the update page choose the original `SDPro_V1.0.6...bin` file. |
| A file you chose on the update page is refused | Read the message. Nothing was installed and the clock keeps running. (Today the extra checks only warn; see below.) |
| No rescue network after unplugging and replugging, and no web page | This is the one case that cannot be fixed without a cable and a serial adapter. Everything in this guide exists to keep it from happening. |

Things that are **safe** to do at any time: opening any page, **Restart**, **Restart in safe mode**.

## Your game and settings: what survives what

**R5 saves settings and the game to flash** (approved by the owner on 2026-11-11): only sectors 0x80000-0x82FFF, by `firmware/store.cpp`, as a ring log of CRC-checked 320-byte records (36 slots, newest valid record wins, a torn write is ignored). It never runs in safe mode, during a firmware update, or if the image is above 0x7D000 bytes; writes are rate limited and verified; after 2 or more crashes the saved game is kept aside and not loaded. If anything looks wrong, flash the previous build or stock firmware: both ignore the area. The status page lines "Saved to flash" and "Loaded from flash" show what happened. Before R5 the game was not saved at all.

| Event | Settings | Game |
|---|---|---|
| Restart button, update, crash, watchdog, automatic restart | Kept (stored in the clock's short-term memory). | Starts again on day 1. |
| Unplugging the clock | Back to defaults. | Starts again on day 1. |

The status page also shows read-only lines about two flash areas: **Stock data sector 0x3FA000** (known to hold the stock firmware's own data, never to be written) and **Free sector 0x80000** (blank, or in use with its first 16 bytes, plus where the running image ends). R3 showed that 0xFF000 is in use (a firmware update is staged there), so the probe now looks at 0x80000, which lies above the running image and below any staged update. A later release may use it to keep settings and the game across power cuts, only after the owner has seen it blank on the real clock and approved. R2 never writes or erases flash by itself; the only flash writing is the firmware update through the update page.

## Switches

- **Automatic restarts** (approved in R2, on): when WiFi has been gone for hours and the ladder of reconnect attempts did not help, when memory stays very low for a minute, and once a week at night (04:00 to 04:30 when the clock knows the time, otherwise while the hero sleeps) if nobody has used the web page for 5 minutes and nobody is on the rescue network. They are not allowed in the first 30 minutes after power-on, never during a firmware upload (a pending automatic restart waits until the upload is over), and every restart counts in the crash counters: if the clock keeps restarting it ends in safe mode. The weekly restart can be switched off on the status page.
- **Strict upload checks** (refuse a file that arrived damaged or is not a GeekQuest build). Still off: the update page shows the file's CRC32 number and warns, and the clock records what the check would have said, but it installs exactly as before. Needs your written OK.
- **WiFi modem sleep off** (`GQ_NET_NO_SLEEP`, about 0.25 W more power). Approved 2026-11-11 and ON (the backend page was dropping while the hotspot stayed up).
- **Flash saving**: does not exist in R2 (it is planned for a later release, after the sector probe above has been seen on the real clock).

R7 adds a browser-side "Check for a new version" button to the update page. It runs only in your browser (GitHub is read from there), the clock sends nothing to GitHub, and the downloaded file goes through exactly the same upload route and checks as a file you pick by hand. R7 also removes the serial debug output (Serial is no longer used anywhere).

## Before you install a new version (pre-flash checklist)

1. The file comes from the `builds` branch, folder `firmware/` (not `obsolete/`), its name starts with `SDP_GeekQuest_`, and the GitHub check for that version is green.
2. You have the last known-good `.bin` and the original `SDPro_V1.0.6` file on the phone or computer you are installing from.
3. The clock has steady power and will not be moved for two minutes.
4. On the clock's status page: **Room for update** is larger than the new file, and the crash counter is 0.
5. Nothing to back up: settings and game are saved in the clock (R5 and later) and survive the update.
6. Choose the file on the update page, check the CRC32 and the message under it, press **Upload and install**. Wait for **Installed N bytes**. Do not unplug for 60 seconds.
7. If the page does not come back within 2 minutes: unplug the clock and plug it in once, join `GEEKQUEST-XXXX` within 10 minutes, open `http://192.168.4.1/`.

## After every install (hardware smoke checklist)

For every version:

1. The status page opens over your home WiFi and shows the new firmware version.
2. For the first 10 minutes `GEEKQUEST-XXXX` is visible. Join it and open `http://192.168.4.1/update`: the update page appears (do not upload anything).
3. `/update` also opens over the home WiFi.
4. **Restart in safe mode** shows SAFE MODE with the rescue network name and the status page works. **Restart** brings the game back.
5. Note from the status page: free heap, FPS, longest loop, last restart reasons.

For the recovery version (R1) in addition:

6. Change the time zone, brightness and colour depth, then press **Restart**. They are still set; the game starts again on day 1.
7. Unplug and plug in: settings are back to defaults and the game is on day 1 (expected).
8. The status page has the **Network** and **Health** boxes. Open `/info`: one line of JSON you can paste into a bug report.
9. Choose a random file (not firmware) on the update page: you get a warning and the clock keeps running. (With the strict checks off the upload would still be accepted if you pressed the button, so do not.)
10. Choose the original `SDPro` file but **do not press upload**. Write down the CRC32 the page shows and send it to the maintainer (it lets the clock recognise the original firmware exactly).
11. Write down the **Free sector 0x80000** line on the status page (blank, or in use with its first bytes, and the 'image ends at' text) and send it to the maintainer: it decides whether a later release may save settings there.
12. Watch the bottom band for a minute: it alternates between the hero's state (for example GOING TO EAT, QUEST: CAVE, CHOPPING), the IP address and the uptime.

For the content version (R2) in addition: the hero is on screen or clearly marked at all times; after a week at least 3 goals bought, the season changed once, and the crash counter has not gone up.

## What the checks in the build protect

Every `.bin` is refused by the build (no file is produced) unless it has the update page, the rescue network, safe mode and WiFi routes, fits in 500 KB, uses the original flash layout, contains no code that could touch the original firmware's files or settings, and uses about the same amount of memory as before. See `tools/check_bin.py`.

### What changed in these checks in R2, and why nothing was weakened

- `check_bin.py`: the required string for the `/backup` route is gone because the route is gone (backup, save, factory reset, quarantine and flash routes were removed together with the save system). Every recovery string is still required: the update page marker (exactly once), `/update_ota`, `/update`, the Updater's "Not Enough Space", the `GEEKQUEST-%02X%02X` rescue network name, `GQ-RECOVERY-REV`, `/safemode`, `/wifi`; a new one was added for the read-only save-sector probe line.
- `check_bin.py`: the rule "the flash journal `GQJ1` may only be present in flash-saving builds" became "must not be present in any build", and `_EEPROM_start` (referenced by the probe) is now always required with the stock address, like `_FS_start` and `_FS_end`. The forbidden-symbol list (EEPROM, LittleFS, SPIFFS, HTTP updater, ArduinoOTA, HTTPClient) is unchanged. A new rule makes the quest build fail if the display-test scene code is linked in.
- `lint_safety.py` L2: the exemption that let `persist.cpp` access flash is gone: no firmware file may call `flashWrite`, `flashEraseSector`, EEPROM, LittleFS or SPIFFS functions, and `flashRead` (the probe) is allowed in `prefs.cpp` only. L8 now requires `-DGQ_AUTO_RESTART=1` (approved) and `-DGQ_STRICT_UPLOAD=0`, and rejects `GQ_FLASH_SAVE` and `GQ_NET_NO_SLEEP` anywhere and `-DGQ_TEST_SCENE=1` outside `[env:test]`. New L9 forbids floating point formatting and parsing (it would pull 14 KB of C library back in).
- `recovery_guard.py` now watches `firmware/prefs.*` and `tools/pio_slim.py` instead of `firmware/persist.*`. CI no longer builds the `quest_save` environment.

## What cannot be protected

- A power cut during the few seconds after an install while the clock copies the new firmware into place (rare). Keep the power on for 60 seconds.
- A bug in the recovery code itself that neither crashes nor serves the page. That code is small, rarely changed, and every change to it is marked in the commit and in the build.
- Hardware faults.
- Someone else on your WiFi or near the clock opening the update page (it has no password on purpose: a password could lock you out).
