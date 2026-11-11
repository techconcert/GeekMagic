# Brief for engineers implementing a design chunk

You implement ONE chunk of docs/DESIGN.md in your own git worktree (based on main, which already contains chunk 0).

## Read first, in this order
README.md, docs/0A-NOTES.md, docs/DESIGN.md sections 0, 8.10 (recovery contract, binding), 10 (interfaces are FINAL), 11.1, your chunk's section in 11 (steps, tests, acceptance), 13, plus the sections your chunk's text points to. Then the existing code under lib/gamecore/src, lib/gqsafe, firmware, tools, test, preview.

## Rules for the game core (lib/gamecore)
No heap, no libc, no floating point, no exceptions/RTTI/STL. Tables are `static const ... GC_ROM_ATTR` read via GC_RD8/GC_RD16. Every loop is bounded, every table index range-checked or masked, saved-state values clamped on load. Code must compile for ESP8266 (Arduino/PROGMEM), wasm32 (tools/build_preview.sh) and host g++ with sanitizers. Do NOT change the signatures of functions firmware/main.cpp calls (gc_init, gc_tick, gc_set_clock, gc_set_status, gc_set_fps, gc_set_depth, gc_dirty_mask, gc_clear_dirty, gc_mark_all_dirty, gc_render_strip); only add.

## Ownership
Own ONLY the files your chunk lists under "Files". Other engineers work in parallel on the other chunks; stay inside your ownership and the frozen interfaces, and compile/test against the stubs of the others. Do not touch firmware/**, platformio.ini, tools/check_bin.py, tools/pio_post.py, lib/gqsafe/**, .github/workflows/** unless you are chunk 1.

## Recovery is sacred
The user has NO serial adapter. The web update page, rescue hotspot and safe mode must keep working in every build. Never write flash outside the Updater. Approvals: A5 (own WiFi ladder) is approved and in main. NOT approved, so keep OFF by default behind flags that default to 0: A1 flash saving (GQ_FLASH_SAVE), A2 automatic restarts (GQ_AUTO_RESTART), A3 modem sleep off, A4 strict upload checks (GQ_STRICT_UPLOAD).

## Build and test
No ESP8266 toolchain here (CI compiles the firmware). Host tests:
`g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wextra -Werror test/test_<name>.cpp lib/gamecore/src/*.cpp lib/gqsafe/src/*.cpp`  (the second glob may match nothing; tolerate).
Preview: tools/build_preview.sh. Check visuals with Playwright (`require('playwright')`, chromium preinstalled): screenshot the #lcd canvas at deviceScaleFactor 1, LOOK at the screenshot, fix what looks wrong. The .bin must stay well under the 500 KB gate (it is ~388 KB now, shared by all chunks; keep to your chunk's budget).

## Commits
Commit in small steps. Every commit message ends with the two lines
`Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`
`Claude-Session: https://claude.ai/code/session_014RL3AjMvv4xj63DtDdpdwY`
COMMIT AT LEAST EVERY 10-15 MINUTES with a message saying what is done: the lead reports your progress from git log. Do not push or merge.

## Final report (under 200 words)
Branch, worktree path, tests passing, size estimate, visual findings, deviations from the design.
