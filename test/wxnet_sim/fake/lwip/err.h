#pragma once
#include <stdint.h>
typedef int8_t err_t; typedef uint16_t u16_t; typedef uint8_t u8_t;
enum { ERR_OK = 0, ERR_MEM = -1, ERR_INPROGRESS = -5, ERR_ABRT = -13, ERR_RST = -14, ERR_VAL = -6 };
