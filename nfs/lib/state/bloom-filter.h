#ifndef _BLOOM_FILTER_H_INCLUDED_
#define _BLOOM_FILTER_H_INCLUDED_

#include "bloom-filter-util.h"

#include <stdbool.h>
#include <stdint.h>

#include "../util/time.h"

struct BloomFilter;

int bf_allocate(uint32_t height, uint32_t width, uint32_t key_size,
                time_ns_t periodic_cleanup_interval,
                struct BloomFilter **bf_out);
void bf_set(struct BloomFilter *bf, void *key);
int bf_query(struct BloomFilter *bf, void *key);
int bf_periodic_cleanup(struct BloomFilter *bf, time_ns_t now);

#endif