# Package 0A notes (deviations and interpretations)

1. **`friend` field.** `friend` is a C++ keyword, so `GqSave` calls it `friend_` (same offset 128).
2. **Art flash.** Adding 151 placeholder cells as separate 128-byte arrays would cost about 19 KB of flash in release R0, over the 10 KB budget. `png2tiles.py` therefore stores identical cells once (`GC_<NAME>_DATA` plus `GC_<NAME>_MAP`, read with `GC_TILES(id)`, `GC_CHARS(id)`, `GC_ITEMS(id)`). Chunk 7 paints in the PNGs as planned; the generator handles the rest. `tools/make_art.py` is now legacy (it still writes the old layout and `hero.png`); do not run it, edit the PNGs.
3. **`day` is 0-based** (first game day = 0), so that `season = (day/7)%4` and `day%7+1` of design 5.1 hold. `GQ_Q_DAY` returns that value.
4. **Events.** `gq_emit` delivers immediately when called outside a handler; events raised inside a handler wait in the 8-deep queue. A delivery loop is bounded at 64. `gq_pump()` (internal, `gc_internal.h`) also runs after `cal_tick` so NEW_DAY resets quotas before the hero chooses.
5. **Additions to `gq_mod.h`:** `quest_count()` and `quest_steps(q)` (stubs return 0), needed by `gq_save_sanitize` to clamp `q_active`/`q_step`. `GqLook` is defined as in design 6.2.
6. **Hash test.** The step-3 hash equality was verified (commit "Step 3"); steps 4 to 9 were committed together and behaviour changes on purpose, so `test_hash_baseline.cpp` was removed.
7. **Restore and the clock.** After `gc_save_import` the minute comes from the wall clock if valid (design 5.1), never from the save, and the day is never advanced by a restore.
8. **Presence.** Computed after the whole world tick. While walking off or on the screen edge the hero is counted as away (postcard) once outside the screen. Quest trips pick a place at departure; the postcard icon is a tile cell until chunk 7 paints icons.
9. **`build.yml`** is a recovery-contract file (8.10 rule 1). The commit carries a `RECOVERY-CHANGE` line, but `GQ_RECOVERY_REV` lives in `safety.cpp`, which belongs to package 0B.
10. HUD icons use the untinted `g_ui` palette (they no longer darken at night).
