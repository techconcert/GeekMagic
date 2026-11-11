# Recovery guide (for the owner of the clock)

You do not need to know how to program to use this page. It tells you what to do when something goes wrong, what to check before you install a new version, and what to look at after you install one.

Two addresses matter:

- Your clock on the home WiFi: `http://geekquest.local/` (or the IP address shown at the bottom of the clock screen or in your router's list).
- The clock's own rescue network: join the WiFi called `GEEKQUEST-XXXX` (the password is `GEEKQUEST`), then open `http://192.168.4.1/`. This network is switched on for the first 10 minutes after every power-on, and whenever the clock cannot find your home WiFi.

The page `/update` (for example `http://192.168.4.1/update`) installs a new version of the firmware. It is the most important page: every version of GeekQuest has it, and the clock starts it before it starts the game, so a game problem cannot hide it.

## What can go wrong, and what to do

| What you see | What to do |
|---|---|
| The clock works, but something in the game looks wrong | Open the update page and install the last version you trusted (the file in `firmware/known-good/`, or an older file from `firmware/obsolete/`). |
| The screen shows SAFE MODE and the name of the rescue network | The game crashed several times in a row, so the clock stopped running it. The update page still works. Install a fixed version, or press **Restart** on the status page to try the game again. |
| The game crashes every few minutes | After 5 crashes the clock goes into safe mode by itself. Then install a fixed or older version. |
| The status page says **Saved game quarantined** | The clock crashed right after loading your saved game, so it started a fresh game and kept the old save untouched. Press **Try the saved game again**, or **Discard the saved game**. |
| The game state looks like nonsense | On the status page press **Factory reset game** (it starts again on day 1 and keeps your settings), or **Restore backup** with a backup file you downloaded earlier. |
| The screen is black or garbled | The web page does not depend on the screen. Open it over WiFi or through the rescue network. |
| The clock is no longer on your home WiFi | Unplug it, plug it in, and within 10 minutes join `GEEKQUEST-XXXX` and open `http://192.168.4.1/wifi` to enter the WiFi name and password again. |
| The clock lost the WiFi because the router was off | Nothing to do. The clock keeps trying with longer and longer pauses and connects again by itself. The bottom band shows the rescue network name and password meanwhile. |
| The time shows `--:--` | The clock has not reached a time server yet (or the power was cut). It fixes itself once the WiFi is back. |
| You want the original clock firmware back | On the update page choose the original `SDPro_V1.0.6...bin` file. If you ever turned on flash saving, press **Disable and erase** first. |
| A file you chose on the update page is refused | Read the message. Nothing was installed and the clock keeps running. (Today the extra checks only warn; see below.) |
| No rescue network after unplugging and replugging, and no web page | This is the one case that cannot be fixed without a cable and a serial adapter. Everything in this guide exists to keep it from happening. |

Things that are **safe** to do at any time: opening any page, **Download backup**, **Restart**, **Restart in safe mode**.

## Your game and settings: what survives what

| Event | Settings and game |
|---|---|
| Restart button, update, crash, watchdog | Kept (stored in the clock's short-term memory). |
| Unplugging the clock | Lost, unless flash saving was switched on (it is not, yet). |
| Anything, if you have a backup file | **Restore backup** brings it back. |

So: before every update, and from time to time, press **Download backup** and keep the file (`geekquest-dayN.gqs`) on your phone or computer.

## Switches that are still off

These need your written OK before they are switched on (they change what the clock does by itself):

- **Flash saving** (survive a power cut). Off. The clock never writes to its permanent memory except when you install firmware.
- **Automatic restarts** (when WiFi is gone for hours, when memory is low, once a week at night). Off: the clock only notes on the status page that it would have restarted.
- **Strict upload checks** (refuse a file that arrived damaged or is not a GeekQuest build). Off: the update page already shows the file's CRC32 number and warns, and the clock records what the check would have said, but it installs exactly as before.

## Before you install a new version (pre-flash checklist)

1. The file comes from the `builds` branch, folder `firmware/` (not `obsolete/`), its name starts with `SDP_GeekQuest_`, and the GitHub check for that version is green.
2. You have the last known-good `.bin` and the original `SDPro_V1.0.6` file on the phone or computer you are installing from.
3. The clock has steady power and will not be moved for two minutes.
4. On the clock's status page: **Room for update** is larger than the new file, and the crash counter is 0.
5. Press **Download backup** and keep the file.
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

6. Change the time zone, brightness and colour depth, then press **Restart**. They are still set and the game is on the same day with the same coins.
7. Unplug and plug in: settings and game are back to defaults (expected while flash saving is off). **Restore backup** with the file from step 5 above brings them back.
8. The status page has the **Network** and **Health** boxes. Open `/info`: one line of JSON you can paste into a bug report.
9. Choose a random file (not firmware) on the update page: you get a warning and the clock keeps running. (With the strict checks off the upload would still be accepted if you pressed the button, so do not.)
10. Choose the original `SDPro` file but **do not press upload**. Write down the CRC32 the page shows and send it to the maintainer (it lets the clock recognise the original firmware exactly).
11. Only if flash saving was approved for testing: write down the **Save sector** line. After a few days of use press **Enable saving to flash**, then repeat step 7: now settings and game survive unplugging.

For the content version (R2) in addition: the hero is on screen or clearly marked at all times; after a week at least 3 goals bought, the season changed once, and the crash counter has not gone up.

## What the checks in the build protect

Every `.bin` is refused by the build (no file is produced) unless it has the update page, the rescue network, safe mode, backup and WiFi routes, fits in 500 KB, uses the original flash layout, contains no code that could touch the original firmware's files or settings, and uses about the same amount of memory as before. See `tools/check_bin.py`.

## What cannot be protected

- A power cut during the few seconds after an install while the clock copies the new firmware into place (rare). Keep the power on for 60 seconds.
- A bug in the recovery code itself that neither crashes nor serves the page. That code is small, rarely changed, and every change to it is marked in the commit and in the build.
- Hardware faults.
- Someone else on your WiFi or near the clock opening the update page (it has no password on purpose: a password could lock you out).
