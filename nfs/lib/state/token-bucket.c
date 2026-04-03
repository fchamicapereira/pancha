#include "token-bucket.h"

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include "map.h"
#include "vector.h"
#include "double-chain.h"
#include "../util/time.h"
#include "../util/expirator.h"

#include <rte_malloc.h>

struct TokenBucket {
  struct Map *flows;
  struct Vector *keys;
  struct Vector *buckets;
  struct DoubleChain *allocator;

  uint32_t capacity;
  uint64_t rate;
  uint64_t burst;
  uint32_t key_size;
};

struct tb_bucket {
  uint64_t tokens;
  time_ns_t time;
};

int tb_allocate(uint32_t capacity, uint64_t rate, uint64_t burst, uint32_t key_size, struct TokenBucket **tb_out) {
  struct TokenBucket *tb_alloc = (struct TokenBucket *)rte_malloc("struct TokenBucket", sizeof(struct TokenBucket), 64);
  if (tb_alloc == NULL) {
    return 0;
  }

  (*tb_out) = tb_alloc;

  (*tb_out)->capacity = capacity;
  (*tb_out)->rate     = rate;
  (*tb_out)->burst    = burst;
  (*tb_out)->key_size = key_size;

  (*tb_out)->flows = NULL;
  if (map_allocate(capacity, key_size, &((*tb_out)->flows)) == 0) {
    rte_free(tb_alloc);
    return 0;
  }

  (*tb_out)->keys = NULL;
  if (vector_allocate(key_size, capacity, &((*tb_out)->keys)) == 0) {
    rte_free(tb_alloc);
    return 0;
  }

  (*tb_out)->buckets = NULL;
  if (vector_allocate(sizeof(struct tb_bucket), capacity, &((*tb_out)->buckets)) == 0) {
    rte_free(tb_alloc);
    return 0;
  }

  (*tb_out)->allocator = NULL;
  if (dchain_allocate(capacity, &((*tb_out)->allocator)) == 0) {
    rte_free(tb_alloc);
    return 0;
  }

  return 1;
}

int tb_is_tracing(struct TokenBucket *tb, void *k, int *index_out) { return map_get(tb->flows, k, index_out); }

int tb_trace(struct TokenBucket *tb, void *k, uint32_t pkt_len, time_ns_t time, int *index_out) {
  int allocated = dchain_allocate_new_index(tb->allocator, index_out, time);

  if (!allocated) {
    return 0;
  }

  void *key                = 0;
  struct tb_bucket *bucket = 0;

  vector_borrow(tb->keys, *index_out, (void **)&key);
  vector_borrow(tb->buckets, *index_out, (void **)&bucket);

  memcpy(key, k, tb->key_size);
  bucket->tokens = tb->burst - pkt_len;
  bucket->time   = time;

  map_put(tb->flows, key, *index_out);

  vector_return(tb->keys, *index_out, key);
  vector_return(tb->buckets, *index_out, bucket);

  return 1;
}

int tb_update_and_check(struct TokenBucket *tb, int index, uint32_t pkt_len, time_ns_t time) {
  dchain_rejuvenate_index(tb->allocator, index, time);

  struct tb_bucket *bucket = 0;
  vector_borrow(tb->buckets, index, (void **)&bucket);

  uint64_t time_u    = (uint64_t)time;
  uint64_t time_diff = time_u - bucket->time;

  if (time_diff < tb->burst * NS_TO_S_MULTIPLIER / tb->rate) {
    uint64_t added_tokens = time_diff * tb->rate / NS_TO_S_MULTIPLIER;
    bucket->tokens += added_tokens;
    if (bucket->tokens > tb->burst) {
      bucket->tokens = tb->burst;
    }
  } else {
    bucket->tokens = tb->burst;
  }

  bucket->time = time_u;

  int pass = 0;
  if (bucket->tokens > pkt_len) {
    bucket->tokens -= pkt_len;
    pass = 1;
  }

  vector_return(tb->buckets, index, bucket);

  return pass;
}

int tb_expire(struct TokenBucket *tb, time_ns_t time) {
  assert(time >= 0); // we don't support the past
  time_ns_t exp_time = NS_TO_S_MULTIPLIER * (tb->burst / tb->rate);
  uint64_t time_u    = (uint64_t)time;
  // OK because time >= tb->burst / tb->rate >= 0
  time_ns_t min_time = time_u - exp_time;
  return expire_items_single_map(tb->allocator, tb->keys, tb->flows, min_time);
}
