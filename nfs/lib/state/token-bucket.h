#ifndef _TB_H_INCLUDED_
#define _TB_H_INCLUDED_

#include <stdbool.h>
#include <stdint.h>

#include "../util/time.h"

struct TokenBucket;

int tb_allocate(uint32_t capacity, uint64_t rate, uint64_t burst, uint32_t key_size, struct TokenBucket **tb_out);
int tb_is_tracing(struct TokenBucket *tb, void *k, int *index_out);
int tb_trace(struct TokenBucket *tb, void *k, uint32_t pkt_len, time_ns_t time, int *index_out);
int tb_update_and_check(struct TokenBucket *tb, int index, uint32_t pkt_len, time_ns_t time);
int tb_expire(struct TokenBucket *tb, time_ns_t time);

#endif