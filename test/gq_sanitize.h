// Test helper: clamps a randomly filled GqSave to the value ranges of the game (the game itself no longer
// loads saved states, so only the fuzzing tests need this). Include after gq_mod.h.
#pragma once
#include "../lib/gamecore/src/gq_mod.h"

static inline uint8_t clamp8(uint8_t v, uint8_t hi) { return v > hi ? hi : v; }

