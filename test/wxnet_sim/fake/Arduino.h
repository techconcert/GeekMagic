#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
uint32_t millis(); uint32_t micros();
struct ESPClass { uint32_t getFreeHeap(); uint32_t getMaxFreeBlockSize(); uint32_t getChipId(); };
extern ESPClass ESP;
struct String { const char* c_str() const { return ""; } };
