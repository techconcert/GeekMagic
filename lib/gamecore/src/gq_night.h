// The game's built-in night, in minutes of the day. A header of its own so the status page can show it without
// including the game (the recovery layer stays independent of the core).
#pragma once
#define GC_NIGHT_START_MIN (22 * 60)
#define GC_NIGHT_END_MIN   (6 * 60)
