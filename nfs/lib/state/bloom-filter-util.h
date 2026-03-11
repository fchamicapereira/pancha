#ifndef _BLOOM_FILTER_UTIL_H_INCLUDED_
#define _BLOOM_FILTER_UTIL_H_INCLUDED_

#define BF_MAX_SALTS_BANK_SIZE 64

#include <stdint.h>

extern const uint32_t BF_SALTS[BF_MAX_SALTS_BANK_SIZE];
#endif