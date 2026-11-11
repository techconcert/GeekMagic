#pragma once
#include "err.h"
typedef struct { uint32_t addr; } ip_addr_t;
#define ip_addr_isany(a) ((a) == 0 || (a)->addr == 0)
#define ip_addr_copy(d, s) ((d).addr = (s).addr)
