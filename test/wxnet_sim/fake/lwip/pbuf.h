#pragma once
#include "err.h"
struct pbuf { pbuf* next; void* payload; u16_t tot_len; u16_t len; };
void pbuf_free(pbuf* p);
